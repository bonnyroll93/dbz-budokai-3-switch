#!/usr/bin/env python3
"""T38: build-time copy of a lifted PPU source in which every function records
itself as the main PPU thread's "current function" while it runs.

On entry a function stores its guest address in g_ppu_fn_stamp (only when it
runs on the main guest thread) and restores the caller's on exit, so the
sampler in runtime/platform/switch_ppu_profile.c sees the innermost lifted
function -- not the last call site, which is all ctx->lr can tell and which
charged every callee's time to its caller. Diagnostic builds only: the checked-
in lift is untouched.

usage: ppu_instrument.py INPUT OUTPUT
"""
import re
import sys

FUNC_RE = re.compile(r'^void func_([0-9A-Fa-f]{8})\(ppu_context\* ctx\) \{$')

PRELUDE = r'''
/* ppu_instrument.py: main-thread current-function stamp (T38). */
extern "C" ppu_context* volatile g_ppu_main_ctx;
extern "C" volatile uint32_t g_ppu_fn_stamp;
struct PpuFnScope {
    uint32_t prev;
    bool on;
    PpuFnScope(ppu_context* c, uint32_t a) : prev(0), on(c == g_ppu_main_ctx)
    { if (on) { prev = g_ppu_fn_stamp; g_ppu_fn_stamp = a; } }
    ~PpuFnScope() { if (on) g_ppu_fn_stamp = prev; }
};
'''


def main():
    src, dst = sys.argv[1], sys.argv[2]
    lines = open(src, encoding='utf-8', errors='surrogateescape').read().split('\n')
    out, n, prelude_done = [], 0, False
    for line in lines:
        m = FUNC_RE.match(line)
        if m:
            if not prelude_done:
                out.append(PRELUDE)
                prelude_done = True
            out.append(line)
            out.append(f'    PpuFnScope _pfs(ctx, 0x{m.group(1)}u);')
            n += 1
            continue
        out.append(line)
    open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write('\n'.join(out))
    print(f'ppu_instrument: {n} functions -> {dst}')


if __name__ == '__main__':
    main()
