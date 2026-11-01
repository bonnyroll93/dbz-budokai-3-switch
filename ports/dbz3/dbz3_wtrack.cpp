/* Native-A (PORT_DBZ3_WTRACK, diagnostic only): which guest memory lifted
 * functions write, so native rewrites can be checked against it.
 *
 * Functions marked with ppu_instrument.py --wtrack call dbz3_wtrack_enter and
 * dbz3_wtrack_leave on the main thread. While any of them runs, every store
 * through the liftfast accessors calls dbz3_wtrack_note, and the store is
 * credited to every tracked function active at that moment (a store made by
 * a tracked callee belongs to its tracked callers too). Only stores from the
 * thread that entered count: the other guest threads keep running on the
 * other cores. Each store is classed, per function, by where it lands:
 *   R3/R4/R5  within 0x800 bytes of the r3, r4 or r5 the function was called
 *             with (logged as an offset from it),
 *   STACK     within 64 KiB below the entry r1,
 *   CMD       inside the game's GCM command buffer (begin..end of the context
 *             whose pointer is at 0x005D5610), counted in bytes,
 *   PAGE      anything else, grouped by 64 KiB page.
 * Vector stores the lifter writes as a raw memcpy are not seen. Every 5 s the
 * totals go to t32-wtrack.log, one block per function. */
#include "ppu_recomp.h"
#include <switch.h>
#include <cstdio>
#include <cstring>

extern "C" { extern uint8_t* vm_base; }

extern "C" volatile int g_dbz3_wtrack_on = 0;

namespace {

enum { R3 = 0, R4 = 1, R5 = 2, STACK = 3, CMD = 4, PAGE = 5 };
const char* const kClass[] = { "r3", "r4", "r5", "stack", "cmd", "page" };
const uint32_t kGcmCtxPtr = 0x005D5610u;   // CellGcmContextData*: begin, end, current

const unsigned kMaxFns = 16, kMaxDepth = 8;

struct Slot { uint32_t key, fn, count, lo, hi; };   // key: class<<28 | size<<16 | offset, or page
const unsigned kSlots = 8192;
Slot s_tab[kSlots];

struct FnStats {
    uint32_t fn, calls;
    uint64_t cmd_bytes, cur_delta, page_bytes;
};
FnStats s_fns[kMaxFns];
unsigned s_nfns;

struct Frame { unsigned fi; uint32_t r[3], r1, cur0; };
Frame s_stack[kMaxDepth];
unsigned s_depth, s_overflow_depth;

Handle s_thread;
uint32_t s_cmd_lo, s_cmd_hi;
uint64_t s_window;
uint32_t s_overflow;
FILE* s_file;

uint32_t guest32(uint32_t a)
{
    uint32_t v;
    memcpy(&v, vm_base + a, 4);
    return __builtin_bswap32(v);
}

uint32_t gcm_current()
{
    const uint32_t g = guest32(kGcmCtxPtr);
    return g ? guest32(g + 8u) : 0;
}

unsigned fn_index(uint32_t fn)
{
    for (unsigned i = 0; i < s_nfns; i++)
        if (s_fns[i].fn == fn) return i;
    if (s_nfns == kMaxFns) return kMaxFns;
    s_fns[s_nfns] = FnStats{ fn, 0, 0, 0, 0 };
    return s_nfns++;
}

void add(unsigned fi, uint32_t key, uint32_t lo, uint32_t hi)
{
    unsigned h = ((key ^ (fi * 0x9E3779B9u)) * 2654435761u) % kSlots;
    for (unsigned p = 0; p < 64; p++) {
        Slot& s = s_tab[(h + p) % kSlots];
        if (!s.count) { s.key = key; s.fn = fi; s.count = 1; s.lo = lo; s.hi = hi; return; }
        if (s.key == key && s.fn == fi) {
            s.count++;
            if (lo < s.lo) s.lo = lo;
            if (hi > s.hi) s.hi = hi;
            return;
        }
    }
    s_overflow++;
}

void dump()
{
    if (!s_file) {
        s_file = fopen("sdmc:/switch/dbz-budokai-3-switch/t32-wtrack.log", "w");
        if (!s_file) return;
        fprintf(s_file, "stores made by the main thread while --wtrack functions run, per 5 s\n");
    }
    fprintf(s_file, "window overflow=%u depth_overflow=%u cmdbuf=0x%08X..0x%08X\n",
            s_overflow, s_overflow_depth, s_cmd_lo, s_cmd_hi);
    for (unsigned fi = 0; fi < s_nfns; fi++) {
        FnStats& f = s_fns[fi];
        if (!f.calls) continue;
        fprintf(s_file, " fn=0x%08X calls=%u cmd_bytes/call=%llu current_advance/call=%llu page_bytes/call=%llu\n",
                f.fn, f.calls, (unsigned long long)(f.cmd_bytes / f.calls),
                (unsigned long long)(f.cur_delta / f.calls), (unsigned long long)(f.page_bytes / f.calls));
        for (int cls = R3; cls <= PAGE; cls++) {
            for (;;) {
                Slot* best = nullptr;
                for (Slot& s : s_tab)
                    if (s.count && s.fn == fi && (s.key >> 28) == (uint32_t)cls &&
                        (!best || (s.key & 0x0FFFFFFF) < (best->key & 0x0FFFFFFF)))
                        best = &s;
                if (!best) break;
                const uint32_t v = best->key & 0x0FFFFFFF;
                if (cls == PAGE)
                    fprintf(s_file, "   page=0x%08X stores=%u lo=0x%08X hi=0x%08X\n",
                            v << 16, best->count, best->lo, best->hi);
                else if (cls == STACK || cls == CMD)
                    fprintf(s_file, "   %s stores=%u\n", kClass[cls], best->count);
                else
                    fprintf(s_file, "   %s%+d size=%u stores=%u\n", kClass[cls],
                            (int)(v & 0xFFFF) - 0x800, (v >> 16) & 0xFF, best->count);
                best->count = 0;
            }
        }
        f.calls = 0;
        f.cmd_bytes = f.cur_delta = f.page_bytes = 0;
    }
    fflush(s_file);
    memset(s_tab, 0, sizeof s_tab);
    s_overflow = s_overflow_depth = 0;
}

}  // namespace

