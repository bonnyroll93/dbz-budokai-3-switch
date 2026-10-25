/*
 * DBZ3 (LAUNCH/DBZ3.elf) -- native replacements for hot lifted PPU functions.
 *
 * The lifted definitions are made weak by dbz3_weak_overrides.h (force-included
 * into their translation unit), so these definitions win at link time for
 * every caller, the indirect-call table included.
 *
 * Each one reproduces the lifted code's arithmetic in the same order and with
 * the same roundings (double FMA then round to single, as ppu_fmadd_core +
 * ppu_fp_single do), so results are bit-identical to the lifted code. Only
 * caller-visible state is produced: guest memory and the non-volatile
 * registers; volatile scratch registers (r4-r12, f0-f13, v2-v19, cr0) are not
 * reproduced -- the PPC ABI does not let a caller read them after a call.
 */

#define PPU_FAST_VM 1
#include "ppu_recomp.h"

#include <arm_neon.h>
#include <cstdio>
#include <cstring>

namespace {

inline uint32_t be32(uint32_t ea)
{
    uint32_t v;
    memcpy(&v, vm_base + ea, 4);
    return __builtin_bswap32(v);
}
inline float bef(uint32_t ea)
{
    uint32_t w = be32(ea);
    float f;
    memcpy(&f, &w, 4);
    return f;
}
inline uint32_t fbits(float f)
{
    uint32_t w;
    memcpy(&w, &f, 4);
    return w;
}
/* lvlx: the bytes from ea to the end of its 16-byte line, zero after. */
inline uint8x16_t lvlx(uint32_t ea)
{
    const uint32_t sh = ea & 15u;
    uint8_t b[16] = {0};
    memcpy(b, vm_base + (ea & ~15u) + sh, 16u - sh);
    return vld1q_u8(b);
}
/* vperm on raw big-endian register bytes: concat{a,b}[c[i] & 0x1F]. */
inline uint8x16_t vperm(uint8x16_t a, uint8x16_t b, uint8x16_t c)
{
    uint8x16x2_t t = {{ a, b }};
    return vqtbl2q_u8(t, vandq_u8(c, vdupq_n_u8(0x1F)));
}
/* Four BE floats as the raw bytes stvx stores. */
inline uint8x16_t row(float a, float b, float c, float d)
{
    uint32_t w[4] = { __builtin_bswap32(fbits(a)), __builtin_bswap32(fbits(b)),
                      __builtin_bswap32(fbits(c)), __builtin_bswap32(fbits(d)) };
    return vreinterpretq_u8_u32(vld1q_u32(w));
}
inline float fmuls(float a, float b) { return (float)((double)a * (double)b); }
inline float fadds(float a, float b) { return (float)((double)a + (double)b); }
inline float fsubs(float a, float b) { return (float)((double)a - (double)b); }
inline float fdivs(float a, float b) { return (float)((double)a / (double)b); }
/* fmadds / fnmsubs as the lifted code computes them. */
inline float fmadds(float a, float c, float b)
{ return (float)__builtin_fma((double)a, (double)c, (double)b); }
inline float fnmsubs(float a, float c, float b)
{
    const double r = __builtin_fma((double)a, (double)c, -(double)b);
    return (float)((r == r) ? -r : r);    /* a NaN keeps its sign (ppu_fmadd_core) */
}

bool check_guest(uint32_t ea, uint64_t expected_hash, const char* what)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (unsigned i = 0; i < 8; ++i) {
        const uint32_t word = be32(ea + 4u * i);
        for (int shift = 24; shift >= 0; shift -= 8) {
            hash ^= (word >> shift) & 0xFFu;
            hash *= 0x100000001B3ull;
        }
    }
    if (hash == expected_hash) return true;
    fprintf(stderr, "[t36] %s: guest code at 0x%08X does not match -- "
            "native replacement built for another DBZ3.elf\n", what, ea);
    return false;
}

} // namespace

/* 0x00025130: 4x4 transform from a quaternion and an optional translation.
 *   r3 = 16-byte-aligned output (4 rows of 4 floats)
 *   r4 = quaternion {x, y, z, w}
 *   r5 = translation (lvlx-loaded, may be unaligned) or 0 for (0, 0, 0, K2)
 *   constants K at 0x25100 = {0.0, 2.0, 1.0}; permute masks at 0x25110/0x25120
 * In fights this was ~11% of the main PPU thread (one call per bone). */
