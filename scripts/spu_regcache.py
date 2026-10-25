#!/usr/bin/env python3
"""T30: build-time rewrite of a lifted SPU source so each function keeps the SPU
registers it touches in local variables instead of ctx->gpr[].

Why: the lifted code reads and writes ctx->gpr[N] for every SPU instruction.
Local-store stores go through a uint8_t pointer, which may alias anything, so
after each one GCC reloads every register from memory and can never keep one in
a NEON register across a loop. In the DBZ3 audio SPU (bnusCore) a third of the
host instructions of the hottest DSP loops were those reloads and stores.

Rewrite, per function (the checked-in lift is left untouched; this writes a
build copy):
  - every ctx->gpr[N] the function uses becomes a local `sg_N`, loaded from the
    context on entry;
  - before each exit (`return;`, SPU_RET) the registers the function writes are
    stored back;
  - a line that calls anything that may read or write the register file
    (another lifted function, channel ops, stop/halt, drain, interpreter...)
    is left in its original ctx->gpr[] form, with a store-back before it and a
    reload of every used register after it -- so it behaves exactly as before.
Only the local-store accessors are treated as register-neutral: with
SPU_LS_NODIAG (set for these sources) they touch ctx->ls only.

usage: spu_regcache.py INPUT OUTPUT HEADER_DIR
"""
import re
import sys

FUNC_RE = re.compile(r'^void (\w+_spu_func_[0-9A-Fa-f]+)\(spu_context\* ctx\) \{$')
GPR_RE = re.compile(r'ctx->gpr\[(\d+)\]')
WRITE_RE = re.compile(r'ctx->gpr\[(\d+)\]\s*=(?!=)')
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(\s*ctx\s*[,)]')
# Calls that take ctx but never touch ctx->gpr (with SPU_LS_NODIAG).
SAFE_CALLS = {
    'spu_ls_read128', 'spu_ls_write128',
    'spu_ls_read8', 'spu_ls_read16', 'spu_ls_read32', 'spu_ls_read64',
    'spu_ls_write8', 'spu_ls_write16', 'spu_ls_write32', 'spu_ls_write64',
}
EXIT_RE = re.compile(r'\breturn\s*;|\bSPU_RET\s*\(\s*ctx\s*\)\s*;')


def is_call_line(line):
    for m in CALL_RE.finditer(line):
        if m.group(1) not in SAFE_CALLS:
            return True
    return False


ALL_FUNCS = False
LABEL_RE = re.compile(r'^\s*(loc_[0-9A-Fa-f]+):')
GOTO_RE = re.compile(r'\bgoto\s+(loc_[0-9A-Fa-f]+)\s*;')


def has_loop(body):
    """A backward goto: a label defined above a goto to it."""
    seen = set()
    for line in body:
        m = LABEL_RE.match(line)
        if m:
            seen.add(m.group(1))
        for g in GOTO_RE.findall(line):
            if g in seen:
                return True
    return False


def transform_function(header, body):
    used = sorted({int(n) for l in body for n in GPR_RE.findall(l)})
    # Only functions with a loop: the register file in locals pays off on every
    # iteration, while a straight-line function -- entered over a million times
    # a second -- would only pay the extra loads and stores at entry and exit
    # (measured: the hot loops shrank 17-35%, straight-line bodies grew).
    if not used or (not has_loop(body) and not ALL_FUNCS):
        return [header] + body, False
    # Store back only the registers the rewritten lines assign. Every register
    # write in the lifts has the one form `ctx->gpr[N] = ...` (checked over all
    # four DBZ3 images: 52293 of them, no compound or partial stores), so this
    # set is exact; writes made by call lines already went to ctx. A function is
    # entered ~1.5M times a second, so its exits are worth keeping small.
    written = sorted({int(n) for l in body if not is_call_line(l)
                      for n in WRITE_RE.findall(l)})
    # Locals are NEON vectors, not u128: GCC splits a u128 local (a union with
    # a uint64_t[2] member) into two 64-bit integer registers and round-trips
    # it through the stack for every vector op. A uint32x4_t stays in a Q
    # register. Helpers keep taking and returning u128; the SV/SU conversions
    # are 16-byte copies the compiler folds away.
    store = ' '.join(f'vst1q_u32(ctx->gpr[{n}]._u32, sv_{n});' for n in written)
    load = ' '.join(f'sv_{n} = vld1q_u32(ctx->gpr[{n}]._u32);' for n in used)
    out = [header]
    out.append('    uint32x4_t ' + ', '.join(f'sv_{n} = vld1q_u32(ctx->gpr[{n}]._u32)'
                                            for n in used) + ';')
    for line in body:
        if is_call_line(line):
            # Original memory form: registers in ctx are current before it,
            # and anything it changed is picked up after it.
            if store:
                out.append('    ' + store)
            out.append(line)
            out.append('    ' + load)
            continue
        new = vectorize_line(line)
        if store:
            new = EXIT_RE.sub(lambda m: '{ ' + store + ' ' + m.group(0) + ' }', new)
        out.append(new)
    # Falling off the end of the body (no explicit exit): store back.
    if store:
        out.append('    ' + store)
    return out, True


