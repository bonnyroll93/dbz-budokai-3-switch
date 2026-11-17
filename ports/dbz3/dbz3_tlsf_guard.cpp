/*
 * TLSF guard (every Switch build): checks the block after the one being freed.
 *
 * func_0005494C is the free path of a TLSF allocator (reached from the main
 * loop through 0x48088 -> 0x53FA0 -> 0x55010 -> 0x547CC). With r5 = the block
 * being freed it looks at the next physical block, r6 = r5 + *(r5 + 0xC):
 * if *(r6 + 8) != r6 that block is free, and it is unlinked through
 * *(r6 + 4) and *(r6 + 8) to merge the two. run167 and two crashes on
 * 2 November died there reading guest 0xFF000004: the next block's link was
 * 0xFF000000, i.e. the owner of r5 (or something else) wrote past the end of
 * its block, over the next header.
 *
 * When that header is not sane (a used block links to itself; a free one's
 * list neighbours point back to it), this logs the block, the callers and the
 * memory around the overwritten header to the crash trail. Only when a link
 * points to unmapped memory, where the game would fault, is the next block
 * marked as in use (both links to itself) so the merge is skipped instead of
 * crashing. A diagnostic: the overrun itself is still there.
 *
 * scripts/ppu_liftfast.py calls dbz3_tlsf_free_check() first thing in
 * func_0005494C: the function is also reached through trampolines inside its
 * own source file, which a linker --wrap does not see (run169 crashed that
 * way with the wrap in place).
 */
#include "ppu_recomp.h"

#include <switch.h>

#include <cstdint>
#include <cstdio>

/* Weak: the guard is in every Switch build, the crash trail only in
 * crash-trail builds (without it the reports go to stderr only). */
extern "C" void dbz3_trail_event(const char* fmt, ...) __attribute__((weak));
extern "C" { extern uint8_t* vm_base; }   /* as dbz3_wtrack.cpp */

#define TRAIL(...) do { if (dbz3_trail_event) dbz3_trail_event(__VA_ARGS__); } while (0)

namespace {

int g_reports = 0;

/* Whether guest bytes [a, a + 12) are backed, asked of the kernel. The heaps
 * of this allocator live in sys_memory (0x4xxxxxxx) and in RSX local memory
 * (0xC5xxxxxx), their list heads in the control blocks (0x0048xxxx), so a
 * fixed address range is wrong (run168/run170 flagged sane blocks). */
/* Readable regions seen so far (host addresses, [lo, hi)), per thread: a
 * region is large, so the kernel is asked only for addresses outside them.
 * Only positive answers are kept. */
constexpr int kRegions = 8;
thread_local uint64_t t_lo[kRegions], t_hi[kRegions];
thread_local int t_next = 0;

bool host_readable(uint64_t at)
{
    for (int i = 0; i < kRegions; ++i)
        if (at >= t_lo[i] && at < t_hi[i]) return true;
    MemoryInfo mi;
    u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, at))) return false;
    if (mi.type == MemType_Unmapped || !(mi.perm & Perm_R)) return false;
    t_lo[t_next] = mi.addr;
    t_hi[t_next] = mi.addr + mi.size;
    t_next = (t_next + 1) % kRegions;
    return true;
}

bool mapped(uint32_t a)
{
    if (a < 0x00010000u || (a & 3u)) return false;
    const uint64_t at = (uint64_t)(uintptr_t)(vm_base + a);
    return host_readable(at) && host_readable(at + 8u);
}

/* 0: the next block is in use (links to itself) or free with list neighbours
 * pointing back to it. 1: free but the neighbours disagree (logged only).
 * 2: a link points to unmapped memory, where the allocator would crash. */
int next_state(uint32_t next, uint32_t prev_free, uint32_t next_free)
{
    if (next_free == next) return 0;               /* in use: no merge */
    if (!mapped(prev_free) || !mapped(next_free)) return 2;
    return vm_read32(prev_free + 8u) == next && vm_read32(next_free + 4u) == next ? 0 : 1;
}

void dump(uint32_t from, uint32_t to)
{
    for (uint32_t a = from; a < to; a += 16u)
        TRAIL("  %08X: %08X %08X %08X %08X", a, vm_read32(a), vm_read32(a + 4u),
                         vm_read32(a + 8u), vm_read32(a + 12u));
}

} // namespace

/* Called first thing in func_0005494C (scripts/ppu_liftfast.py adds the
 * call). Returns nonzero when the free must be skipped. */
