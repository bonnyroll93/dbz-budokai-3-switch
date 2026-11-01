#!/usr/bin/env python3
"""T38: build-time copy of a lifted PPU source in which every function records
itself as the main PPU thread's "current function" while it runs.

On entry a function stores its guest address in g_ppu_fn_stamp (only when it
runs on the main guest thread) and restores the caller's on exit, so the
sampler in runtime/platform/switch_ppu_profile.c sees the innermost lifted
function -- not the last call site, which is all ctx->lr can tell and which
charged every callee's time to its caller. Diagnostic builds only: the checked-
in lift is untouched.

usage: ppu_instrument.py INPUT OUTPUT [--wtrack HEX[,HEX...]]
  --wtrack: also mark those functions for the store tracker (dbz3_wtrack.cpp).
"""
import re
import sys

FUNC_RE = re.compile(r'^void func_([0-9A-Fa-f]{8})\(ppu_context\* ctx\) \{$')

PRELUDE = r'''
/* ppu_instrument.py: main-thread current-function stamp (T38) and the chain of
 * active functions (meas1), which the sampler reads for inclusive time. The
 * exit restores the depth saved on entry rather than decrementing, so a frame
 * skipped by a non-local exit cannot leave the chain off by one. */
extern "C" ppu_context* volatile g_ppu_main_ctx;
extern "C" volatile uint32_t g_ppu_fn_stamp;
extern "C" volatile uint32_t g_ppu_fn_stack[256];
extern "C" volatile uint32_t g_ppu_fn_depth;
struct PpuFnScope {
    uint32_t prev, prev_depth;
    bool on;
    PpuFnScope(ppu_context* c, uint32_t a) : prev(0), prev_depth(0), on(c == g_ppu_main_ctx)
    {
        if (on) {
            prev = g_ppu_fn_stamp; g_ppu_fn_stamp = a;
            prev_depth = g_ppu_fn_depth;
            if (prev_depth < 256u) g_ppu_fn_stack[prev_depth] = a;
            g_ppu_fn_depth = prev_depth + 1u;
        }
    }
    ~PpuFnScope() { if (on) { g_ppu_fn_stamp = prev; g_ppu_fn_depth = prev_depth; } }
};
'''

WTRACK_PRELUDE = r'''
/* ppu_instrument.py --wtrack: tracked functions report entry and exit. */
extern "C" void dbz3_wtrack_enter(ppu_context* c, uint32_t fn);
extern "C" void dbz3_wtrack_leave(ppu_context* c, uint32_t fn);
struct Dbz3WTrackScope {
    ppu_context* c; uint32_t fn; bool on;
    Dbz3WTrackScope(ppu_context* c_, uint32_t f) : c(c_), fn(f), on(c_ == g_ppu_main_ctx)
    { if (on) dbz3_wtrack_enter(c, fn); }
    ~Dbz3WTrackScope() { if (on) dbz3_wtrack_leave(c, fn); }
};
'''


def main():
    src, dst = sys.argv[1], sys.argv[2]
    wtrack = set()
    if '--wtrack' in sys.argv[3:]:
        arg = sys.argv[sys.argv.index('--wtrack') + 1]
        wtrack = {int(x, 16) for x in arg.split(',') if x}
    lines = open(src, encoding='utf-8', errors='surrogateescape').read().split('\n')
    out, n, prelude_done = [], 0, False
    for line in lines:
        m = FUNC_RE.match(line)
        if m:
            if not prelude_done:
                out.append(PRELUDE)
                if wtrack:
                    out.append(WTRACK_PRELUDE)
                prelude_done = True
            out.append(line)
            out.append(f'    PpuFnScope _pfs(ctx, 0x{m.group(1)}u);')
            if int(m.group(1), 16) in wtrack:
                out.append(f'    Dbz3WTrackScope _wts(ctx, 0x{m.group(1)}u);')
            n += 1
            continue
        out.append(line)
    open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write('\n'.join(out))
    print(f'ppu_instrument: {n} functions -> {dst}')


if __name__ == '__main__':
    main()