FIELD_U32_RE = re.compile(r'ctx->gpr\[(\d+)\]\._u32\[0\]')
FIELD_U16_RE = re.compile(r'ctx->gpr\[(\d+)\]\._u16\[0\]')
ASSIGN_RE = re.compile(r'ctx->gpr\[(\d+)\]\s*=(?!=)\s*')


def rvalues(text):
    return GPR_RE.sub(lambda m: f'SU(sv_{m.group(1)})', text)


def vectorize_line(line):
    # Field reads first (same bytes as the union member: lane 0 / its low half).
    line = FIELD_U32_RE.sub(lambda m: f'vgetq_lane_u32(sv_{m.group(1)}, 0)', line)
    line = FIELD_U16_RE.sub(
        lambda m: f'vgetq_lane_u16(vreinterpretq_u16_u32(sv_{m.group(1)}), 0)', line)
    out, pos = [], 0
    for m in ASSIGN_RE.finditer(line):
        if m.start() < pos:
            continue
        # The right-hand side runs to the ';' at bracket depth 0.
        depth, j = 0, m.end()
        while j < len(line):
            ch = line[j]
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            elif ch == ';' and depth == 0:
                break
            j += 1
        out.append(rvalues(line[pos:m.start()]))
        out.append(f'sv_{m.group(1)} = SV({rvalues(line[m.end():j])})')
        pos = j
    out.append(rvalues(line[pos:]))
    return ''.join(out)


PRELUDE = """
/* spu_regcache.py: u128 <-> NEON register conversions for vector locals. */
#include <arm_neon.h>
#include <string.h>
static inline __attribute__((always_inline)) uint32x4_t SV(u128 x)
{ uint32x4_t v; memcpy(&v, &x, 16); return v; }
static inline __attribute__((always_inline)) u128 SU(uint32x4_t v)
{ u128 x; memcpy(&x, &v, 16); return x; }
"""


def main():
    global ALL_FUNCS
    ALL_FUNCS = '--all' in sys.argv[4:]   # also straight-line functions
    src, dst, header_dir = sys.argv[1], sys.argv[2], sys.argv[3].replace('\\', '/')
    lines = open(src, encoding='utf-8', errors='surrogateescape').read().split('\n')
    out, i, done, total = [], 0, 0, 0
    while i < len(lines):
        line = lines[i]
        if re.match(r'^#include ".*spu_recomp\.h"', line):
            out.append(f'#include "{header_dir}/spu_recomp.h"')
            i += 1
            continue
        if FUNC_RE.match(line):
            if total == 0:
                out.append(PRELUDE)
            total += 1
            j = i + 1
            while j < len(lines) and lines[j] != '}':
                j += 1
            body = lines[i + 1:j]
            new, changed = transform_function(line, body)
            done += changed
            out.extend(new)
            out.append('}')
            i = j + 1
            continue
        out.append(line)
        i += 1
    open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write('\n'.join(out))
    print(f'spu_regcache: {done}/{total} functions rewritten -> {dst}')


if __name__ == '__main__':
    main()
