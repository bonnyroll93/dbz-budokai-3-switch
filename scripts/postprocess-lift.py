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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("recompiled", type=pathlib.Path)
    parser.add_argument("includes", type=pathlib.Path)
    args = parser.parse_args()
    chunk0 = args.recompiled / "ppu_recomp_000.cpp"
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
    make_include(args.recompiled / "ppu_recomp_003.cpp", ("00216DB4",),
                 args.includes / "dbz3_crierr_lifted.inc")
    make_include(chunk0, ("0001D294", "0001D4D8"),
                 args.includes / "dbz3_heap_trace_lifted.inc")


if __name__ == "__main__":
    main()