extern "C" int dbz3_tlsf_free_check(ppu_context* ctx)
{
    const uint32_t block = (uint32_t)ctx->gpr[5];
    const uint32_t size = vm_read32(block + 0xCu);
    const uint32_t next = block + size;
    const bool size_ok = size >= 0x10u && size < 0x04000000u && (size & 3u) == 0;
    if (size_ok && !mapped(next)) return 0;      /* leave odd sizes to the game */
    const uint32_t prev_free = size_ok ? vm_read32(next + 4u) : 0u;
    const uint32_t next_free = size_ok ? vm_read32(next + 8u) : 0u;
    const int state = size_ok ? next_state(next, prev_free, next_free) : 2;
    if (state == 0) return 0;
    if (g_reports < 8) {
        /* lr: the allocator function that jumped here; the free wrapper's
         * 0x70-byte frame holds its caller's lr at +0x80. */
        const uint32_t caller = (uint32_t)vm_read64(ctx->gpr[1] + 0x80u);
        TRAIL("TLSF BAD #%d (%s): free block=%08X size=%08X next=%08X prev_free=%08X "
                         "next_free=%08X lr=%08X caller=%08X heap=%08X",
                         g_reports + 1, state == 2 ? "unmapped link, repaired" : "links disagree, left",
                         block, size, next, prev_free, next_free,
                         (uint32_t)ctx->lr, caller, (uint32_t)ctx->gpr[3]);
        fprintf(stderr, "[tlsf] BAD free block=%08X size=%08X next=%08X links=%08X/%08X lr=%08X caller=%08X\n",
                block, size, next, prev_free, next_free, (uint32_t)ctx->lr, caller);
        if (size_ok) {
            TRAIL("  block header and the end of its data, then the next header:");
            dump(block & ~15u, (block & ~15u) + 0x20u);
            dump((next & ~15u) - 0x40u, (next & ~15u) + 0x20u);
        }
        ++g_reports;
    }
    if (state == 1) return 0;                  /* the game reads mapped memory: as on PS3 */
    if (size_ok) {
        /* Where the game would fault: back to "in use" (links to itself),
         * how a used block looks, so the allocator skips the merge. */
        vm_write32(next + 4u, next);
        vm_write32(next + 8u, next);
        return 0;
    }
    /* No sane next block to look at: leave the block out of the free lists
     * (a leak) rather than walk garbage. */
    TRAIL("TLSF block %08X not freed (bad size)", block);
    return 1;
}

/* ---- per-frame heap walk (crash-trail builds) ------------------------------
 * The guards above see an overrun only when the damaged block is freed, long
 * after the write (run191: no free hit it, but model data was damaged and
 * hands, faces and Shenron went missing until a restart). This walks every
 * heap of the allocator every 4th pass and reports the first damaged header,
 * so the write is pinned to the last 4 passes.
 *
 * Heap control (func_000542F8 / func_00054398, r3): +0 base, +4 size,
 * +8 first-level bitmap, +0xC + fl * 0x204 second-level bitmap and list
 * heads, +0x408C bytes in use, +0x4090 blocks in use; 0x40B0 bytes.
 * Physical blocks from align16(base): +0 previous block when that one is
 * free (0 or stale otherwise), +4/+8 free-list links (both to itself when in
 * use), +0xC size to the next block. The last block, at align16(base) +
 * size - pad - 16, has size 0.
 * Read only: a damage report needs the same damage on two walks in a row, so
 * a block another thread is changing during the walk is not reported. */
