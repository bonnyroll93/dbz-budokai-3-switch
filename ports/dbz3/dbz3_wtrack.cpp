/* Native-A step 1 (PORT_DBZ3_WTRACK, diagnostic only): which guest memory a
 * lifted function writes, so a native rewrite can be checked against it.
 *
 * Functions marked with ppu_instrument.py --wtrack call dbz3_wtrack_enter and
 * dbz3_wtrack_leave on the main thread; while one runs, every store through
 * the liftfast accessors calls dbz3_wtrack_note. Only stores from the thread
 * that entered the function count: the other guest threads keep running on
 * the other cores. Each store is classed by where it lands:
 *   R3/R4  within 0x800 bytes of the r3 or r4 the function was called with
 *          (logged as an offset from it),
 *   STACK  within 64 KiB below the entry r1,
 *   CMD    inside the game's GCM command buffer (begin..end of the context
 *          whose pointer is at 0x005D5610), counted in bytes,
 *   PAGE   anything else, grouped by 64 KiB page.
 * Vector stores the lifter writes as a raw memcpy are not seen. Every 5 s the
 * totals go to t32-wtrack.log. */
#include "ppu_recomp.h"
#include <switch.h>
#include <cstdio>
#include <cstring>

extern "C" { extern uint8_t* vm_base; }

extern "C" volatile int g_dbz3_wtrack_on = 0;

namespace {

enum { R3 = 0, R4 = 1, STACK = 2, CMD = 3, PAGE = 4 };
const char* const kClass[] = { "r3", "r4", "stack", "cmd", "page" };
const uint32_t kGcmCtxPtr = 0x005D5610u;   // CellGcmContextData*: begin, end, current

struct Slot { uint32_t key, count, lo, hi; };   // key: class<<28 | size<<16 | offset, or page
const unsigned kSlots = 4096;
Slot s_tab[kSlots];

struct Obj { uint32_t addr, vtable, calls; };
Obj s_r4_seen[8];

uint32_t s_fn, s_r3, s_r4, s_r1, s_cmd_lo, s_cmd_hi, s_cur0;
Handle s_thread;
int s_depth;
uint64_t s_window;
uint32_t s_calls, s_overflow;
uint64_t s_cmd_bytes, s_cur_delta, s_page_bytes;
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

void add(uint32_t key, uint32_t lo, uint32_t hi)
{
    unsigned h = (key * 2654435761u) % kSlots;
    for (unsigned p = 0; p < 64; p++) {
        Slot& s = s_tab[(h + p) % kSlots];
        if (!s.count) { s.key = key; s.count = 1; s.lo = lo; s.hi = hi; return; }
        if (s.key == key) {
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
        fprintf(s_file, "stores made by the main thread while a --wtrack function runs, per 5 s\n");
    }
    const unsigned long long n = s_calls ? s_calls : 1;
    fprintf(s_file, "window fn=0x%08X calls=%u overflow=%u cmdbuf=0x%08X..0x%08X "
                    "cmd_bytes/call=%llu current_advance/call=%llu page_bytes/call=%llu\n",
            s_fn, s_calls, s_overflow, s_cmd_lo, s_cmd_hi,
            (unsigned long long)s_cmd_bytes / n, (unsigned long long)s_cur_delta / n,
            (unsigned long long)s_page_bytes / n);
    for (const Obj& o : s_r4_seen)
        if (o.calls)
            fprintf(s_file, "  r4obj=0x%08X vtable=0x%08X calls=%u\n", o.addr, o.vtable, o.calls);
    for (int cls = R3; cls <= PAGE; cls++) {
        for (;;) {
            Slot* best = nullptr;
            for (Slot& s : s_tab)
                if (s.count && (s.key >> 28) == (uint32_t)cls &&
                    (!best || (s.key & 0x0FFFFFFF) < (best->key & 0x0FFFFFFF)))
                    best = &s;
            if (!best) break;
            const uint32_t v = best->key & 0x0FFFFFFF;
            if (cls == PAGE)
                fprintf(s_file, "  page=0x%08X stores=%u lo=0x%08X hi=0x%08X\n",
                        v << 16, best->count, best->lo, best->hi);
            else if (cls == STACK || cls == CMD)
                fprintf(s_file, "  %s stores=%u\n", kClass[cls], best->count);
            else
                fprintf(s_file, "  %s%+d size=%u stores=%u\n", kClass[cls],
                        (int)(v & 0xFFFF) - 0x800, (v >> 16) & 0xFF, best->count);
            best->count = 0;
        }
    }
    fflush(s_file);
    memset(s_tab, 0, sizeof s_tab);
    memset(s_r4_seen, 0, sizeof s_r4_seen);
    s_calls = s_overflow = 0;
    s_cmd_bytes = s_cur_delta = s_page_bytes = 0;
}

}  // namespace

extern "C" void dbz3_wtrack_enter(ppu_context* c, uint32_t fn)
{
    if (s_depth++) return;          // nested call of a tracked function
    s_fn = fn;
    s_r3 = (uint32_t)c->gpr[3];
    s_r4 = (uint32_t)c->gpr[4];
    s_r1 = (uint32_t)c->gpr[1];
    s_thread = threadGetCurHandle();
    const uint32_t g = guest32(kGcmCtxPtr);
    s_cmd_lo = g ? guest32(g) : 0;
    s_cmd_hi = g ? guest32(g + 4u) + 4u : 0;
    s_cur0 = g ? guest32(g + 8u) : 0;
    for (Obj& o : s_r4_seen) {
        if (o.calls && o.addr == s_r4) { o.calls++; break; }
        if (!o.calls) { o.addr = s_r4; o.vtable = s_r4 ? guest32(s_r4) : 0; o.calls = 1; break; }
    }
    g_dbz3_wtrack_on = 1;
}

extern "C" void dbz3_wtrack_leave(ppu_context* c, uint32_t fn)
{
    (void)c; (void)fn;
    if (--s_depth) return;
    g_dbz3_wtrack_on = 0;
    s_calls++;
    const uint32_t cur = gcm_current();
    if (cur >= s_cur0) s_cur_delta += cur - s_cur0;   // a buffer wrap counts 0
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
    if (a - (s_r3 - 0x800u) < 0x1000u)
        add((uint32_t)R3 << 28 | n << 16 | (a - s_r3 + 0x800u), 0, 0);
    else if (a - (s_r4 - 0x800u) < 0x1000u)
        add((uint32_t)R4 << 28 | n << 16 | (a - s_r4 + 0x800u), 0, 0);
    else if (s_r1 - a < 0x10000u)
        add((uint32_t)STACK << 28, 0, 0);
    else if (a - s_cmd_lo < s_cmd_hi - s_cmd_lo) {
        add((uint32_t)CMD << 28, 0, 0);
        s_cmd_bytes += n;
    } else {
        add((uint32_t)PAGE << 28 | a >> 16, a, a + n);
        s_page_bytes += n;
    }
}
