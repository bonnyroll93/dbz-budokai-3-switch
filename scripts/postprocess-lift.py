#!/usr/bin/env python3
"""Apply the small port-specific edits and regenerate lifted include fragments."""
import argparse
import pathlib
import re


def replace_once(path: pathlib.Path, before: str, after: str) -> None:
    data = path.read_text(encoding="latin-1")
    if data.count(before) != 1:
        raise RuntimeError(f"expected one match in {path}: {before[:65]!r}")
    path.write_text(data.replace(before, after), encoding="latin-1", newline="\n")


def extract_function(source: str, address: str) -> str:
    match = re.search(rf"^void func_{address}\(ppu_context\* ctx\) \{{", source, re.M)
    if not match:
        raise RuntimeError(f"function {address} not found")
    next_function = re.search(r"^void func_[0-9A-F]{8}\(ppu_context\* ctx\) \{", source[match.end():], re.M)
    end = match.end() + next_function.start() if next_function else len(source)
    body = source[match.start():end].rstrip()
    return body.replace(f"void func_{address}", f"static void lifted_{address}", 1)


def make_include(source_path: pathlib.Path, addresses: tuple[str, ...], output: pathlib.Path) -> None:
    source = source_path.read_text(encoding="latin-1")
    first = re.search(r"^void func_[0-9A-F]{8}\(ppu_context\* ctx\) \{", source, re.M)
    if not first:
        raise RuntimeError(f"no lifted functions in {source_path}")
    preamble = source[:first.start()].rstrip()
    content = "/* Generated locally from the user's DBZ3 ELF. */\n" + preamble + "\n\n"
    content += "\n\n".join(extract_function(source, address) for address in addresses) + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(content, encoding="latin-1", newline="\n")


# ppu_lifter.py emits vmulouh/vmuleuh with host-endian uint16_t*/uint32_t*
# access, but ctx->vr holds raw big-endian bytes (every other lane op goes
# through ppu_vldu4/ppu_vstu4 or explicit byte assembly). The products were
# built from byte-swapped halfwords and stored as little-endian words, so the
# vector results were garbage; the DU effect slots carried pointers with low
# bits 1..3 because of it. Rewrite them with big-endian lane access.
_VMUL_RE = re.compile(
    r"\{ uint32_t\* d=\(uint32_t\*\)&ctx->vr\[(\d+)\]; "
    r"uint16_t\* a=\(uint16_t\*\)&ctx->vr\[(\d+)\]; "
    r"uint16_t\* b=\(uint16_t\*\)&ctx->vr\[(\d+)\]; "
    r"d\[0\]=\(uint32_t\)a\[([01])\]\*\(uint32_t\)b\[\4\]; "
    r"d\[1\]=\(uint32_t\)a\[[23]\]\*\(uint32_t\)b\[[23]\]; "
    r"d\[2\]=\(uint32_t\)a\[[45]\]\*\(uint32_t\)b\[[45]\]; "
    r"d\[3\]=\(uint32_t\)a\[[67]\]\*\(uint32_t\)b\[[67]\]; \}")


def _vmul_be(match: "re.Match[str]") -> str:
    vd, va, vb, lane = match.group(1), match.group(2), match.group(3), int(match.group(4))
    off = 2 * lane  # even halfword: bytes 4i..4i+1, odd: bytes 4i+2..4i+3
    return (f"{{ const uint8_t* a=(const uint8_t*)&ctx->vr[{va}]; "
            f"const uint8_t* b=(const uint8_t*)&ctx->vr[{vb}]; uint8_t r[16]; "
            f"for(int i=0;i<4;i++){{ uint32_t x=(uint32_t)((a[4*i+{off}]<<8)|a[4*i+{off + 1}]); "
            f"uint32_t y=(uint32_t)((b[4*i+{off}]<<8)|b[4*i+{off + 1}]); uint32_t p=x*y; "
            f"r[4*i]=(uint8_t)(p>>24); r[4*i+1]=(uint8_t)(p>>16); r[4*i+2]=(uint8_t)(p>>8); r[4*i+3]=(uint8_t)p; }} "
            f"memcpy(&ctx->vr[{vd}], r, 16); }}")