extern "C" void dbz3_wtrack_enter(ppu_context* c, uint32_t fn)
{
    if (s_depth >= kMaxDepth) { s_depth++; s_overflow_depth++; return; }
    if (s_depth == 0) {
        s_thread = threadGetCurHandle();
        const uint32_t g = guest32(kGcmCtxPtr);
        s_cmd_lo = g ? guest32(g) : 0;
        s_cmd_hi = g ? guest32(g + 4u) + 4u : 0;
    }
    Frame& f = s_stack[s_depth++];
    f.fi = fn_index(fn);
    f.r[0] = (uint32_t)c->gpr[3];
    f.r[1] = (uint32_t)c->gpr[4];
    f.r[2] = (uint32_t)c->gpr[5];
    f.r1 = (uint32_t)c->gpr[1];
    f.cur0 = gcm_current();
    g_dbz3_wtrack_on = 1;
}

extern "C" void dbz3_wtrack_leave(ppu_context* c, uint32_t fn)
{
    (void)c; (void)fn;
    if (s_depth > kMaxDepth) { s_depth--; return; }
    const Frame& f = s_stack[--s_depth];
    if (f.fi < kMaxFns) {
        FnStats& st = s_fns[f.fi];
        st.calls++;
        const uint32_t cur = gcm_current();
        if (cur >= f.cur0) st.cur_delta += cur - f.cur0;   // a buffer wrap counts 0
    }
    if (s_depth) return;
    g_dbz3_wtrack_on = 0;
    const uint64_t now = armGetSystemTick();
    if (!s_window) s_window = now;
    if (armTicksToNs(now - s_window) >= 5000000000ull) {
        dump();
        s_window = now;
    }
}

extern "C" void dbz3_wtrack_note(uint32_t a, uint32_t n)
{
    if (threadGetCurHandle() != s_thread) return;
    const unsigned depth = s_depth < kMaxDepth ? s_depth : kMaxDepth;
    const bool in_cmd = a - s_cmd_lo < s_cmd_hi - s_cmd_lo;
    for (unsigned d = 0; d < depth; d++) {
        const Frame& f = s_stack[d];
        if (f.fi >= kMaxFns) continue;
        FnStats& st = s_fns[f.fi];
        int cls = -1;
        for (int r = 0; r < 3 && cls < 0; r++)
            if (a - (f.r[r] - 0x800u) < 0x1000u) {
                add(f.fi, (uint32_t)r << 28 | n << 16 | (a - f.r[r] + 0x800u), 0, 0);
                cls = r;
            }
        if (cls >= 0) continue;
        if (f.r1 - a < 0x10000u) add(f.fi, (uint32_t)STACK << 28, 0, 0);
        else if (in_cmd) { add(f.fi, (uint32_t)CMD << 28, 0, 0); st.cmd_bytes += n; }
        else { add(f.fi, (uint32_t)PAGE << 28 | a >> 16, a, a + n); st.page_bytes += n; }
    }
}
