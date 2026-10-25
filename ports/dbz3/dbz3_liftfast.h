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

#endif /* PPU_FAST_VM && __SWITCH__ */
#endif /* DBZ3_LIFTFAST_H */
