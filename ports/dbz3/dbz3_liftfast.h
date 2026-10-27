/* T40 liftfast: cheaper guest memory accessors for the lifted PPU code
 * (Switch). Included by scripts/ppu_liftfast.py right after ppu_recomp.h, and
 * replaces its PPU_FAST_VM accessors.
 *
 * The ppu_recomp.h fast path reloads vm_base, g_ppu_fast_vm and ppu_vm_size
 * through the GOT on every access -- again after every guest store, since a
 * store through vm_base may alias anything (-fno-strict-aliasing) -- and GCC
 * sometimes outlines the accessors altogether. Here:
 *   - the boot-time constants are read through const-qualified aliases of the
 *     same symbols, so the compiler loads them once and keeps them in
 *     registers. They are written once at boot, before any lifted code runs:
 *     vm_base (main.cpp vm_reserve), g_ppu_fast_vm (switch_app.c fastvm-off),
 *     and the coherency pointers are already const;
 *   - hidden visibility: a direct PC-relative address instead of a GOT load;
 *   - ppu_vm_size is not tested: the Switch reserves the whole 32-bit guest
 *     space and sets it to 0 (main.cpp), which makes that test always pass;
 *   - one range test with the same bounds as ppu_fvm_plain:
 *     [0x10000, 0x20000000) and [0x30000000, 0xE0000000 - 8);
 *   - plain static inline (always_inline grew the binary ~50% and was slower
 *     on the console, run03); the out-of-range path calls the checked
 *     accessor (null page, RSX/HLE window, raw SPU, watch points, ...).
 * Stores still check the SPU reservation bitmap when it is armed. */
#ifndef DBZ3_LIFTFAST_H
#define DBZ3_LIFTFAST_H
#if defined(PPU_FAST_VM) && defined(__SWITCH__)
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define LF_HIDDEN __attribute__((visibility("hidden")))
#define LF_INLINE static inline

#undef vm_read8
#undef vm_read16
#undef vm_read32
#undef vm_read64
#undef vm_write8
#undef vm_write16
#undef vm_write32
#undef vm_write64

extern "C" {
extern uint8_t* const lf_vm_base __asm__("vm_base") LF_HIDDEN;
extern const int lf_fast_vm __asm__("g_ppu_fast_vm") LF_HIDDEN;
extern const unsigned char* const lf_coh_bitmap __asm__("g_spu_coh_bitmap") LF_HIDDEN;
extern const int* const lf_coh_armed __asm__("g_spu_coh_armed") LF_HIDDEN;
/* Checked accessors (ppu_loader.cpp). */
uint8_t  vm_read8(uint64_t);
uint16_t vm_read16(uint64_t);
uint32_t vm_read32(uint64_t);
uint64_t vm_read64(uint64_t);
void vm_write8(uint64_t, uint8_t);
void vm_write16(uint64_t, uint16_t);
void vm_write32(uint64_t, uint32_t);
void vm_write64(uint64_t, uint64_t);
}

LF_INLINE int lf_plain(uint32_t a)
{
    return lf_fast_vm &&
           ((a - 0x10000u) < 0x1FFF0000u || (a - 0x30000000u) < 0xAFFFFFF8u);
}

LF_INLINE int lf_reserved(uint32_t a, uint32_t n)
{
    if (__builtin_expect(!*lf_coh_armed, 1)) return 0;
    const uint32_t l0 = a >> 7, l1 = (a + n - 1u) >> 7;
    return ((lf_coh_bitmap[l0 >> 3] >> (l0 & 7)) & 1u) |
           ((lf_coh_bitmap[l1 >> 3] >> (l1 & 7)) & 1u);
}