# The same host-endian defect in three more value-sensitive ops: vsubsws
# (int32 saturating subtract), vaddshs (int16 saturating add) and vmsumshm
# (int16 pair products summed into an int32 accumulator). Pure lane moves
# (vsplth, vmrg*) are unaffected and left alone.
_VSUBSWS_RE = re.compile(
    r"\{ int32_t\* d=\(int32_t\*\)&ctx->vr\[(\d+)\]; int32_t\* a=\(int32_t\*\)&ctx->vr\[(\d+)\]; "
    r"int32_t\* b=\(int32_t\*\)&ctx->vr\[(\d+)\]; "
    r"for\(int i=0;i<4;i\+\+\)\{int64_t r=\(int64_t\)a\[i\]-\(int64_t\)b\[i\]; "
    r"d\[i\]=\(int32_t\)\(r>0x7FFFFFFFLL\?0x7FFFFFFFLL:r<-0x80000000LL\?-0x80000000LL:r\);\} \}")
_VADDSHS_RE = re.compile(
    r"\{ int16_t\* d=\(int16_t\*\)&ctx->vr\[(\d+)\]; int16_t\* a=\(int16_t\*\)&ctx->vr\[(\d+)\]; "
    r"int16_t\* b=\(int16_t\*\)&ctx->vr\[(\d+)\]; "
    r"for\(int i=0;i<8;i\+\+\)\{int32_t r=\(int32_t\)a\[i\]\+\(int32_t\)b\[i\]; "
    r"d\[i\]=\(int16_t\)\(r>32767\?32767:r<-32768\?-32768:r\);\} \}")
_VMSUMSHM_RE = re.compile(
    r"\{ int16_t\* a=\(int16_t\*\)&ctx->vr\[(\d+)\]; int16_t\* b=\(int16_t\*\)&ctx->vr\[(\d+)\]; "
    r"int32_t\* c=\(int32_t\*\)&ctx->vr\[(\d+)\]; int32_t\* d=\(int32_t\*\)&ctx->vr\[(\d+)\]; "
    r"for\(int i=0;i<4;i\+\+\) d\[i\]=c\[i\]\+\(int32_t\)a\[i\*2\]\*\(int32_t\)b\[i\*2\]"
    r"\+\(int32_t\)a\[i\*2\+1\]\*\(int32_t\)b\[i\*2\+1\]; \}")


def _vsubsws_be(m: "re.Match[str]") -> str:
    vd, va, vb = m.group(1), m.group(2), m.group(3)
    return (f"{{ uint32_t a[4],b[4],d[4]; ppu_vldu4(&ctx->vr[{va}],a); ppu_vldu4(&ctx->vr[{vb}],b); "
            f"for(int i=0;i<4;i++){{int64_t r=(int64_t)(int32_t)a[i]-(int64_t)(int32_t)b[i]; "
            f"d[i]=(uint32_t)(int32_t)(r>0x7FFFFFFFLL?0x7FFFFFFFLL:r<-0x80000000LL?-0x80000000LL:r);}} "
            f"ppu_vstu4(&ctx->vr[{vd}],d); }}")


def _vaddshs_be(m: "re.Match[str]") -> str:
    vd, va, vb = m.group(1), m.group(2), m.group(3)
    return (f"{{ const uint8_t* a=(const uint8_t*)&ctx->vr[{va}]; const uint8_t* b=(const uint8_t*)&ctx->vr[{vb}]; "
            f"uint8_t r[16]; for(int i=0;i<8;i++){{ "
            f"int32_t x=(int16_t)(uint16_t)((a[2*i]<<8)|a[2*i+1]); int32_t y=(int16_t)(uint16_t)((b[2*i]<<8)|b[2*i+1]); "
            f"int32_t s=x+y; s=s>32767?32767:s<-32768?-32768:s; "
            f"r[2*i]=(uint8_t)((uint32_t)s>>8); r[2*i+1]=(uint8_t)s; }} memcpy(&ctx->vr[{vd}], r, 16); }}")


def _vmsumshm_be(m: "re.Match[str]") -> str:
    va, vb, vc, vd = m.group(1), m.group(2), m.group(3), m.group(4)
    return (f"{{ const uint8_t* a=(const uint8_t*)&ctx->vr[{va}]; const uint8_t* b=(const uint8_t*)&ctx->vr[{vb}]; "
            f"uint32_t c[4],d[4]; ppu_vldu4(&ctx->vr[{vc}],c); for(int i=0;i<4;i++){{ "
            f"int32_t a0=(int16_t)(uint16_t)((a[4*i]<<8)|a[4*i+1]), a1=(int16_t)(uint16_t)((a[4*i+2]<<8)|a[4*i+3]); "
            f"int32_t b0=(int16_t)(uint16_t)((b[4*i]<<8)|b[4*i+1]), b1=(int16_t)(uint16_t)((b[4*i+2]<<8)|b[4*i+3]); "
            f"d[i]=c[i]+(uint32_t)(a0*b0)+(uint32_t)(a1*b1); }} ppu_vstu4(&ctx->vr[{vd}],d); }}")


