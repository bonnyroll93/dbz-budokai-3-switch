/*
 * DBZ3 -- trace of the title allocator (logic60 heap corruption, T6.2b).
 *
 * func_0001D294  alloc(r3 = size, r4 = mode, r5 = source file name, r6 = line)
 *                -> r3 = user pointer (block header at -0x20)
 * func_0001D4D8  free(r3 = user pointer)
 * Both are made weak in their lifted TU (dbz3_weak_overrides.h) and replaced
 * here by the lifted bodies, copied verbatim (dbz3_heap_trace_lifted.inc,
 * generated from ppu_recomp_000.cpp), plus a ring of recent events: which
 * block, which caller (guest lr), which scheduler task and pass. When the
 * heap check in dbz3_loop_prof.cpp finds a broken link it dumps the events
 * whose block covers the broken address.
 */

#define PPU_FAST_VM 1
#include "dbz3_heap_trace_lifted.inc"

#include <atomic>
#include <cstdio>
#include <cstring>

extern "C" uint32_t g_dbz3_cur_task_fn;   /* dbz3_loop_prof.cpp */
extern "C" int g_dbz3_cur_pass;           /* 0 outside the scheduler, 1 regular, 2 extra */

namespace {

struct HeapEvent {
    uint32_t seq;
    uint8_t op;          /* 1 alloc, 2 free */
    uint8_t pass;
    uint16_t line;
    uint32_t user;       /* user pointer */
    uint32_t size;       /* header size word at the time */
    uint32_t lr;         /* guest caller */
    uint32_t task_fn;
    uint32_t file;       /* guest string (alloc only) */
};

constexpr uint32_t kRing = 8192;          /* power of two */
HeapEvent g_ring[kRing];
std::atomic<uint32_t> g_seq{0};

void push(uint8_t op, uint32_t user, uint32_t lr, uint32_t file, uint32_t line)
{
#ifdef PORT_DBZ3_LIGHT_BUILD
    (void)op; (void)user; (void)lr; (void)file; (void)line;
    return;
#else
    if (!user) return;
    const uint32_t s = g_seq.fetch_add(1, std::memory_order_relaxed);
    HeapEvent& e = g_ring[s & (kRing - 1)];
    e.seq = s;
    e.op = op;
    e.pass = (uint8_t)g_dbz3_cur_pass;
    e.line = (uint16_t)line;
    e.user = user;
    const int32_t sz = (int32_t)vm_read32(user - 0x20u);
    e.size = (uint32_t)(sz < 0 ? -sz : sz);
    e.lr = lr;
    e.task_fn = g_dbz3_cur_task_fn;
    e.file = file;
#endif
}

void guest_str(uint32_t ea, char* out, int n)
{
    int i = 0;
    if (ea >= 0x10000u && ea < 0x80000000u) {
        for (; i < n - 1; ++i) {
            const char c = (char)vm_read8(ea + (uint32_t)i);
            if (!c) break;
            out[i] = (c >= 32 && c < 127) ? c : '?';
        }
    }
    out[i] = 0;
}

} // namespace

extern "C" void dbz3_heap_trace_dump(FILE* f, uint32_t addr)
{
    if (!f) return;
    const uint32_t end = g_seq.load(std::memory_order_relaxed);
    const uint32_t begin = end > kRing ? end - kRing : 0;
    int shown = 0;
    fprintf(f, "  heap events covering 0x%08X (last %u events):\n", addr, end - begin);
    for (uint32_t s = begin; s < end; ++s) {
        const HeapEvent& e = g_ring[s & (kRing - 1)];
        if (e.seq != s) continue;
        const uint32_t lo = e.user - 0x20u, hi = e.user + e.size + 0x20u;
        if (addr < lo || addr >= hi) continue;
        char file[64];
        guest_str(e.file, file, sizeof file);
        fprintf(f, "    #%u %s user=0x%08X size=0x%X lr=0x%08X task=0x%08X pass=%u %s:%u\n",
                e.seq, e.op == 1 ? "ALLOC" : "FREE ", e.user, e.size, e.lr, e.task_fn,
                e.pass, e.op == 1 ? file : "", e.op == 1 ? e.line : 0u);
        if (++shown >= 64) break;
    }
    fflush(f);
}

/*
 * run30/run31 (Logic60, removed 2026-11-25): with 2 logic steps per image the
 * a2d (2D renderer) tables of a2d.cpp:410-420 overflowed and its state stacks
 * underflowed, because the draw side resets them once per drawn frame
 * (func_000558F8). The table growth and the empty-pop guard went with
 * Logic60: with one logic step per image, as on PS3, neither can happen.
 */
void func_0001D294(ppu_context* ctx)
{
    const uint32_t lr = (uint32_t)ctx->lr;
    const uint32_t file = (uint32_t)ctx->gpr[5];
    const uint32_t line = (uint32_t)ctx->gpr[6];
    lifted_0001D294(ctx);
    push(1, (uint32_t)ctx->gpr[3], lr, file, line);
}

/*
 * a2d state stack pops, as the lifted bodies do them (no empty check):
 *   func_000595F0  count *(0x484818) -= 1, top *(0x484810) -= 80   (base *(0x484808))
 *   func_000595CC  count *(0x48481C) -= 1, top *(0x484814) -= 4    (base *(0x48480C))
 */
static void a2d_pop(ppu_context* ctx, uint32_t count_ea, uint32_t top_ea, uint32_t base_ea,
                    uint32_t step, int which)
{
    (void)base_ea; (void)which;
    int32_t count = (int32_t)vm_read32(count_ea);
    uint32_t top = vm_read32(top_ea);
    count -= 1;
    top -= step;
    vm_write32(count_ea, (uint32_t)count);
    vm_write32(top_ea, top);
    /* Same volatile registers as the lifted body leaves. */
    ctx->gpr[3] = 0x00480000u;
    ctx->gpr[4] = 0x00480000u;
    ctx->gpr[5] = (uint64_t)(int64_t)count;
    ctx->gpr[6] = top;
}

void func_000595F0(ppu_context* ctx) { a2d_pop(ctx, 0x00484818u, 0x00484810u, 0x00484808u, 80u, 0); }
void func_000595CC(ppu_context* ctx) { a2d_pop(ctx, 0x0048481Cu, 0x00484814u, 0x0048480Cu, 4u, 1); }

void func_0001D4D8(ppu_context* ctx)
{
    const uint32_t lr = (uint32_t)ctx->lr;
    const uint32_t user = (uint32_t)ctx->gpr[3];
    push(2, user, lr, 0, 0);       /* before: the free rewrites the header */
    lifted_0001D4D8(ctx);
}