namespace {

constexpr uint32_t kCtlSize = 0x40B0u;
constexpr int kMaxHeaps = 32;
uint32_t s_heaps[kMaxHeaps];
volatile int s_heap_count = 0;
Mutex s_heap_lock;

struct Bad { uint32_t heap, block, w[4]; const char* why; int index; };
Bad s_last{};                 /* candidate from the previous walk */
uint32_t s_reported[kMaxHeaps];   /* damaged block already reported, per heap */
int s_walk_reports = 0;
uint64_t s_walks = 0, s_blocks = 0, s_ticks = 0, s_max_ticks = 0;

bool in_heap(uint32_t heap, uint32_t a, uint32_t lo, uint32_t hi)
{
    if ((a & 3u) != 0) return false;
    return (a >= lo && a < hi) || (a >= heap && a < heap + kCtlSize);
}

/* Fills bad and returns true at the first damaged block. */
bool walk_heap(uint32_t heap, Bad& bad, uint64_t& blocks)
{
    const uint32_t base = vm_read32(heap), size = vm_read32(heap + 4u);
    const uint32_t pad = (0u - base) & 15u;
    if (base == 0 || size < pad + 0x40u) return false;
    uint32_t total = size - pad - 16u;
    if (total > 0xFC000000u) total = 0xFC000000u;
    const uint32_t lo = base + pad, hi = lo + total;
    if (hi < lo || !mapped(lo) || !mapped(hi)) return false;
    uint32_t prev = 0;
    bool prev_free = false;
    for (uint32_t blk = lo; blk < hi; ) {
        const uint32_t p0 = vm_read32(blk), pf = vm_read32(blk + 4u),
                       nf = vm_read32(blk + 8u), sz = vm_read32(blk + 0xCu);
        const char* why = nullptr;
        const bool used = nf == blk;
        if (sz < 16u || (sz & 15u) || sz > hi - blk) why = "bad size";
        else if (used && pf != blk) why = "used block, prev link not itself";
        else if (!used && (!in_heap(heap, pf, lo, hi) || !in_heap(heap, nf, lo, hi)))
            why = "free block, link outside its heap";
        else if (!used && (vm_read32(pf + 8u) != blk || vm_read32(nf + 4u) != blk))
            why = "free block, list neighbours disagree";
        else if (prev_free && p0 != prev) why = "previous block free but +0 not it";
        else if (!prev_free && p0 != 0 && p0 != prev) why = "+0 neither 0 nor previous block";
        ++blocks;
        if (why) {
            bad = { heap, blk, { p0, pf, nf, sz }, why, 0 };
            return true;
        }
        prev = blk;
        prev_free = !used;
        blk += sz;
    }
    return false;
}

void report(const Bad& b)
{
    const uint32_t base = vm_read32(b.heap), size = vm_read32(b.heap + 4u);
    TRAIL("TLSF WALK BAD #%d: heap=%08X (base %08X size %08X used %08X/%u) block=%08X "
          "header %08X %08X %08X %08X: %s",
          s_walk_reports + 1, b.heap, base, size, vm_read32(b.heap + 0x408Cu),
          vm_read32(b.heap + 0x4090u), b.block, b.w[0], b.w[1], b.w[2], b.w[3], b.why);
    fprintf(stderr, "[tlsf] WALK BAD heap=%08X block=%08X %08X %08X %08X %08X %s\n",
            b.heap, b.block, b.w[0], b.w[1], b.w[2], b.w[3], b.why);
    /* The previous physical block is the likely writer's: walk again up to it. */
    const uint32_t pad = (0u - base) & 15u;
    uint32_t prev = 0;
    for (uint32_t blk = base + pad; blk < b.block; ) {
        const uint32_t sz = vm_read32(blk + 0xCu);
        if (sz < 16u || (sz & 15u)) break;
        prev = blk;
        blk += sz;
    }
    if (prev) {
        const uint32_t hdr = prev + 24u;   /* alloc header when aligned to 16 */
        TRAIL("  previous block %08X size %08X %s; alloc header at %08X: heap %08X owner %08X "
              "block size %08X requested %08X",
              prev, vm_read32(prev + 0xCu), vm_read32(prev + 8u) == prev ? "used" : "free",
              hdr, vm_read32(hdr + 8u), vm_read32(hdr + 0xCu), vm_read32(hdr + 0x10u),
              vm_read32(hdr + 0x14u));
        TRAIL("  start of the previous block:");
        dump(prev, prev + 0x40u);
    }
    TRAIL("  end of the previous block, then the damaged header:");
    const uint32_t from = b.block - 0x60u >= base + pad ? b.block - 0x60u : base + pad;
    dump(from & ~15u, b.block + 0x20u);
}

} // namespace

/* Called first thing in func_00054398 (heap init, r3 = control). */
extern "C" void dbz3_tlsf_heap_init(ppu_context* ctx)
{
    const uint32_t heap = (uint32_t)ctx->gpr[3];
    mutexLock(&s_heap_lock);   /* zero-initialised = unlocked */
    int n = s_heap_count;
    bool known = false;
    for (int i = 0; i < n; ++i) known |= s_heaps[i] == heap;
    if (!known && n < kMaxHeaps) {
        s_heaps[n] = heap;
        __atomic_store_n(&s_heap_count, n + 1, __ATOMIC_RELEASE);
        TRAIL("TLSF heap #%d: control=%08X base=%08X size=%08X", n, heap,
              (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5]);
    }
    mutexUnlock(&s_heap_lock);
}