LF_INLINE uint8_t lf_read8(uint64_t ea)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a), 1)) return lf_vm_base[a];
    return vm_read8(ea);
}
LF_INLINE uint16_t lf_read16(uint64_t ea)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a), 1)) {
        uint16_t v; memcpy(&v, lf_vm_base + a, 2); return __builtin_bswap16(v);
    }
    return vm_read16(ea);
}
LF_INLINE uint32_t lf_read32(uint64_t ea)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a), 1)) {
        uint32_t v; memcpy(&v, lf_vm_base + a, 4); return __builtin_bswap32(v);
    }
    return vm_read32(ea);
}
LF_INLINE uint64_t lf_read64(uint64_t ea)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a), 1)) {
        uint64_t v; memcpy(&v, lf_vm_base + a, 8); return __builtin_bswap64(v);
    }
    return vm_read64(ea);
}
LF_INLINE void lf_write8(uint64_t ea, uint8_t v)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a) && !lf_reserved(a, 1), 1)) { lf_vm_base[a] = v; return; }
    vm_write8(ea, v);
}
LF_INLINE void lf_write16(uint64_t ea, uint16_t v)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a) && !lf_reserved(a, 2), 1)) {
        const uint16_t s = __builtin_bswap16(v); memcpy(lf_vm_base + a, &s, 2); return;
    }
    vm_write16(ea, v);
}
LF_INLINE void lf_write32(uint64_t ea, uint32_t v)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a) && !lf_reserved(a, 4), 1)) {
        const uint32_t s = __builtin_bswap32(v); memcpy(lf_vm_base + a, &s, 4); return;
    }
    vm_write32(ea, v);
}
LF_INLINE void lf_write64(uint64_t ea, uint64_t v)
{
    const uint32_t a = (uint32_t)ea;
    if (__builtin_expect(lf_plain(a) && !lf_reserved(a, 8), 1)) {
        const uint64_t s = __builtin_bswap64(v); memcpy(lf_vm_base + a, &s, 8); return;
    }
    vm_write64(ea, v);
}

#define vm_read8(a)      lf_read8(a)
#define vm_read16(a)     lf_read16(a)
#define vm_read32(a)     lf_read32(a)
#define vm_read64(a)     lf_read64(a)
#define vm_write8(a, v)  lf_write8((a), (v))
#define vm_write16(a, v) lf_write16((a), (v))
#define vm_write32(a, v) lf_write32((a), (v))
#define vm_write64(a, v) lf_write64((a), (v))

#ifdef PORT_DBZ3_LEVEL_FUNCTION_PROFILE
extern "C" uint64_t dbz3_level30790_begin(void);
extern "C" void dbz3_level30790_end(uint64_t start);
struct dbz3_level30790_scope {
    uint64_t start;
    dbz3_level30790_scope() : start(dbz3_level30790_begin()) {}
    ~dbz3_level30790_scope() { dbz3_level30790_end(start); }
};
#endif

/* Reproduce selection-sort swaps in O(n log n). A stable sort is NOT
 * equivalent: the original swaps can reorder equal-depth objects. Each tree
 * node selects the greatest key, breaking ties by current list position.
 * Restrict keys to the exactly representable integer range: truncating then
 * storing them as floats must not change subsequent comparisons. Unusual
 * inputs fall back before any guest writes. Scratch is per invocation. */
