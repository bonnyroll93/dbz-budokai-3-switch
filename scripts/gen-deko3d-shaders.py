#!/usr/bin/env python3
"""T22: precompile the guest's RSX programs for the Switch deko3d backend.

The draw engine hands every backend the decompilers' HLSL. The Switch has no
HLSL compiler, and uam (deko3d's compiler) runs on the PC and only takes GLSL,
so the programs are compiled here, ahead of time:

  1. the backend dumps each distinct pipeline's HLSL to the SD card
     (sdmc:/switch/dbz-budokai-3-switch/t23-shaders/vs_<hash>.hlsl, ps_<hash>.hlsl);
  2. those files are copied into shaders/deko3d/hlsl/;
  3. this script translates each one to GLSL, compiles it with uam and writes
     libs/video/deko3d/rsx_deko3d_shaders.h, a table keyed by the same FNV-1a
     hash of the HLSL text that the backend computes at run time.

The engine's own built-in pair (kEngFixedVS / kEngFixedPS in
rsx_draw_engine.c) is always included.

The translation only has to cover the narrow HLSL dialect the two decompilers
emit (rsx_vp_decompiler.c, rsx_fp_decompiler.c): structs with semantics,
cbuffers, a Texture2D bank, C-style casts, a handful of intrinsics. It keeps
the HLSL structs and functions as they are and generates the GLSL shell
(inputs, outputs, uniform blocks) around a renamed hlsl_main().

Usage:  python scripts/gen-deko3d-shaders.py [--uam PATH]
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIDEO = os.environ.get("PS3RECOMP_VIDEO_DIR") or os.path.join(ROOT, "third_party", "ps3recomp", "libs", "video")
HLSL_DIR = os.path.join(ROOT, "shaders", "deko3d", "hlsl")
GLSL_DIR = os.path.join(VIDEO, "deko3d", "glsl")
HEADER = os.path.join(VIDEO, "deko3d", "rsx_deko3d_shaders.h")
ENGINE_C = os.path.join(VIDEO, "rsx_draw_engine.c")
DEFAULT_UAM = r"C:\devkitPro\tools\bin\uam.exe"
STAMP = os.path.join(ROOT, "generated", "shaders", "deko3d", "prebuild-state.json")


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


# ---- the GLSL prelude: HLSL names the decompilers use, as macros -----------

PRELUDE = """#version 460
#define float4 vec4
#define float3 vec3
#define float2 vec2
#define int4 ivec4
#define int3 ivec3
#define int2 ivec2
#define uint4 uvec4
#define bool4 bvec4
#define lerp mix
#define frac fract
#define rsqrt inversesqrt
#define saturate(x) clamp((x), 0.0, 1.0)
"""

# Varying locations, by semantic. The VS writes and the PS reads by these.
VARYING_LOC = {"COLOR0": 0, "COLOR1": 1, "FOG": 2}
for _n in range(8):
    VARYING_LOC["TEXCOORD%d" % _n] = 3 + _n

CAST_TYPES = ("float4", "float3", "float2", "float", "int4", "int2", "int",
              "uint4", "uint", "bool4", "bool")
VECTOR_CASTS = {"float4", "float3", "float2", "int4", "int2", "uint4", "bool4"}
CMP_FUNCS = {"<": "lessThan", "<=": "lessThanEqual", ">": "greaterThan",
             ">=": "greaterThanEqual", "==": "equal", "!=": "notEqual"}


class TranslateError(Exception):
    pass


def match_paren(s, i):
    """Index of the bracket closing the one at s[i]."""
    pairs = {"(": ")", "[": "]"}
    want = [pairs[s[i]]]
    j = i + 1
    while j < len(s):
        c = s[j]
        if c in pairs:
            want.append(pairs[c])
        elif c in ")]":
            if not want or c != want[-1]:
                raise TranslateError("unbalanced brackets at %d" % j)
            want.pop()
            if not want:
                return j
        j += 1
    raise TranslateError("unterminated bracket at %d" % i)


def split_top(s, seps):
    """Split at top-level occurrences of any separator; returns pieces and the
    separators found, in order."""
    parts, found, depth, last, i = [], [], 0, 0, 0
    seps = sorted(seps, key=len, reverse=True)
    while i < len(s):
        c = s[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif depth == 0:
            hit = next((sp for sp in seps if s.startswith(sp, i)), None)
            if hit:
                parts.append(s[last:i])
                found.append(hit)
                i += len(hit)
                last = i
                continue
        i += 1
    parts.append(s[last:])
    return parts, found


def top_comparison(s):
    """(lhs, op, rhs) when s is `A op B` at top level, else None."""
    depth, i = 0, 0
    while i < len(s):
        c = s[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif depth == 0:
            for op in ("<=", ">=", "==", "!=", "<", ">"):
                if s.startswith(op, i):
                    return s[:i], op, s[i + len(op):]
        i += 1
    return None


OPERAND_RE = re.compile(r"[A-Za-z_0-9.]+")


def read_operand(s, i):
    """The operand a cast applies to, starting at s[i]: a parenthesised group,
    or a name/number with any call, index and swizzle postfixes."""
    while i < len(s) and s[i] == " ":
        i += 1
    start = i
    if i < len(s) and s[i] == "(":
        end = match_paren(s, i) + 1
    else:
        m = OPERAND_RE.match(s, i)
        if not m:
            raise TranslateError("cast without operand at %d: %r" % (i, s[i:i + 30]))
        end = m.end()
    while end < len(s) and s[end] in "([.":
        if s[end] == ".":
            m = OPERAND_RE.match(s, end + 1)
            end = m.end() if m else end + 1
        else:
            end = match_paren(s, end) + 1
    return start, end


CAST_RE = re.compile(r"\((%s)\)" % "|".join(CAST_TYPES))
SAMPLE_RE = re.compile(r"(rsx_v?tex(?:\[\d+\]|\d+))\.(Sample|SampleLevel|SampleBias)\(")


def xlate_expr(s):
    """Rewrite casts and texture sampling, recursively."""
    out, i = [], 0
    while i < len(s):
        mc = CAST_RE.search(s, i)
        ms = SAMPLE_RE.search(s, i)
        m = min((x for x in (mc, ms) if x), key=lambda x: x.start(), default=None)
        if not m:
            out.append(s[i:])
            break
        out.append(s[i:m.start()])
        if m is mc:
            typ = m.group(1)
            a, b = read_operand(s, m.end())
            operand = s[a:b]
            inner = operand[1:-1] if operand.startswith("(") and \
                match_paren(operand, 0) == len(operand) - 1 else None
            cmp = top_comparison(inner) if inner is not None else None
            if cmp and typ in VECTOR_CASTS:
                lhs, op, rhs = cmp
                out.append("%s(%s(%s, %s))" % (typ, CMP_FUNCS[op],
                                               xlate_expr(lhs.strip()),
                                               xlate_expr(rhs.strip())))
            else:
                body = inner if inner is not None else operand
                out.append("%s(%s)" % (typ, xlate_expr(body)))
            i = b
        else:
            tex, fn = m.group(1), m.group(2)
            open_at = m.end() - 1
            close_at = match_paren(s, open_at)
            args, _ = split_top(s[open_at + 1:close_at], [","])
            args = [xlate_expr(x.strip()) for x in args[1:]]   # drop the sampler
            if fn == "Sample":
                out.append("texture(%s, %s)" % (tex, ", ".join(args)))
            elif fn == "SampleBias":
                out.append("texture(%s, %s, %s)" % (tex, args[0], args[1]))
            else:
                out.append("textureLod(%s, %s, %s)" % (tex, args[0], args[1]))
            i = close_at + 1
    return "".join(out)


STRUCT_RE = re.compile(r"struct\s+(\w+)\s*\{(.*?)\};", re.S)
CBUF_RE = re.compile(r"cbuffer\s+(\w+)\s*:\s*register\(b(\d+)\)\s*\{(.*?)\};", re.S)
TEX_RE = re.compile(r"(Texture2D|TextureCube)\s+(\w+)(\[\d+\])?\s*:\s*register\(t(\d+)\);")
SAMP_RE = re.compile(r"SamplerState\s+\w+(\[\d+\])?\s*:\s*register\(s\d+\);")
MAIN_RE = re.compile(r"(\w+)\s+main\s*\(\s*(\w+)\s+input\s*\)\s*(:\s*\w+\s*)?\{")


def parse_fields(body):
    fields = []
    for decl in body.split(";"):
        decl = decl.strip()
        if not decl:
            continue
        m = re.match(r"(\w+)\s+(\w+)\s*(?::\s*(\w+))?$", decl)
        if not m:
            raise TranslateError("struct field %r" % decl)
        fields.append((m.group(1), m.group(2), (m.group(3) or "").upper()))
    return fields



def rewrite_guarded_div(src):
    """(abs(A) > 0.0 ? B : (float4)0.0) -> mix(vec4(0.0), B, greaterThan(abs(A), vec4(0.0)))."""
    out, i = [], 0
    while True:
        j = src.find("(abs(", i)
        if j < 0:
            out.append(src[i:])
            return "".join(out)
        end = match_paren(src, j)
        inner = src[j + 1:end]
        a_end = match_paren(inner, 3)
        head, tail = inner[a_end + 1:], " : (float4)0.0"
        if head.startswith(" > 0.0 ? ") and inner.endswith(tail):
            a = inner[4:a_end]
            b = head[len(" > 0.0 ? "):-len(tail)]
            out.append(src[i:j])
            out.append("mix(vec4(0.0), (%s), greaterThan(abs(%s), vec4(0.0)))" % (b, a))
            i = end + 1
        else:
            out.append(src[i:j + 1])
            i = j + 1

def translate(hlsl, stage):
    src = hlsl.replace("\r\n", "\n")
    src = re.sub(r"\[(unroll|loop|branch|flatten)\]\s*", "", src)

    structs = {}

    def take_struct(m):
        structs[m.group(1)] = parse_fields(m.group(2))
        return ""
    src = STRUCT_RE.sub(take_struct, src)

    decls = []

    def take_cbuf(m):
        decls.append("layout(std140, binding = %s) uniform %s {%s};"
                     % (m.group(2), m.group(1), m.group(3)))
        return ""
    src = CBUF_RE.sub(take_cbuf, src)

    def take_tex(m):
        kind = "samplerCube" if m.group(1) == "TextureCube" else "sampler2D"
        decls.append("layout(binding = %s) uniform %s %s%s;"
                     % (m.group(4), kind, m.group(2), m.group(3) or ""))
        return ""
    src = TEX_RE.sub(take_tex, src)
    src = SAMP_RE.sub("", src)

    m = MAIN_RE.search(src)
    if not m:
        raise TranslateError("no main()")
    ret_type, in_type = m.group(1), m.group(2)
    src = src[:m.start()] + "%s hlsl_main(%s hin) {" % (ret_type, in_type) + src[m.end():]
    src = re.sub(r"\binput\b", "hin", src)
    # The NaN guard is a per-component select in HLSL.
    src = re.sub(r"\((\w+) == \1\) \? \1 : \(float4\)0",
                 r"mix(vec4(0.0), \1, equal(\1, \1))", src)
    # The RSX DIV guard `abs(a) > 0.0 ? b : 0` is a per-component select too.
    src = rewrite_guarded_div(src)
    # A uniform is not a constant expression in GLSL.
    src = src.replace("const float _rsx_alpha_ref", "float _rsx_alpha_ref")
    # dot(v, 1.0): HLSL widens the scalar.
    src = re.sub(r"dot\(([^(),]*\([^()]*\)[^(),]*|[^(),]+), 1\.0\)",
                 r"dot(\1, vec4(1.0))", src)
    src = xlate_expr(src)

    if in_type not in structs:
        raise TranslateError("input struct %s not declared" % in_type)

    shell = []
    for name, fields in structs.items():
        shell.append("struct %s {" % name)
        for typ, fname, _sem in fields:
            shell.append("    %s %s;" % (typ, fname))
        shell.append("};")

    entry = ["void main() {", "    %s hin;" % in_type]
    if stage == "vert":
        for typ, fname, sem in structs[in_type]:
            if sem.startswith("ATTR"):
                n = int(sem[4:])
                shell.append("layout(location = %d) in vec4 rsx_in_%s;" % (n, fname))
                entry.append("    hin.%s = rsx_in_%s;" % (fname, fname))
            elif sem == "SV_VERTEXID":
                entry.append("    hin.%s = uint(gl_VertexID);" % fname)
            else:
                raise TranslateError("VS input semantic %s" % sem)
        if ret_type not in structs:
            raise TranslateError("VS output struct %s not declared" % ret_type)
        entry.append("    %s rsx_o = hlsl_main(hin);" % ret_type)
        for typ, fname, sem in structs[ret_type]:
            if sem == "SV_POSITION":
                entry.append("    gl_Position = rsx_o.%s;" % fname)
            elif sem in VARYING_LOC:
                shell.append("layout(location = %d) out vec4 rsx_v%d;" % (VARYING_LOC[sem], VARYING_LOC[sem]))
                entry.append("    rsx_v%d = rsx_o.%s;" % (VARYING_LOC[sem], fname))
            else:
                raise TranslateError("VS output semantic %s" % sem)
    else:
        for typ, fname, sem in structs[in_type]:
            if sem == "SV_POSITION":
                entry.append("    hin.%s = vec4(gl_FragCoord.xyz, 1.0 / gl_FragCoord.w);" % fname)
            elif sem in VARYING_LOC:
                shell.append("layout(location = %d) in vec4 rsx_v%d;" % (VARYING_LOC[sem], VARYING_LOC[sem]))
                entry.append("    hin.%s = rsx_v%d;" % (fname, VARYING_LOC[sem]))
            else:
                raise TranslateError("PS input semantic %s" % sem)
        if ret_type in structs:
            entry.append("    %s rsx_o = hlsl_main(hin);" % ret_type)
            for typ, fname, sem in structs[ret_type]:
                t = int(sem[len("SV_TARGET"):] or 0)
                shell.append("layout(location = %d) out vec4 rsx_out%d;" % (t, t))
                entry.append("    rsx_out%d = rsx_o.%s;" % (t, fname))
        else:
            shell.append("layout(location = 0) out vec4 rsx_out0;")
            entry.append("    rsx_out0 = hlsl_main(hin);")
    entry.append("}")

    return (PRELUDE + "\n".join(decls) + "\n" + "\n".join(shell) + "\n" +
            src.strip() + "\n\n" + "\n".join(entry) + "\n")



# The Switch backend emits vs_<hash>.hlsl / ps_<hash>.hlsl. The hash is the
# same exact-content FNV key used by dk_hash_str at runtime.
CAPTURE_RE = re.compile(r"^(vs|ps)_([0-9a-fA-F]{16})\.hlsl$", re.I)


def import_capture_dirs(paths):
    """Import byte-exact Switch captures into the project shader directory."""
    staged = {}
    inputs = 0
    for directory in paths:
        directory = Path(directory)
        if not directory.is_dir():
            raise TranslateError("capture directory does not exist: %s" % directory)
        for path in sorted(directory.iterdir()):
            if not path.is_file():
                continue
            match_hash = CAPTURE_RE.fullmatch(path.name)
            if not match_hash:
                continue
            raw = path.read_bytes()
            if not raw or b"\x00" in raw:
                raise TranslateError("empty or non-text shader: %s" % path)
            stage = match_hash.group(1).lower()
            hashvalue = fnv1a64(raw)
            if hashvalue != int(match_hash.group(2), 16):
                raise TranslateError("%s: filename/hash mismatch" % path)
            filename = "%s_%016x.hlsl" % (stage, hashvalue)
            previous = staged.get(filename)
            if previous is not None and previous != raw:
                raise TranslateError("content conflict for %s" % filename)
            staged[filename] = raw
            inputs += 1
    if paths and not inputs:
        raise TranslateError("no VS/PS HLSL captures found in the provided directories")
    os.makedirs(HLSL_DIR, exist_ok=True)
    added = 0
    for name, data in staged.items():
        dest = Path(HLSL_DIR) / name
        if dest.exists():
            if dest.read_bytes() != data:
                raise TranslateError("existing shader differs: %s" % dest)
            continue
        dest.write_bytes(data)
        added += 1
    if paths:
        print("Imported %d unique shaders (%d new) from %d captures" %
              (len(staged), added, inputs))
    return added


def input_signature(uam):
    """Content-addressed rebuild, including toolchain and source changes."""
    digest = hashlib.sha256()
    sources = [Path(__file__), Path(ENGINE_C)]
    if os.path.isdir(HLSL_DIR):
        sources += sorted(Path(HLSL_DIR).glob("*.hlsl"))
        sources += sorted((Path(HLSL_DIR) / "override").glob("*.hlsl"))
        sources += sorted((Path(HLSL_DIR) / "builtin").glob("*.hlsl"))
    for path in sources:
        digest.update(str(path.resolve()).encode("utf-8"))
        digest.update(path.read_bytes())
    uam_path = Path(uam)
    if not uam_path.is_file():
        raise TranslateError("uam not found: %s" % uam)
    stat = uam_path.stat()
    digest.update(str(uam_path.resolve()).encode("utf-8"))
    digest.update(('%d:%d' % (stat.st_size, stat.st_mtime_ns)).encode('ascii'))
    return digest.hexdigest()

# ---- inputs -----------------------------------------------------------------

def c_string_literal(name, text):
    """The value of `static const char* const NAME = "..." "...";` in C source."""
    m = re.search(r"\b%s\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;" % name, text)
    if not m:
        raise TranslateError("%s not found in rsx_draw_engine.c" % name)
    out = []
    for piece in re.findall(r"\"((?:[^\"\\]|\\.)*)\"", m.group(1)):
        out.append(piece.encode("latin-1").decode("unicode_escape"))
    return "".join(out)


def collect():
    shaders = []   # (stage, hash, hlsl text, origin)
    with open(ENGINE_C, "r", encoding="latin-1") as f:
        engine = f.read()
    for name, stage in (("kEngFixedVS", "vert"), ("kEngFixedPS", "frag")):
        text = c_string_literal(name, engine)
        shaders.append((stage, fnv1a64(text.encode("latin-1")), text, name))
    builtin_dir = Path(HLSL_DIR) / "builtin"
    for name, stage in (("fxaa_vs.hlsl", "vert"), ("fxaa_ps.hlsl", "frag")):
        path = builtin_dir / name
        if path.is_file():
            raw = path.read_bytes()
            origin = "kDkFxaaVS" if stage == "vert" else "kDkFxaaPS"
            shaders.append((stage, fnv1a64(raw), raw.decode("latin-1"), origin))
    if os.path.isdir(HLSL_DIR):
        for fn in sorted(os.listdir(HLSL_DIR)):
            m = re.match(r"(vs|ps)_([0-9a-f]{16})\.hlsl$", fn)
            if not m:
                continue
            with open(os.path.join(HLSL_DIR, fn), "rb") as f:
                raw = f.read()
            h = fnv1a64(raw)
            if h != int(m.group(2), 16):
                raise TranslateError("%s: content hash %016x does not match its name"
                                     % (fn, h))
            text = raw.decode("latin-1")
            # hlsl/override/<same name>: compile this text under the original
            # hash instead (diagnostic variants, hand fixes).
            ov = os.path.join(HLSL_DIR, "override", fn)
            if os.path.isfile(ov):
                with open(ov, "rb") as f:
                    text = f.read().decode("latin-1")
                fn = fn + " (override)"
            shaders.append(("vert" if m.group(1) == "vs" else "frag", h, text, fn))
    seen, unique = set(), []
    for s in shaders:
        if (s[0], s[1]) not in seen:
            seen.add((s[0], s[1]))
            unique.append(s)
    return unique


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--uam", default=DEFAULT_UAM)
    ap.add_argument("--capture-dir", action="append", default=[],
                    help="Switch vs/ps_<hash>.hlsl inputs; repeatable")
    ap.add_argument("--incremental", action="store_true", help="skip compilation if sources and uam are unchanged")
    args = ap.parse_args()

    try:
        import_capture_dirs(args.capture_dir)
        signature = input_signature(args.uam)
        if args.incremental and os.path.isfile(HEADER) and os.path.isfile(STAMP):
            with open(STAMP, "r", encoding="utf-8") as f:
                record = json.load(f)
            if record.get("inputs") == signature and record.get("header_sha256") == hashlib.sha256(Path(HEADER).read_bytes()).hexdigest():
                print("deko3d shaders unchanged; reusing %s" % os.path.relpath(HEADER, ROOT))
                return 0
    except (TranslateError, OSError, ValueError) as exc:
        print("Shader prebuild failed: %s" % exc, file=sys.stderr)
        return 1

    os.makedirs(GLSL_DIR, exist_ok=True)
    entries, failed = [], 0
    shaders = collect()
    if not any(origin not in ('kEngFixedVS', 'kEngFixedPS') for _, _, _, origin in shaders):
        print('WARNING: only the 2 generic engine shaders are available; '
              'game-specific draws can still be skipped on Switch.', file=sys.stderr)
    with tempfile.TemporaryDirectory() as tmp:
        for stage, h, hlsl, origin in shaders:
            tag = "%s_%016x" % ("vs" if stage == "vert" else "ps", h)
            try:
                glsl = translate(hlsl, stage)
            except TranslateError as e:
                print("FAIL translate %s (%s): %s" % (tag, origin, e))
                failed += 1
                continue
            gpath = os.path.join(GLSL_DIR, tag + ".glsl")
            with open(gpath, "w", newline="\n") as f:
                f.write(glsl)
            out = os.path.join(tmp, tag + ".dksh")
            r = subprocess.run([args.uam, "-s", stage, "-o", out, gpath],
                               capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(out):
                print("FAIL uam %s (%s):\n%s%s" % (tag, origin, r.stdout, r.stderr))
                failed += 1
                continue
            with open(out, "rb") as f:
                entries.append((stage, h, f.read(), origin, tag))
            print("ok   %s  %-28s %6d bytes" % (tag, origin, len(entries[-1][2])))

    lines = [
        "/* Generated by scripts/gen-deko3d-shaders.py -- do not edit.",
        " * deko3d shader modules (.dksh) for the HLSL programs the draw engine",
        " * hands the Switch backend, keyed by the FNV-1a hash of that HLSL. */",
        "#ifndef RSX_DEKO3D_SHADERS_H",
        "#define RSX_DEKO3D_SHADERS_H",
        "",
        "typedef struct rsx_dk_shader_blob {",
        "    unsigned long long hash;",
        "    int stage;                       /* 0 vertex, 1 fragment */",
        "    const unsigned char* dksh;",
        "    unsigned int size;",
        "} rsx_dk_shader_blob;",
        "",
    ]
    for stage, h, blob, origin, tag in entries:
        lines.append("/* %s */" % origin)
        lines.append("static const unsigned char rsx_dk_%s[%d] __attribute__((aligned(256))) = {"
                     % (tag, len(blob)))
        for i in range(0, len(blob), 16):
            lines.append("    " + ",".join("0x%02x" % b for b in blob[i:i + 16]) + ",")
        lines.append("};")
    lines.append("")
    lines.append("static const rsx_dk_shader_blob rsx_dk_shader_table[] = {")
    for stage, h, blob, origin, tag in entries:
        lines.append("    { 0x%016xull, %d, rsx_dk_%s, %d }," %
                     (h, 0 if stage == "vert" else 1, tag, len(blob)))
    lines.append("};")
    lines.append("#define RSX_DK_SHADER_COUNT (sizeof rsx_dk_shader_table / sizeof rsx_dk_shader_table[0])")
    for stage, h, _text, origin in shaders:
        if origin == "kDkFxaaVS":
            lines.append("#define RSX_DK_FXAA_VS_HASH 0x%016xull" % h)
        elif origin == "kDkFxaaPS":
            lines.append("#define RSX_DK_FXAA_PS_HASH 0x%016xull" % h)
    lines.append("")
    lines.append("#endif")
    if failed:
        print("%d shaders compiled, %d failed; leaving previous header intact" %
              (len(entries), failed), file=sys.stderr)
        return 1
    # A partially regenerated shader table would silently drop game shaders.
    # Publish the new header and stamp only after ALL compilations succeed.
    header_bytes = ("\n".join(lines) + "\n").encode("utf-8")
    os.makedirs(os.path.dirname(HEADER), exist_ok=True)
    tmp_header = HEADER + ".tmp"
    with open(tmp_header, "wb") as f:
        f.write(header_bytes)
    os.replace(tmp_header, HEADER)
    os.makedirs(os.path.dirname(STAMP), exist_ok=True)
    tmp_stamp = STAMP + ".tmp"
    with open(tmp_stamp, "w", encoding="utf-8") as f:
        json.dump({"inputs": signature, "header_sha256": hashlib.sha256(header_bytes).hexdigest(),
                   "shaders": len(entries)}, f, indent=2)
    os.replace(tmp_stamp, STAMP)
    print("%d shaders -> %s (0 failed)" % (len(entries), os.path.relpath(HEADER, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