_BE_FIXES = (
    (_VMUL_RE, _vmul_be),
    (_VSUBSWS_RE, _vsubsws_be),
    (_VADDSHS_RE, _vaddshs_be),
    (_VMSUMSHM_RE, _vmsumshm_be),
)


def fix_vmul_halfwords(directory: pathlib.Path) -> int:
    total = 0
    for path in sorted(directory.glob("*.cpp")) + sorted(directory.glob("*.inc")):
        data = path.read_text(encoding="latin-1")
        changed = 0
        for regex, repl in _BE_FIXES:
            data, n = regex.subn(repl, data)
            changed += n
        if changed:
            path.write_text(data, encoding="latin-1", newline="\n")
            total += changed
    return total


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("recompiled", type=pathlib.Path)
    parser.add_argument("includes", type=pathlib.Path)
    args = parser.parse_args()
    print(f"[postprocess] big-endian vmulouh/vmuleuh sites fixed: {fix_vmul_halfwords(args.recompiled)}")
    chunk0 = args.recompiled / "ppu_recomp_000.cpp"
    chunk3 = args.recompiled / "ppu_recomp_003.cpp"
    chunk4 = args.recompiled / "ppu_recomp_004.cpp"
    replace_once(chunk0,
                 "        ctx->gpr[29] = ppc_rldicl(ctx->gpr[29], 0, 32);\n"
                 "        ctx->gpr[6] = vm_read32(ctx->gpr[4] + 0x0);",
                 "        ctx->gpr[29] = ppc_rldicl(ctx->gpr[29], 0, 32);\n"
                 "        if ((uint32_t)ctx->gpr[29] < 0x10000u ||\n"
                 "            (uint32_t)ctx->gpr[29] >= 0x80000000u ||\n"
                 "            ((uint32_t)ctx->gpr[29] & 3u)) goto loc_00031F08;\n"
                 "        ctx->gpr[6] = vm_read32(ctx->gpr[4] + 0x0);")
    replace_once(chunk0,
                 "        ctx->gpr[30] = vm_read32(ctx->gpr[29] + 0x14);\n"
                 "        ctx->ctr = (uint32_t)ctx->gpr[6];",
                 "        ctx->gpr[30] = vm_read32(ctx->gpr[29] + 0x14);\n"
                 "        if ((uint32_t)ctx->gpr[30] & 3u) {\n"
                 "            static unsigned misaligned_reports = 0;\n"
                 "            if (misaligned_reports++ < 8u)\n"
                 "                fprintf(stderr, \"[dbz3] misaligned object ptr=0x%08X owner=0x%08X lr=0x%08X\\n\",\n"
                 "                        (uint32_t)ctx->gpr[30], (uint32_t)ctx->gpr[29], (uint32_t)ctx->lr);\n"
                 "            goto loc_00031F08;\n"
                 "        }\n"
                 "        if ((uint32_t)ctx->gpr[30] < 0x10000u ||\n"
                 "            (uint32_t)ctx->gpr[30] >= 0x80000000u) goto loc_00031F08;\n"
                 "        ctx->ctr = (uint32_t)ctx->gpr[6];")
    replace_once(chunk0, "void func_00053C24(ppu_context* ctx) {",
                 "void func_00053C24(ppu_context* ctx) {\n"
                 "        { static int _n=0; if (_n++ < 12) { uint32_t b=(uint32_t)ctx->gpr[4]; uint32_t ix=(uint32_t)ctx->gpr[5]; uint32_t e=b+ix*36+0xC;\n"
                 "          fprintf(stderr, \"[T23-53C24] r3=0x%08X blob=0x%08X idx=%u entry@0x%08X: %08X %08X %08X | blob: %08X %08X %08X %08X lr=0x%08X tid=%u\\n\",\n"
                 "            (uint32_t)ctx->gpr[3], b, ix, e, (uint32_t)vm_read32(e), (uint32_t)vm_read32(e+4), (uint32_t)vm_read32(e+8), (uint32_t)vm_read32(b), (uint32_t)vm_read32(b+4), (uint32_t)vm_read32(b+8), (uint32_t)vm_read32(b+12), (uint32_t)ctx->lr, (unsigned)ctx->thread_id); } }")
    replace_once(chunk4, "void func_002909C0(ppu_context* ctx) {",
                 "extern \"C\" int g_t23_postfx_nowait;\n"
                 "void func_002909C0(ppu_context* ctx) {\n"
                 "        if (g_t23_postfx_nowait) return;")
    # DU effects list: Namek has produced reads at 0x2F41A00C, 0x8000000C,
    # 0x39DDA00C and 0x3D3155D8. Those regions are not committed on Switch.
    # Skip only a bad effects node and retain scene/task progression. The
    # regions mirror main/RSX, raw SPU, the sys_memory allocation window,
    # local RSX and stack VA. Namek allocates 0x41100000..0x41300000 and
    # 0x41300000..0x4A800000, so rejecting 0x4... nodes hides its scene.
    replace_once(chunk3, "void func_001D1EC0(ppu_context* ctx) {",
                 "extern \"C\" uint32_t g_sys_mem_bump_ptr;\n"
                 "extern \"C\" uint32_t g_dbz3_du_rejects[4];\n"
                 "static inline bool dbz3_du_effect_node_valid(uint32_t p) {\n"
                 "    return !(p & 3u) && ((p >= 0x10000u && p <= 0x1FFFFFCCu) ||\n"
                 "           (p >= 0x30000000u && p <= 0x301FFFCCu) ||\n"
                 "           (p >= 0x40000000u && g_sys_mem_bump_ptr >= 0x40000034u &&\n"
                 "            p <= g_sys_mem_bump_ptr - 0x34u) ||\n"
                 "           (p >= 0xC0000000u && p <= 0xDFFFFFCCu));\n"
                 "}\n"
                 "static void dbz3_du_dump_table(ppu_context* ctx) {\n"
                 "    static unsigned dumped = 0;\n"
                 "    if (dumped++ >= 3u) return;\n"
                 "    uint32_t base = (uint32_t)ctx->gpr[30] - 0x20u * (uint32_t)ctx->gpr[23];\n"
                 "    fprintf(stderr, \"[du-table] base=0x%08X count=%d idx=%d\\n\", base, (int)ctx->gpr[31], (int)ctx->gpr[23]);\n"
                 "    for (int i = 0; i < (int)ctx->gpr[31] && i < 96; i++) {\n"
                 "        uint32_t e = base + 0x20u * (uint32_t)i;\n"
                 "        fprintf(stderr, \"[du-table] #%d e=0x%08X: %08X %08X %08X %08X %08X %08X %08X %08X\\n\", i, e,\n"
                 "                (uint32_t)vm_read32(e), (uint32_t)vm_read32(e+4), (uint32_t)vm_read32(e+8), (uint32_t)vm_read32(e+12),\n"
                 "                (uint32_t)vm_read32(e+16), (uint32_t)vm_read32(e+20), (uint32_t)vm_read32(e+24), (uint32_t)vm_read32(e+28));\n"
                 "    }\n"
                 "}\n"
                 "void func_001D1EC0(ppu_context* ctx) {")
    replace_once(chunk3,
                 "        ctx->gpr[28] = vm_read32(ctx->gpr[4] + 0x20);\n"
                 "        ctx->lr = 0x001D23E0; func_0001D954(ctx);",
                 "        ctx->gpr[28] = vm_read32(ctx->gpr[4] + 0x20);\n"
                 "        if (ctx->gpr[28] && !dbz3_du_effect_node_valid((uint32_t)ctx->gpr[28])) {\n"
                 "            static unsigned bad_nodes = 0; g_dbz3_du_rejects[0]++; dbz3_du_dump_table(ctx);\n"
                 "            if (bad_nodes++ < 8u) fprintf(stderr, \"[du-effect] invalid first node=0x%08X owner=0x%08X\\n\",\n"
                 "                                         (uint32_t)ctx->gpr[28], (uint32_t)ctx->gpr[4]);\n"
                 "            goto loc_001D24F4;\n"
                 "        }\n"
                 "        ctx->lr = 0x001D23E0; func_0001D954(ctx);")
    # A valid effects node can still contain an offset that computes an
    # unmapped address. run39 faults at 0x3D3155D8 in this exact read.
    replace_once(chunk3,
                 "        ctx->gpr[3] = vm_read32(ctx->gpr[5] + 0x0);\n"
                 "        ctx->lr = 0x001D242C;",
                 "        if (!dbz3_du_effect_node_valid((uint32_t)ctx->gpr[5])) {\n"
                 "            static unsigned bad_derived = 0; g_dbz3_du_rejects[1]++;\n"
                 "            if (bad_derived++ < 8u) fprintf(stderr, \"[du-effect] invalid derived=0x%08X node=0x%08X index=0x%08X\\n\",\n"
                 "                                              (uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[28], (uint32_t)ctx->gpr[6]);\n"
                 "            goto loc_001D24E8;\n"
                 "        }\n"
                 "        ctx->gpr[3] = vm_read32(ctx->gpr[5] + 0x0);\n"
                 "        ctx->lr = 0x001D242C;")
    replace_once(chunk3,
                 "loc_001D24E8:\n"
                 "        ctx->gpr[28] = vm_read32(ctx->gpr[28] + 0x0);\n"
                 "        { int64_t a = (int32_t)ctx->gpr[28];",
                 "loc_001D24E8:\n"
                 "        ctx->gpr[28] = vm_read32(ctx->gpr[28] + 0x0);\n"
                 "        if (ctx->gpr[28] && !dbz3_du_effect_node_valid((uint32_t)ctx->gpr[28])) {\n"
                 "            static unsigned bad_links = 0; g_dbz3_du_rejects[2]++;\n"
                 "            if (bad_links++ < 8u) fprintf(stderr, \"[du-effect] invalid next node=0x%08X\\n\",\n"
                 "                                         (uint32_t)ctx->gpr[28]);\n"
                 "            goto loc_001D24F4;\n"
                 "        }\n"
                 "        { int64_t a = (int32_t)ctx->gpr[28];")
    # A valid effects node can point at a malformed child table entry. On
    # Switch, liftfast reads guest addresses directly when they fall in its
    # broad fast range, so a freed 0x3... address can abort before vm_oob sees
    # it. run48 hits this path in func_00021C6C with derived address 0x3F4952B2.
    # Reject entries outside the guest's actually allocated regions and advance
    # the current iterator, preserving the rest of the DU effect list.
    replace_once(chunk0, "void func_00021C6C(ppu_context* ctx) {",
                 "extern \"C\" uint32_t g_sys_mem_bump_ptr;\n"
                 "extern \"C\" uint32_t g_dbz3_du_rejects[4];\n"
                 "static inline bool dbz3_du_effect_lookup_valid(uint32_t p) {\n"
                 "    return !(p & 3u) && ((p >= 0x10000u && p <= 0x1FFFFFCCu) ||\n"
                 "           (p >= 0x30000000u && p <= 0x301FFFCCu) ||\n"
                 "           (p >= 0x40000000u && g_sys_mem_bump_ptr >= 0x40000034u &&\n"
                 "            p <= g_sys_mem_bump_ptr - 0x34u) ||\n"
                 "           (p >= 0xC0000000u && p <= 0xDFFFFFCCu));\n"
                 "}\n"
                 "void func_00021C6C(ppu_context* ctx) {")
    replace_once(chunk0,
                 "        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n"
                 "        ctx->gpr[25] = vm_read32(ctx->gpr[3] + 0x0);",
                 "        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n"
                 "        if (!dbz3_du_effect_lookup_valid((uint32_t)ctx->gpr[3])) {\n"
                 "            static unsigned bad_lookup = 0; g_dbz3_du_rejects[3]++;\n"
                 "            if (bad_lookup++ < 8u) fprintf(stderr, \"[du-effect] invalid lookup=0x%08X container=0x%08X index=%u\\n\",\n"
                 "                                              (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[27], (uint32_t)ctx->gpr[24]);\n"
                 "            goto loc_00021F4C;\n"
                 "        }\n"
                 "        ctx->gpr[25] = vm_read32(ctx->gpr[3] + 0x0);")
    make_include(args.recompiled / "ppu_recomp_003.cpp", ("00216DB4",),
                 args.includes / "dbz3_crierr_lifted.inc")
    make_include(chunk0, ("0001D294", "0001D4D8"),
                 args.includes / "dbz3_heap_trace_lifted.inc")


if __name__ == "__main__":
    main()