void func_00025130(ppu_context* ctx)
{
    static int checked = 0;
    if (__builtin_expect(!checked, 0)) {
        checked = check_guest(0x00025130u, 0xA5E0778B0892EBB2ull,
                              "func_00025130") ? 1 : -1;
        fprintf(stderr, "[t36] native func_00025130 (quaternion -> matrix) %s\n",
                checked > 0 ? "active" : "ON A MISMATCHED IMAGE");
    }

    const uint32_t out = (uint32_t)ctx->gpr[3] & ~15u;
    const uint32_t q = (uint32_t)ctx->gpr[4];
    const uint32_t v = (uint32_t)ctx->gpr[5];
    const uint32_t K = 0x00025100u;

    const float y = bef(q + 4), x = bef(q + 0), z = bef(q + 8), w = bef(q + 12);
    const float k0 = bef(K + 0), k1 = bef(K + 4), k2 = bef(K + 8);

    float n = fmuls(y, y);
    n = fmadds(x, x, n);
    n = fmadds(z, z, n);
    n = fmadds(w, w, n);
    const float inv = fdivs(k1, n);
    const double test = (double)k0 - (double)n;        /* fsub (double), fsel */
    const float s = (test >= 0.0) ? k0 : inv;

    const float ys = fmuls(y, s), zs = fmuls(z, s), xs = fmuls(x, s);
    const float yy = fmuls(y, ys), zz = fmuls(z, zs);
    const float xy = fmuls(x, ys), xz = fmuls(x, zs), yz = fmuls(y, zs);
    const float yyzz = fadds(yy, zz);
    const float xxzz = fmadds(x, xs, zz);
    const float xxyy = fmadds(x, xs, yy);
    const float r01 = fmadds(w, zs, xy);       /* f13 */
    const float r02 = fnmsubs(w, ys, xz);      /* f0  */
    const float r10 = fnmsubs(w, zs, xy);      /* f7  */
    const float r12 = fmadds(w, xs, yz);       /* f9  */
    const float r20 = fmadds(w, ys, xz);       /* f6  */
    const float r21 = fnmsubs(w, xs, yz);      /* f1  */
    const float r00 = fsubs(k2, yyzz);         /* f3  */
    const float r11 = fsubs(k2, xxzz);         /* f4  */
    const float r22 = fsubs(k2, xxyy);         /* f2  */

    /* Translation row: vperm(vperm(v5, v2, m1), vperm(v3, v4, m1), m2). */
    uint8x16_t v5, v2, v3, v4;
    if (v) {
        v5 = lvlx(v);
        v2 = lvlx(v + 4);
        v3 = lvlx(v + 8);
        v4 = lvlx(v + 12);
    } else {
        v5 = v2 = v3 = vdupq_n_u8(0);
        uint32_t raw;
        memcpy(&raw, vm_base + K + 8, 4);                /* lvlx + vspltw word 0 */
        v4 = vreinterpretq_u8_u32(vdupq_n_u32(raw));
    }
    const uint8x16_t m1 = vld1q_u8(vm_base + K + 0x10);
    const uint8x16_t m2 = vld1q_u8(vm_base + K + 0x20);
    const uint8x16_t t = vperm(vperm(v5, v2, m1), vperm(v3, v4, m1), m2);

    vst1q_u8(vm_base + out + 0x00, row(r00, r01, r02, 0.0f));
    vst1q_u8(vm_base + out + 0x10, row(r10, r11, r12, 0.0f));
    vst1q_u8(vm_base + out + 0x20, row(r20, r21, r22, 0.0f));
    vst1q_u8(vm_base + out + 0x30, t);
}

/* 0x00292628: the game's libc memmove (r3 = dst, r4 = src, r5 = size;
 * returns dst). The lifted body copied 8 bytes at a time through checked guest
 * accesses, then the tail a byte at a time; ~4.6% of the main thread in fights,
 * called from everywhere. Plain guest memory takes host memmove; a range that
 * touches anything the fast VM does not treat as plain (null page, RSX control
 * window, raw SPU, a line an SPU holds a reservation on) copies byte by byte
 * through the runtime accessors, in the direction memmove requires. */
namespace {
inline bool mm_plain(uint32_t ea, uint64_t n)
{
    if (!n) return true;
    if ((uint64_t)ea + n > 0xE0000000ull - 8u) return false;
    /* Must not touch the RSX control window 0x20000000..0x2FFFFFFF. */
    if (ea < 0x30000000u && (uint64_t)ea + n > 0x20000000ull) return false;
    return ppu_fvm_plain(ea, 1) && ppu_fvm_plain((uint32_t)(ea + n - 1u), 1);
}
inline bool mm_unreserved(uint32_t ea, uint64_t n)
{
    if (!*g_spu_coh_armed || !n) return true;
    for (uint32_t l = ea >> 7, last = (uint32_t)((ea + n - 1u) >> 7); l <= last; ++l)
        if ((g_spu_coh_bitmap[l >> 3] >> (l & 7)) & 1u) return false;
    return true;
}
} // namespace

void func_00292628(ppu_context* ctx)
{
    static int checked = 0;
    if (__builtin_expect(!checked, 0)) {
        checked = check_guest(0x00292628u, 0xA04FEE84B41AC86Bull,
                              "func_00292628") ? 1 : -1;
        fprintf(stderr, "[t36] native func_00292628 (memmove) %s\n",
                checked > 0 ? "active" : "ON A MISMATCHED IMAGE");
    }
    const uint32_t dst = (uint32_t)ctx->gpr[3];
    const uint32_t src = (uint32_t)ctx->gpr[4];
    const uint64_t n = ctx->gpr[5];
    ctx->gpr[3] = dst;                               /* rldicl r3,r3,0,32 */
    if (!n) return;
    if (n <= 0xFFFFFFFFull && mm_plain(dst, n) && mm_plain(src, n) && mm_unreserved(dst, n)) {
        memmove(vm_base + dst, vm_base + src, (size_t)n);
        return;
    }
    /* Exact accessor path. */
    if (dst <= src || (uint64_t)dst >= (uint64_t)src + n) {
        for (uint64_t i = 0; i < n; i++)
            vm_write8((uint32_t)(dst + i), vm_read8((uint32_t)(src + i)));
    } else {
        for (uint64_t i = n; i-- > 0;)
            vm_write8((uint32_t)(dst + i), vm_read8((uint32_t)(src + i)));
    }
}
