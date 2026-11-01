#!/usr/bin/env python3
"""T41: build-time rewrite of a lifted PPU source so each function keeps the
PPU registers it touches in local variables instead of the context.

Why: the lifted code reads and writes ctx->gpr[N], ctx->fpr[N], ctx->cr,
ctx->xer and ctx->ctr for every PPU instruction. Guest memory stores go through
a byte pointer, which may alias anything (-fno-strict-aliasing), so after each
one GCC reloads every register from the context and stores every result back:
in a DBZ3 duel the main PPU thread spends ~27 ms per pass on lifted code that
is mostly these loads and stores.

Rewrite, per function (same scheme as spu_regcache.py; the checked-in lift is
untouched, this writes a build copy):
  - every cached register the function uses becomes a local (pg_N, pf_N,
    pc_cr, pc_xer, pc_ctr), loaded from the context on entry;
  - before each exit (`return;`) the registers the function writes are stored
    back;
  - a line that passes ctx to anything (another lifted function, an indirect
    call, a syscall, a trampoline drain) is left in its original ctx-> form,
    with a store-back before it and a reload of every used register after it
    -- so it behaves exactly as before.
ctx->lr and ctx->vr stay in the context (lr is set on every call line; the
vector code addresses ctx->vr through the NEON macros).

usage: ppu_regcache.py INPUT OUTPUT [--all]
  --all also rewrites straight-line functions (default: functions with a loop).
"""
import re
import sys

FUNC_RE = re.compile(r'^void func_([0-9A-Fa-f]{8})\(ppu_context\* ctx\) \{$')
# Cached fields: regex, local name, C type, context lvalue.
FIELDS = [
    (re.compile(r'ctx->gpr\[(\d+)\]'), 'pg_{}', 'uint64_t', 'ctx->gpr[{}]'),
    (re.compile(r'ctx->fpr\[(\d+)\]'), 'pf_{}', 'double', 'ctx->fpr[{}]'),
    (re.compile(r'ctx->(cr)\b'), 'pc_{}', 'uint32_t', 'ctx->{}'),
    (re.compile(r'ctx->(xer)\b'), 'pc_{}', 'uint32_t', 'ctx->{}'),
    (re.compile(r'ctx->(ctr)\b'), 'pc_{}', 'uint64_t', 'ctx->{}'),
]
ASSIGN_TAIL = r'\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)'
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(\s*ctx\s*[,)]')
# Takes ctx but never touches the register file (ppu_instrument.py's stamp).
SAFE_CALLS = {'_pfs'}
BARE_CTX_RE = re.compile(r'\bctx\b(?!\s*->)')
EXIT_RE = re.compile(r'\breturn\s*;')
LABEL_RE = re.compile(r'^\s*(loc_[0-9A-Fa-f]+):')
GOTO_RE = re.compile(r'\bgoto\s+(loc_[0-9A-Fa-f]+)\s*;')
ALL_FUNCS = False


def is_sync_line(line):
    """Anything that hands ctx to other code must see the context current."""
    for m in CALL_RE.finditer(line):
        if m.group(1) not in SAFE_CALLS:
            return True
    stripped = CALL_RE.sub('', line)
    return bool(BARE_CTX_RE.search(stripped))


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


def regs_in(line, pattern):
    return set(pattern.findall(line))


def transform_function(header, body):
    # A bare ctx outside a call (stored, compared, cast...) is never expected
    # in the lifts; leave such a function alone rather than guess.
    used = []          # (field index, key) in first-seen order per field
    written = set()
    seen = set()
    for line in body:
        sync = is_sync_line(line)
        for fi, (pat, _, _, _) in enumerate(FIELDS):
            for key in pat.findall(line):
                if (fi, key) not in seen:
                    seen.add((fi, key))
                    used.append((fi, key))
            if sync:
                continue
            # Writes: plain or compound assignment, or the address taken
            # (memcpy into an fpr). Writes on sync lines already go to ctx.
            lhs = re.compile(pat.pattern + ASSIGN_TAIL)
            for key in lhs.findall(line):
                written.add((fi, key))
            addr = re.compile(r'&\s*' + pat.pattern)
            for key in addr.findall(line):
                written.add((fi, key))
    if not used or (not has_loop(body) and not ALL_FUNCS):
        return [header] + body, False

    def local(fi, key):
        return FIELDS[fi][1].format(key)

    def ctxv(fi, key):
        return FIELDS[fi][3].format(key)

    used.sort()
    wr = sorted(written)
    store = ' '.join(f'{ctxv(fi, k)} = {local(fi, k)};' for fi, k in wr)
    load = ' '.join(f'{local(fi, k)} = {ctxv(fi, k)};' for fi, k in used)

    def rename(line):
        for fi, (pat, name, _, _) in enumerate(FIELDS):
            line = pat.sub(lambda m, n=name: n.format(m.group(1)), line)
        return line

    out = [header]
    by_type = {}
    for fi, k in used:
        by_type.setdefault(FIELDS[fi][2], []).append(f'{local(fi, k)} = {ctxv(fi, k)}')
    for ctype, decls in by_type.items():
        out.append(f'    {ctype} ' + ', '.join(decls) + ';')
    for line in body:
        if is_sync_line(line):
            if store:
                out.append('    ' + store)
            out.append(line)
            out.append('    ' + load)
            continue
        new = rename(line)
        if store:
            new = EXIT_RE.sub(lambda m: '{ ' + store + ' ' + m.group(0) + ' }', new)
        out.append(new)
    # Falling off the end of the body (no explicit exit): store back.
    if store:
        out.append('    ' + store)
    return out, True


def main():
    global ALL_FUNCS
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ALL_FUNCS = '--all' in sys.argv[3:]
    src, dst = sys.argv[1], sys.argv[2]
    lines = open(src, encoding='utf-8', errors='surrogateescape').read().split('\n')
    out, i, done, total = [], 0, 0, 0
    while i < len(lines):
        line = lines[i]
        if FUNC_RE.match(line):
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
    print(f'ppu_regcache: {done}/{total} functions rewritten -> {dst}')


if __name__ == '__main__':
    main()