/* Once per pass, main thread (dbz3_loop_prof.cpp, crash-trail builds). */
extern "C" void dbz3_tlsf_walk(void)
{
    /* ~1 ms per walk with ~3000 blocks (run192): every 4th pass. */
    static uint32_t s_pass = 0;
    if ((++s_pass & 3u) != 0) return;
    const uint64_t t0 = armGetSystemTick();
    const int n = __atomic_load_n(&s_heap_count, __ATOMIC_ACQUIRE);
    Bad bad{};
    bool found = false;
    uint64_t blocks = 0;
    for (int i = 0; i < n && !found; ++i) {
        /* A heap whose damage was reported stops at that block from then on:
         * go on with the other heaps. */
        if (walk_heap(s_heaps[i], bad, blocks) && bad.block != s_reported[i]) {
            found = true;
            bad.index = i;
        }
    }
    if (found) {
        const bool same = s_last.why && s_last.heap == bad.heap && s_last.block == bad.block &&
                          s_last.w[0] == bad.w[0] && s_last.w[1] == bad.w[1] &&
                          s_last.w[2] == bad.w[2] && s_last.w[3] == bad.w[3];
        if (same) {
            if (s_walk_reports < 4) { report(bad); ++s_walk_reports; }
            s_reported[bad.index] = bad.block;
            s_last = Bad{};
        } else {
            s_last = bad;
        }
    } else {
        s_last = Bad{};
    }
    const uint64_t dt = armGetSystemTick() - t0;
    s_ticks += dt;
    if (dt > s_max_ticks) s_max_ticks = dt;
    s_blocks += blocks;
    if (++s_walks % 450u == 0) {   /* ~30 s */
        TRAIL("TLSF walk: heaps=%d blocks/walk=%llu avg=%lluus max=%lluus", n,
              (unsigned long long)(s_blocks / 450u),
              (unsigned long long)armTicksToNs(s_ticks / 450u) / 1000ull,
              (unsigned long long)armTicksToNs(s_max_ticks) / 1000ull);
        s_blocks = s_ticks = s_max_ticks = 0;
    }
}

/* Called first thing in func_000547CC, the free entry (r3 = user pointer).
 * The block header is at p - 24; its word at +0xC points 16 bytes past the
 * owning heap's control word, which is read next, then that word + 0xC.
 * run190 died there reading guest 0xFEFFFFF0: the header of the block being
 * freed (in use, so the guard in func_0005494C never saw it) held
 * 0xFF000000, the same value as the earlier overruns. Only when one of these
 * reads would fault is the free skipped (the block leaks); the header and the
 * memory before it are logged so the overrunning data can be recognised.
 * Returns nonzero when the free must be skipped. */
extern "C" int dbz3_tlsf_free_entry_check(ppu_context* ctx)
{
    const uint32_t p = (uint32_t)ctx->gpr[3];
    if (p == 0) return 0;                          /* free(NULL): the game returns */
    const uint32_t hdr = p - 24u;
    const char* why = nullptr;
    uint32_t owner = 0, heap = 0;
    if (!mapped(hdr) || !mapped(hdr + 0x8u)) why = "header unmapped";
    else {
        owner = vm_read32(hdr + 0xCu);
        if (owner == 0) return 0;                  /* the game's own path */
        if (!mapped(owner - 16u)) why = "owner unmapped";
        else {
            heap = vm_read32(owner - 16u);
            if (heap != 0 && !mapped(heap)) why = "heap unmapped";
        }
    }
    if (!why) return 0;
    if (g_reports < 8) {
        TRAIL("TLSF BAD #%d (free entry, %s, skipped): ptr=%08X hdr=%08X owner=%08X heap=%08X lr=%08X",
              g_reports + 1, why, p, hdr, owner, heap, (uint32_t)ctx->lr);
        fprintf(stderr, "[tlsf] BAD free entry ptr=%08X owner=%08X heap=%08X lr=%08X (%s)\n",
                p, owner, heap, (uint32_t)ctx->lr, why);
        /* Guest callers: back chain at 0(r1), saved lr at +16. */
        uint32_t sp = (uint32_t)ctx->gpr[1];
        for (int i = 0; i < 6 && mapped(sp); ++i) {
            sp = vm_read32(sp);
            if (!mapped(sp) || !mapped(sp + 16u)) break;
            TRAIL("  caller[%d] %08X", i, (uint32_t)vm_read64(sp + 16u));
        }
        if (mapped(hdr & ~15u)) {
            TRAIL("  memory before the header (overrun data?), then header and user data:");
            const uint32_t from = (hdr & ~15u) - 0x80u;
            if (mapped(from)) dump(from, (hdr & ~15u) + 0x30u);
        }
        ++g_reports;
    }
    return 1;
}