LF_INLINE bool dbz3_sort_draw_list()
{
    const uint32_t count = vm_read32(0x484800);
    if (count < 32 || count > 16384) return false;
    const uint32_t base = vm_read32(0x484804);
    if ((base & 3) || base > UINT32_MAX - count * 8 ||
        !lf_plain(base) || !lf_plain(base + count * 8 - 1)) return false;
    uint32_t leaves = 1;
    while (leaves < count) leaves <<= 1;
    uint32_t* tree = (uint32_t*)malloc((2 * leaves + count) * sizeof(uint32_t));
    if (!tree) return false;
    int32_t* keys = (int32_t*)(tree + 2 * leaves);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t bits = vm_read32(base + i * 8 + 4);
        float key;
        memcpy(&key, &bits, 4);
        if (!(key >= -16777216.0f && key <= 16777216.0f)) {
            free(tree);
            return false;
        }
        keys[i] = (int32_t)key;
    }
    const auto winner = [keys](uint32_t a, uint32_t b) -> uint32_t {
        if (a == UINT32_MAX) return b;
        if (b == UINT32_MAX) return a;
        if (keys[a] != keys[b]) return keys[a] > keys[b] ? a : b;
        return a < b ? a : b;
    };
    for (uint32_t i = 0; i < leaves; ++i)
        tree[leaves + i] = i < count ? i : UINT32_MAX;
    for (uint32_t i = leaves - 1; i; --i)
        tree[i] = winner(tree[i * 2], tree[i * 2 + 1]);
    const auto update = [&](uint32_t index, uint32_t value) {
        uint32_t node = leaves + index;
        tree[node] = value;
        while ((node >>= 1) != 0)
            tree[node] = winner(tree[node * 2], tree[node * 2 + 1]);
    };
    for (uint32_t head = 0; head + 1 < count; ++head) {
        const uint32_t best = tree[1];
        const uint32_t head_addr = base + head * 8;
        const uint32_t best_addr = base + best * 8;
        const uint32_t object = vm_read32(head_addr);
        const uint32_t selected = vm_read32(best_addr);
        const float head_key = (float)keys[head];
        const float best_key = (float)keys[best];
        uint32_t head_bits, best_bits;
        memcpy(&head_bits, &head_key, 4);
        memcpy(&best_bits, &best_key, 4);
        vm_write32(head_addr + 4, best_bits);
        vm_write32(head_addr, selected);
        vm_write32(best_addr + 4, head_bits);
        vm_write32(best_addr, object);
        keys[best] = keys[head];
        update(head, UINT32_MAX);
        if (best != head) update(best, best);
    }
    free(tree);
    return true;
}

/* func_0005AD60's inner selection-sort scan. The lifted loop loads and
 * converts four vector lanes for each candidate, but compares only the first
 * lane (record+4). The outer loop still performs the original record swaps. */
static inline uint32_t ppu_f2i32(double v, int rn);
LF_INLINE void dbz3_sort_scan(ppu_context* ctx)
{
    uint32_t remaining = ctx->ctr;
    uint32_t cursor = (uint32_t)ctx->gpr[8];
    int32_t best = (int32_t)ctx->gpr[9];
    uint32_t best_addr = (uint32_t)ctx->gpr[11];
    uint32_t best_lane;
    memcpy(&best_lane, &ctx->vr[3], sizeof(best_lane));
    int32_t candidate = 0;

    while (remaining--) {
        const uint32_t bits = vm_read32((uint64_t)cursor + 4);
        float key;
        memcpy(&key, &bits, sizeof(key));
        candidate = (int32_t)ppu_f2i32((double)key, 0);
        if (best < candidate) {
            best = candidate;
            best_addr = cursor;
            const float clamped = key;
            const int32_t lane = (clamped != clamped) ? 0 :
                (clamped >= 2147483647.0f) ? 2147483647 :
                (clamped <= -2147483648.0f) ? (-2147483647 - 1) :
                (int32_t)clamped;
            best_lane = __builtin_bswap32((uint32_t)lane);
        }
        cursor += 8;
    }

    for (int i = 0; i < 4; ++i) {
        memcpy((uint8_t*)&ctx->vr[4] + i * 4, &best_lane, 4);
        memcpy((uint8_t*)&ctx->vr[3] + i * 4, &best_lane, 4);
    }
    ctx->gpr[11] = best_addr;
    ctx->gpr[9] = (uint32_t)best;
    ctx->gpr[7] = (uint32_t)candidate;
    ctx->gpr[8] = cursor;
    ctx->ctr = 0;
}

#endif /* PPU_FAST_VM && __SWITCH__ */
#endif /* DBZ3_LIFTFAST_H */
