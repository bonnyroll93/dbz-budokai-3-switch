/*
 * TLSF guard (crash-trail builds): checks the block after the one being freed.
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

extern "C" void dbz3_trail_event(const char* fmt, ...);
extern "C" { extern uint8_t* vm_base; }   /* as dbz3_wtrack.cpp */

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
        dbz3_trail_event("  %08X: %08X %08X %08X %08X", a, vm_read32(a), vm_read32(a + 4u),
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
        dbz3_trail_event("TLSF BAD #%d (%s): free block=%08X size=%08X next=%08X prev_free=%08X "
                         "next_free=%08X lr=%08X caller=%08X heap=%08X",
                         g_reports + 1, state == 2 ? "unmapped link, repaired" : "links disagree, left",
                         block, size, next, prev_free, next_free,
                         (uint32_t)ctx->lr, caller, (uint32_t)ctx->gpr[3]);
        fprintf(stderr, "[tlsf] BAD free block=%08X size=%08X next=%08X links=%08X/%08X lr=%08X caller=%08X\n",
                block, size, next, prev_free, next_free, (uint32_t)ctx->lr, caller);
        if (size_ok) {
            dbz3_trail_event("  block header and the end of its data, then the next header:");
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
    dbz3_trail_event("TLSF block %08X not freed (bad size)", block);
    return 1;
}
