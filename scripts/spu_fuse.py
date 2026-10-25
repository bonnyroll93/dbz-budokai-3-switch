#!/usr/bin/env python3
"""T6.A1: build-time fusion of lifted SPU functions that form a loop.

Why: the lifter cuts the SPU program at every branch target into small C
functions; a branch to another function is a "trampoline" transfer -- the
function returns to the dispatcher (SPU_DRAIN), which runs its per-transfer
checks and calls the next one. In the DBZ3 audio SPU (bnusCore) the DSP outer
loops are cycles of such functions (e.g. 0xAD30 -> 0xAD98 -> 0xAEC4 -> 0xAD30):
every iteration paid three dispatches and, after spu_regcache.py, reloaded and
stored back dozens of registers at each function boundary.

Rewrite (on a build copy; run before spu_regcache.py):
  - find the strongly connected components of the trampoline graph;
  - fuse each component that is pure computation -- no line calls anything but
    the local-store accessors (so no channel op, stop, call or SPU_RET that
    must go through the dispatcher), at most MAX_MEMBERS functions -- into one
    function `<image>_spu_func_FEED<entry>`, where a transfer to a member
    becomes `goto fz_<member>;` and a transfer out stays a trampoline;
  - each member becomes `{ ctx->pc = <member>; fused(ctx); }`, so every entry
    point still works; the fused body starts with a switch on ctx->pc.
The SPU instructions executed are the same; only the dispatcher's
per-transfer checks (interrupt poll, task-launch check, pc history) are not run
between members of a fused loop -- they run again when the loop exits.

usage: spu_fuse.py INPUT OUTPUT [--max-members N] [--only ADDR,ADDR,...]
"""
import re
import sys

FUNC_RE = re.compile(r'^void (\w+_spu_func_)([0-9A-F]{8})\(spu_context\* ctx\) \{$')
TRAMP_RE = re.compile(r'\{ ctx->pc = 0x([0-9A-Fa-f]+); g_spu_trampoline_fn = (\w+_spu_func_)([0-9A-F]{8}); return; \}')
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(\s*ctx\s*[,)]')
SAFE_CALLS = {
    'spu_ls_read128', 'spu_ls_write128',
    'spu_ls_read8', 'spu_ls_read16', 'spu_ls_read32', 'spu_ls_read64',
    'spu_ls_write8', 'spu_ls_write16', 'spu_ls_write32', 'spu_ls_write64',
}


def has_call(body):
    for line in body:
        for m in CALL_RE.finditer(line):
            if m.group(1) not in SAFE_CALLS:
                return True
        if 'ctx->pc' in TRAMP_RE.sub('', line):     # pc used other than in a transfer
            return True
    return False


def sccs(nodes, edges):
    sys.setrecursionlimit(1000000)
    index, low, stack, on, out, counter = {}, {}, [], set(), [], [0]

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in edges.get(v, ()):
            if w not in nodes:
                continue
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on.discard(w)
                comp.append(w)
                if w == v:
                    break
            out.append(sorted(comp))

    for v in nodes:
        if v not in index:
            visit(v)
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    max_members = 64
    only = None
    args = sys.argv[3:]
    for i, a in enumerate(args):
        if a == '--max-members':
            max_members = int(args[i + 1])
        if a == '--only':
            only = {int(x, 16) for x in args[i + 1].split(',') if x}
    lines = open(src, encoding='utf-8', errors='surrogateescape').read().split('\n')

    # Functions: addr -> (start line, end line); body = lines[start+1:end].
    funcs, prefix, i = {}, None, 0
    while i < len(lines):
        m = FUNC_RE.match(lines[i])
        if m:
            j = i + 1
            while j < len(lines) and lines[j] != '}':
                j += 1
            funcs[int(m.group(2), 16)] = (i, j)
            prefix = m.group(1)
            i = j + 1
            continue
        i += 1
    if not funcs:
        open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write('\n'.join(lines))
        print(f'spu_fuse: no lifted functions in {src}')
        return

    edges, pure = {}, set()
    for a, (s, e) in funcs.items():
        body = lines[s + 1:e]
        edges[a] = {int(m.group(3), 16) for l in body for m in TRAMP_RE.finditer(l)
                    if m.group(2) == prefix}
        if not has_call(body):
            pure.add(a)

    groups = []
    for comp in sccs(set(funcs), edges):
        if len(comp) < 2 or len(comp) > max_members:
            continue
        if not all(a in pure for a in comp):
            continue
        if only is not None and not (set(comp) & only):
            continue
        groups.append(comp)

    member_of = {}
    for g in groups:
        for a in g:
            member_of[a] = g

    out = []
    i = 0
    emitted = set()
    while i < len(lines):
        m = FUNC_RE.match(lines[i])
        if m and int(m.group(2), 16) in member_of:
            a = int(m.group(2), 16)
            g = member_of[a]
            fused = f'{prefix}FEED{g[0]:08X}'
            s, e = funcs[a]
            if g[0] not in emitted:
                emitted.add(g[0])
                # The fused function, at the position of the group's first member
                # met in the file (all members are defined in this file).
                out.append(f'void {fused}(spu_context* ctx);')
                out.append(f'void {fused}(spu_context* ctx) {{')
                out.append('        switch ((uint32_t)ctx->pc) {')
                for b in g:
                    out.append(f'        case 0x{b:X}: goto fz_{b:08X};')
                out.append('        default: return;')
                out.append('        }')
                for b in g:
                    bs, be = funcs[b]
                    out.append(f'fz_{b:08X}:')
                    for line in lines[bs + 1:be]:
                        line = TRAMP_RE.sub(
                            lambda t: (f'{{ goto fz_{int(t.group(3), 16):08X}; }}'
                                       if t.group(2) == prefix and int(t.group(3), 16) in g
                                       else t.group(0)), line)
                        out.append(line)
                    out.append('        return;   /* body end: as the unfused function */')
                out.append('}')
                out.append('')
            out.append(lines[i])
            out.append(f'        ctx->pc = 0x{a:X}; {fused}(ctx);')
            out.append('}')
            i = e + 1
            continue
        out.append(lines[i])
        i += 1

    open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write('\n'.join(out))
    sizes = sorted(len(g) for g in groups)
    print(f'spu_fuse: {len(groups)} loop group(s) fused, {sum(sizes)} functions '
          f'(sizes {sizes}) -> {dst}')


if __name__ == '__main__':
    main()
