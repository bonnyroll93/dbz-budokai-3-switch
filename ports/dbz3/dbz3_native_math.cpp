/*
 * DBZ3: native versions of two hot math functions of the guest.
 *
 *   0x002A733C  sinf/cosf kernel of the title's libm: f1 = angle, r4 = 0 for
 *               sin, 1 for cos. Range reduction by pi/2 and a polynomial. The
 *               rare paths (infinite argument, which sets errno, and huge
 *               arguments, reduced by 0x002A7D10) still run the lifted code.
 *   0x000252B0  builds a 4x4 matrix from a quaternion (r3) and an optional
 *               translation (r4, 0 = none) with 0x00025130, then multiplies
 *               the global matrix at 0x0047A300 by it (AltiVec).
 *   0x0001F1E8  r3 = the global matrix at 0x0047A300 times the point at r4
 *               (x, y, z, 1): four floats.
 *   0x002B6E00  the cellGcmAddressToOffset import stub: the runtime's
 *               function called directly, without the generic HLE dispatch
 *               (diagnostics, milestone log and NID lookup on every call).
 *
 * Both do the guest's floating point operations in the guest's order with the
 * lift's own helpers (scalar) or NEON (vectors: multiply, then add, as the
 * lift), so the results are bit-identical. The vector code of 0x000252B0 and
 * 0x00025130 was converted line by line from the lift; vectors hold the
 * guest's big-endian bytes, as ctx->vr does, and their stack scratch is
 * written to guest memory exactly as the lift writes it.
 *
 * SD switches (sdmc:/switch/dbz-budokai-3-switch/):
 *   native-sincos-off  lifted 0x002A733C
 *   native-matrix-off  lifted 0x000252B0
 *   native-point-off   lifted 0x0001F1E8
 *   native-a2o-off     HLE dispatch for 0x002B6E00
 *   native-rotvec-off, -scale-off, -translate-off, -add3-off, -len3-off,
 *   -planes-off, -euler-off, -memset-off: the small helpers 0x0001EEF8,
 *                      0x0001F164, 0x0001EF6C, 0x000297B8, 0x000299AC,
 *                      0x000357A0, 0x00024F58, 0x002993A0 (memset)
 *   native-put4-off, -put2f-off, -put12-off, -vcall3-off, -transpose-off,
 *   -vcall-a2o-off: draw-state writers 0x0004A39C, 0x0004A428, 0x0004A300,
 *                      0x0004A118,
 *                      0x0004E100, 0x0004EE80 (the last three call other
 *                      guest code and are not checked by native-verify)
 *   native-verify      run the lifted function first, then the native one
 *                      from the same state, compare the results (f1, the
 *                      64 bytes at 0x0047A300, the 16 bytes at r3, or r3 and
 *                      the offset word) and keep the lifted result on any
 *                      difference (t37-native-math.log)
 */
#define PPU_FAST_VM 1
#include "dbz3_lifted_prelude.inc"
#include "dbz3_store_hook.h"

#include <arm_neon.h>
#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void func_002A733C_lifted(ppu_context* ctx);
void func_000252B0_lifted(ppu_context* ctx);
void func_0001F1E8_lifted(ppu_context* ctx);
void func_002B6E00_lifted(ppu_context* ctx);
extern "C" int32_t cellGcmAddressToOffset(uint32_t address, uint32_t* offset);   // cellGcmSys.c
extern "C" __attribute__((weak)) void ppu_gcm_pump(void);
void func_002A733C(ppu_context* ctx);   // dispatcher below (native or lifted sin/cos)
void func_0001EEF8_lifted(ppu_context* ctx);
void func_0001F164_lifted(ppu_context* ctx);
void func_0001EF6C_lifted(ppu_context* ctx);
void func_000297B8_lifted(ppu_context* ctx);
void func_000299AC_lifted(ppu_context* ctx);
void func_000357A0_lifted(ppu_context* ctx);
void func_00024F58_lifted(ppu_context* ctx);
void func_002993A0_lifted(ppu_context* ctx);
void func_0004A39C_lifted(ppu_context* ctx);
void func_0004A428_lifted(ppu_context* ctx);
void func_0004A300_lifted(ppu_context* ctx);
void func_0004A118_lifted(ppu_context* ctx);
void func_0004E100_lifted(ppu_context* ctx);
void func_0004EE80_lifted(ppu_context* ctx);
void func_00292628(ppu_context* ctx);   // memmove, native in dbz3_ppu_native.cpp
void func_002B6E00(ppu_context* ctx);
/* The lift's fast-memory switch and SPU reservation watch (as dbz3_liftfast.h). */
extern "C" {
extern const int nm_fast_vm __asm__("g_ppu_fast_vm");
extern const int* const nm_coh_armed __asm__("g_spu_coh_armed");
extern const unsigned char* const nm_coh_bitmap __asm__("g_spu_coh_bitmap");
}

namespace {

/* ---- guest memory -------------------------------------------------------- */
inline uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }
inline uint64_t r64(uint32_t a) { uint64_t v; memcpy(&v, vm_base + a, 8); return __builtin_bswap64(v); }
inline double lfs(uint64_t a) { const uint32_t w = r32((uint32_t)a); float f; memcpy(&f, &w, 4); return f; }
inline double lfd(uint64_t a) { const uint64_t w = r64((uint32_t)a); double d; memcpy(&d, &w, 8); return d; }
inline void w32(uint64_t ea, uint32_t v)
{
    const uint32_t a = (uint32_t)ea;
    dbz3_store_hook(a, 4u);
    v = __builtin_bswap32(v);
    memcpy(vm_base + a, &v, 4);
}
inline void w64(uint64_t ea, uint64_t v)
{
    const uint32_t a = (uint32_t)ea;
    dbz3_store_hook(a, 8u);
    v = __builtin_bswap64(v);
    memcpy(vm_base + a, &v, 8);
}
inline void stfs(uint64_t ea, double d) { const float f = (float)d; uint32_t w; memcpy(&w, &f, 4); w32(ea, w); }

/* ---- AltiVec on guest-order bytes ----------------------------------------- */
typedef uint8x16_t V;
inline V lvx(uint64_t ea) { return vld1q_u8(vm_base + (uint32_t)(ea & ~0xFull)); }
inline void stvx(uint64_t ea, V v)
{
    const uint32_t a = (uint32_t)(ea & ~0xFull);
    dbz3_store_hook(a, 16u);
    vst1q_u8(vm_base + a, v);
}
/* The lift's lvlx: bytes ea..end of the 16-byte block, then zeros. */
inline V lvlx(uint64_t ea)
{
    static const uint8_t kIota[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    const V idx = vaddq_u8(vld1q_u8(kIota), vdupq_n_u8((uint8_t)(ea & 0xFu)));
    return vqtbl1q_u8(lvx(ea), idx);   // indices >= 16 give 0
}
inline V vsplatw(uint32_t x) { return vreinterpretq_u8_u32(vdupq_n_u32(__builtin_bswap32(x))); }
inline uint32x4_t host_u32(V v) { return vreinterpretq_u32_u8(vrev32q_u8(v)); }
inline V guest_u32(uint32x4_t v) { return vrev32q_u8(vreinterpretq_u8_u32(v)); }
inline V vslw(V a, V b)
{
    const uint32x4_t s = vandq_u32(host_u32(b), vdupq_n_u32(31u));
    return guest_u32(vshlq_u32(host_u32(a), vreinterpretq_s32_u32(s)));
}
/* vmaddfp d = a*c + b: multiply, then add, as the lift (not fused). */
inline V vmaddfp(V a, V b, V c)
{
    const float32x4_t fa = vreinterpretq_f32_u8(vrev32q_u8(a));
    const float32x4_t fb = vreinterpretq_f32_u8(vrev32q_u8(b));
    const float32x4_t fc = vreinterpretq_f32_u8(vrev32q_u8(c));
    return vrev32q_u8(vreinterpretq_u8_f32(vaddq_f32(vmulq_f32(fa, fc), fb)));
}
inline V vmrghw(V a, V b)
{
    return vreinterpretq_u8_u32(vzip1q_u32(vreinterpretq_u32_u8(a), vreinterpretq_u32_u8(b)));
}
inline V vperm(V a, V b, V c)
{
    const uint8x16x2_t t = { { a, b } };
    return vqtbl2q_u8(t, vandq_u8(c, vdupq_n_u8(0x1Fu)));
}
inline V vspltw0(V a) { return vreinterpretq_u8_u32(vdupq_laneq_u32(vreinterpretq_u32_u8(a), 0)); }

/* ---- 0x002A733C: sinf / cosf kernel --------------------------------------- */
bool sincos_native(ppu_context* c)
{
    const float xf = (float)c->fpr[1];                 // stfs f1 -> 0xD0(r1)
    uint32_t xb; memcpy(&xb, &xf, 4);
    const uint32_t hi = xb >> 16, lo = xb & 0xFFFFu;
    const uint64_t r2 = c->gpr[2], r31 = c->gpr[4];
    // 0x002A6EB4 classify: 1 inf, 2 nan, 0 zero, -1 finite
    int cls;
    if ((hi & 0x7F80u) == 0x7F80u) cls = ((hi & 0x7Fu) == 0 && lo == 0) ? 1 : 2;
    else cls = ((hi & 0x7FFFu) == 0 && lo == 0) ? 0 : -1;
    if (cls == 1) return false;                        // sets errno: lifted
    if (cls == 2) { c->fpr[1] = (double)xf; return true; }
    if (cls == 0) { c->fpr[1] = ((int32_t)r31 == 0) ? (double)xf : lfs(r2 - 0x5F48); return true; }

    const double x = xf;
    if (x < lfd(r2 - 0x5F40) || x > lfd(r2 - 0x5F38)) return false;   // 0x002A7D10: lifted
    const double f12a = ppu_fp_single(ppu_fmul(x, lfs(r2 - 0x5F2C)));
    double t;
    if (f12a > lfs(r2 - 0x5F00)) t = ppu_fp_single(ppu_fadd(f12a, lfs(r2 - 0x5F28)));
    else t = ppu_fp_single(ppu_fsub(f12a, lfs(r2 - 0x5F28)));
    const uint32_t n = ppu_f2i32(t, 0);                // fctiwz, stfiwx, lwz
    const uint32_t q = (n & 3u) + (uint32_t)r31;
    const double f12 = ppu_frsp((double)(int64_t)(int32_t)n);   // extsw, fcfid, frsp
    const double thr = lfs(r32((uint32_t)(r2 - 0x5F1C)));
    const double f13 = ppu_fp_single(ppu_fmadd_core(f12, lfs(r2 - 0x5F24), x, 0, 0));
    const double y = ppu_fp_single(ppu_fmadd_core(f12, lfs(r2 - 0x5F20), f13, 0, 0));
    double r;
    if (-thr < y && y < thr) {
        r = (q & 1u) ? lfs(r2 - 0x5F48) : y;
    } else {
        const double z = ppu_fp_single(ppu_fmul(y, y));
        if (!(q & 1u)) {
            const double zy = ppu_fp_single(ppu_fmul(y, z));
            double p = ppu_fp_single(ppu_fmadd_core(z, lfs(r2 - 0x5F0C), lfs(r2 - 0x5F08), 0, 0));
            p = ppu_fp_single(ppu_fmadd_core(z, p, lfs(r2 - 0x5F04), 1, 0));
            r = ppu_fp_single(ppu_fmadd_core(zy, p, y, 0, 0));
        } else {
            double p = ppu_fp_single(ppu_fmadd_core(z, lfs(r2 - 0x5F18), lfs(r2 - 0x5F14), 0, 0));
            p = ppu_fp_single(ppu_fmadd_core(z, p, lfs(r2 - 0x5F10), 1, 0));
            r = ppu_fp_single(ppu_fmadd_core(z, p, lfs(r2 - 0x5F48), 0, 0));
        }
    }
    c->fpr[1] = (q & 2u) ? -r : r;
    return true;
}

/* ---- 0x00025130: quaternion r4 (+ translation r5) -> 4x4 matrix at r3 -----
 * sp is the caller's stack pointer; the frame scratch is written like the lift. */
__attribute__((always_inline)) inline void quat_matrix(uint64_t r3, uint64_t r4, uint64_t r5, uint64_t sp)
{
    const uint64_t r1 = sp - 0x70;
    uint64_t r6, r7, r8, r9;
    double f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13;
    V v2, v3, v4, v5, v6, v7, v8;
    const uint64_t a5 = r5;
    f1 = lfs(r4 + 0x4);
    r6 = (int64_t)(int32_t)((uint32_t)0x2 << 16);
    f2 = lfs(r4 + 0x0);
    f3 = ppu_fp_single(ppu_fmul(f1, f1));
    f4 = lfs(r4 + 0x8);
    r6 = r6 + (int64_t)(0x5100);
    f5 = lfs(r4 + 0xC);
    f6 = lfs(r6 + 0x4);
    f3 = ppu_fp_single(ppu_fmadd_core(f2, f2, f3, 0, 0));
    f7 = lfs(r6 + 0x0);
    f8 = lfs(r6 + 0x8);
    r4 = (int64_t)(int32_t)(0);
    r7 = (int64_t)(int32_t)(8);
    v3 = vsplatw((uint32_t)(int32_t)(0));
    w32(r1 + 0x3C, (uint32_t)r4);
    f3 = ppu_fp_single(ppu_fmadd_core(f4, f4, f3, 0, 0));
    w32(r1 + 0x4C, (uint32_t)r4);
    w32(r1 + 0x5C, (uint32_t)r4);
    v2 = v3;
    v5 = v2;
    f3 = ppu_fp_single(ppu_fmadd_core(f5, f5, f3, 0, 0));
    f6 = ppu_fp_single(ppu_fdiv(f6, f3));
    f3 = ppu_fsub(f7, f3);
    f3 = (f3 >= 0.0) ? f7 : f6;
    f6 = ppu_fp_single(ppu_fmul(f1, f3));
    f7 = ppu_fp_single(ppu_fmul(f4, f3));
    f3 = ppu_fp_single(ppu_fmul(f2, f3));
    f9 = ppu_fp_single(ppu_fmul(f1, f6));
    f4 = ppu_fp_single(ppu_fmul(f4, f7));
    f10 = ppu_fp_single(ppu_fmul(f2, f6));
    f11 = ppu_fp_single(ppu_fmul(f2, f7));
    f1 = ppu_fp_single(ppu_fmul(f1, f7));
    f12 = ppu_fp_single(ppu_fadd(f9, f4));
    f4 = ppu_fp_single(ppu_fmadd_core(f2, f3, f4, 0, 0));
    f2 = ppu_fp_single(ppu_fmadd_core(f2, f3, f9, 0, 0));
    f13 = ppu_fp_single(ppu_fmadd_core(f5, f7, f10, 0, 0));
    f0 = ppu_fp_single(ppu_fmadd_core(f5, f6, f11, 1, 1));
    f7 = ppu_fp_single(ppu_fmadd_core(f5, f7, f10, 1, 1));
    f9 = ppu_fp_single(ppu_fmadd_core(f5, f3, f1, 0, 0));
    f6 = ppu_fp_single(ppu_fmadd_core(f5, f6, f11, 0, 0));
    f1 = ppu_fp_single(ppu_fmadd_core(f5, f3, f1, 1, 1));
    f3 = ppu_fp_single(ppu_fsub(f8, f12));
    f4 = ppu_fp_single(ppu_fsub(f8, f4));
    f2 = ppu_fp_single(ppu_fsub(f8, f2));
    stfs(r1 + 0x34, f13);
    stfs(r1 + 0x38, f0);
    stfs(r1 + 0x40, f7);
    stfs(r1 + 0x48, f9);
    stfs(r1 + 0x50, f6);
    stfs(r1 + 0x54, f1);
    stfs(r1 + 0x30, f3);
    stfs(r1 + 0x44, f4);
    stfs(r1 + 0x58, f2);
    v4 = lvlx((r7 + r6));
    v4 = vspltw0(v4);
    if ((int32_t)a5 != 0) {
        r4 = (int64_t)(int32_t)(4);
        r6 = (int64_t)(int32_t)(8);
        v5 = lvlx(r5);
        r7 = (int64_t)(int32_t)(0xC);
        v2 = lvlx((r4 + r5));
        v3 = lvlx((r6 + r5));
        v4 = lvlx((r7 + r5));
    }
    r4 = (int64_t)(int32_t)((uint32_t)0x2 << 16);
    r5 = (int64_t)(int32_t)(0x50);
    r4 = r4 + (int64_t)(0x5100);
    r6 = (int64_t)(int32_t)(0x10);
    r7 = (int64_t)(int32_t)(0x30);
    r8 = (int64_t)(int32_t)(0x40);
    r9 = (int64_t)(int32_t)(0x20);
    v6 = lvx(r5 + r1);
    r5 = (int64_t)(int32_t)(0x10);
    v7 = lvx(r6 + r4);
    v8 = lvx(r7 + r1);
    r6 = (int64_t)(int32_t)(0x20);
    v2 = vperm(v5, v2, v7);
    r7 = (int64_t)(int32_t)(0x30);
    v3 = vperm(v3, v4, v7);
    v4 = lvx(r8 + r1);
    v5 = lvx(r9 + r4);
    stvx(r3, v8);
    v2 = vperm(v2, v3, v5);
    stvx(r5 + r3, v4);
    stvx(r6 + r3, v6);
    stvx(r7 + r3, v2);
}

/* ---- 0x000252B0: global matrix 0x0047A300 *= matrix(quaternion r3, translation r4) */
bool matrix_native(ppu_context* c)
{
    const uint64_t r1 = c->gpr[1] - 0x180;
    quat_matrix(r1 + 0x70, c->gpr[3], c->gpr[4], r1);
    uint64_t r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r30;
    const uint64_t r31 = 0x000252A0u;
    V v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17, v18, v19,
      v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31;
    r3 = (int64_t)(int32_t)((uint32_t)0x48 << 16);
    r6 = (int64_t)(int32_t)(0x74);
    v11 = vsplatw((uint32_t)(int32_t)(-1));
    r4 = r3 + (int64_t)(-23808);
    v13 = vsplatw((uint32_t)(int32_t)(-1));
    r8 = (int64_t)(int32_t)(-23808);
    v15 = vsplatw((uint32_t)(int32_t)(-1));
    r5 = r4 + (int64_t)(0x10);
    v18 = vsplatw((uint32_t)(int32_t)(-1));
    r10 = (int64_t)(int32_t)(0x70);
    v11 = vslw(v11, v11);
    r5 = ppc_rldicl(r5, 0, 32);
    v13 = vslw(v13, v13);
    r7 = r4 + (int64_t)(0x30);
    v15 = vslw(v15, v15);
    r9 = (int64_t)(int32_t)(4);
    v18 = vslw(v18, v18);
    r11 = (int64_t)(int32_t)(8);
    v2 = vsplatw((uint32_t)(int32_t)(-1));
    r7 = ppc_rldicl(r7, 0, 32);
    v3 = vsplatw((uint32_t)(int32_t)(-1));
    v10 = lvlx(r5);
    r12 = (int64_t)(int32_t)(0xC);
    v12 = lvlx((r6 + r1));
    r30 = (int64_t)(int32_t)(0x84);
    v0 = lvlx((r8 + r3));
    r6 = r4 + (int64_t)(0x20);
    v1 = lvlx((r10 + r1));
    v14 = lvlx((r9 + r5));
    v16 = lvlx((r11 + r5));
    r6 = ppc_rldicl(r6, 0, 32);
    v17 = lvlx(r7);
    r10 = (int64_t)(int32_t)(0x78);
    v19 = lvlx((r9 + r4));
    v31 = lvlx((r12 + r5));
    v11 = vmaddfp(v10, v11, v12);
    v4 = vsplatw((uint32_t)(int32_t)(-1));
    v13 = vmaddfp(v14, v13, v12);
    v5 = vsplatw((uint32_t)(int32_t)(-1));
    v15 = vmaddfp(v16, v15, v12);
    v2 = vslw(v2, v2);
    v12 = vmaddfp(v31, v18, v12);
    v3 = vslw(v3, v3);
    v30 = lvlx((r9 + r7));
    r5 = (int64_t)(int32_t)(0x7C);
    v29 = lvlx(r6);
    v28 = lvlx((r10 + r1));
    v27 = lvlx((r11 + r4));
    r10 = (int64_t)(int32_t)(0x80);
    v18 = lvlx((r30 + r1));
    v26 = lvlx((r11 + r7));
    v25 = lvlx((r12 + r4));
    v4 = vslw(v4, v4);
    v5 = vslw(v5, v5);
    v11 = vmaddfp(v0, v11, v1);
    v13 = vmaddfp(v19, v13, v1);
    v7 = vsplatw((uint32_t)(int32_t)(-1));
    v15 = vmaddfp(v27, v15, v1);
    r4 = (int64_t)(int32_t)(0x94);
    v12 = vmaddfp(v25, v12, v1);
    v6 = vsplatw((uint32_t)(int32_t)(-1));
    v2 = vmaddfp(v10, v2, v18);
    v8 = vsplatw((uint32_t)(int32_t)(-1));
    v3 = vmaddfp(v14, v3, v18);
    v7 = vslw(v7, v7);
    v4 = vmaddfp(v16, v4, v18);
    v9 = vsplatw((uint32_t)(int32_t)(-1));
    v5 = vmaddfp(v31, v5, v18);
    v6 = vslw(v6, v6);
    v24 = lvlx((r9 + r6));
    v23 = lvlx((r12 + r7));
    v22 = lvlx((r5 + r1));
    r7 = (int64_t)(int32_t)(0x90);
    v1 = lvlx((r10 + r1));
    r5 = (int64_t)(int32_t)(0x88);
    v21 = lvlx((r11 + r6));
    v18 = lvlx((r12 + r6));
    v11 = vmaddfp(v29, v11, v28);
    v8 = vslw(v8, v8);
    v13 = vmaddfp(v24, v13, v28);
    v9 = vslw(v9, v9);
    v15 = vmaddfp(v21, v15, v28);
    r6 = (int64_t)(int32_t)(0x8C);
    v12 = vmaddfp(v18, v12, v28);
    v28 = lvlx((r4 + r1));
    v2 = vmaddfp(v0, v2, v1);
    r4 = (int64_t)(int32_t)(0x98);
    v3 = vmaddfp(v19, v3, v1);
    v4 = vmaddfp(v27, v4, v1);
    v5 = vmaddfp(v25, v5, v1);
    v7 = vmaddfp(v14, v7, v28);
    v6 = vmaddfp(v10, v6, v28);
    v8 = vmaddfp(v16, v8, v28);
    v9 = vmaddfp(v31, v9, v28);
    v20 = lvlx((r5 + r1));
    v1 = lvlx((r7 + r1));
    r5 = (int64_t)(int32_t)(0xA0);
    v11 = vmaddfp(v17, v11, v22);
    r7 = (int64_t)(int32_t)(0xA8);
    v13 = vmaddfp(v30, v13, v22);
    v2 = vmaddfp(v29, v2, v20);
    v3 = vmaddfp(v24, v3, v20);
    v4 = vmaddfp(v21, v4, v20);
    v5 = vmaddfp(v18, v5, v20);
    v7 = vmaddfp(v19, v7, v1);
    v6 = vmaddfp(v0, v6, v1);
    v15 = vmaddfp(v26, v15, v22);
    v12 = vmaddfp(v23, v12, v22);
    v28 = lvlx((r6 + r1));
    v8 = vmaddfp(v27, v8, v1);
    r6 = (int64_t)(int32_t)(0x9C);
    v9 = vmaddfp(v25, v9, v1);
    v22 = lvlx((r4 + r1));
    v1 = lvlx((r5 + r1));
    r4 = (int64_t)(int32_t)(0xA4);
    v2 = vmaddfp(v17, v2, v28);
    v11 = vmrghw(v11, v13);
    v3 = vmaddfp(v30, v3, v28);
    v4 = vmaddfp(v26, v4, v28);
    v5 = vmaddfp(v23, v5, v28);
    v7 = vmaddfp(v24, v7, v22);
    v0 = vmaddfp(v0, v17, v1);
    v19 = vmaddfp(v19, v30, v1);
    v28 = vmaddfp(v27, v26, v1);
    v12 = vmrghw(v15, v12);
    v1 = vmaddfp(v25, v23, v1);
    r5 = (int64_t)(int32_t)(-23792);
    v6 = vmaddfp(v29, v6, v22);
    v8 = vmaddfp(v21, v8, v22);
    v9 = vmaddfp(v18, v9, v22);
    v27 = lvlx((r6 + r1));
    v25 = lvlx((r7 + r1));
    r6 = (int64_t)(int32_t)((uint32_t)0x48 << 16);
    v2 = vmrghw(v2, v3);
    r7 = (int64_t)(int32_t)(-23776);
    v4 = vmrghw(v4, v5);
    v7 = vmaddfp(v30, v7, v27);
    v0 = vmaddfp(v29, v0, v25);
    r9 = (int64_t)(int32_t)((uint32_t)0x48 << 16);
    v19 = vmaddfp(v24, v19, v25);
    v30 = vmaddfp(v21, v28, v25);
    v1 = vmaddfp(v18, v1, v25);
    v6 = vmaddfp(v17, v6, v27);
    v8 = vmaddfp(v26, v8, v27);
    v9 = vmaddfp(v23, v9, v27);
    v17 = lvlx((r4 + r1));
    v3 = lvx(r31);
    r4 = (int64_t)(int32_t)((uint32_t)0x48 << 16);
    v2 = vperm(v2, v4, v3);
    v10 = vmaddfp(v10, v0, v17);
    v13 = vmaddfp(v14, v19, v17);
    v0 = vmaddfp(v16, v30, v17);
    v1 = vmaddfp(v31, v1, v17);
    v5 = vmrghw(v6, v7);
    v7 = vperm(v11, v12, v3);
    v6 = vmrghw(v8, v9);
    v4 = vperm(v5, v6, v3);
    stvx(r8 + r3, v7);
    stvx(r5 + r4, v2);
    v5 = vmrghw(v10, v13);
    r3 = (int64_t)(int32_t)(-23760);
    v6 = vmrghw(v0, v1);
    stvx(r7 + r6, v4);
    v2 = vperm(v5, v6, v3);
    stvx(r3 + r9, v2);
    return true;
}

/* ---- 0x0001F1E8: r3 = M(0x0047A300) * (x, y, z, 1) at r4 ------------------- */
bool point_native(ppu_context* c)
{
    const uint64_t r3 = c->gpr[3], r4 = c->gpr[4];
    uint64_t r5, r6;
    double f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12;
    r5 = (int64_t)(int32_t)((uint32_t)0x48 << 16);
    f1 = lfs(r4 + 0x4);
    r6 = r5 + (int64_t)(-23808);
    f2 = lfs(r6 + 0x10);
    f3 = lfs(r6 + 0x14);
    f4 = lfs(r4 + 0x0);
    f5 = lfs(r5 + -0x5D00);
    f2 = ppu_fp_single(ppu_fmul(f1, f2));
    f6 = lfs(r6 + 0x18);
    f3 = ppu_fp_single(ppu_fmul(f1, f3));
    f7 = lfs(r6 + 0x4);
    f8 = lfs(r6 + 0x1C);
    f6 = ppu_fp_single(ppu_fmul(f1, f6));
    f9 = lfs(r6 + 0x8);
    f1 = ppu_fp_single(ppu_fmul(f1, f8));
    f8 = lfs(r4 + 0x8);
    f2 = ppu_fp_single(ppu_fmadd_core(f4, f5, f2, 0, 0));
    f5 = lfs(r6 + 0x20);
    f3 = ppu_fp_single(ppu_fmadd_core(f4, f7, f3, 0, 0));
    f7 = lfs(r6 + 0xC);
    f6 = ppu_fp_single(ppu_fmadd_core(f4, f9, f6, 0, 0));
    f10 = lfs(r6 + 0x24);
    f1 = ppu_fp_single(ppu_fmadd_core(f4, f7, f1, 0, 0));
    f11 = lfs(r6 + 0x28);
    f9 = lfs(r6 + 0x30);
    f12 = lfs(r6 + 0x2C);
    f4 = lfs(r6 + 0x34);
    f2 = ppu_fp_single(ppu_fmadd_core(f8, f5, f2, 0, 0));
    f5 = lfs(r6 + 0x38);
    f3 = ppu_fp_single(ppu_fmadd_core(f8, f10, f3, 0, 0));
    f7 = lfs(r6 + 0x3C);
    f6 = ppu_fp_single(ppu_fmadd_core(f8, f11, f6, 0, 0));
    f1 = ppu_fp_single(ppu_fmadd_core(f8, f12, f1, 0, 0));
    f2 = ppu_fp_single(ppu_fadd(f2, f9));
    f3 = ppu_fp_single(ppu_fadd(f3, f4));
    f4 = ppu_fp_single(ppu_fadd(f6, f5));
    f1 = ppu_fp_single(ppu_fadd(f1, f7));
    stfs(r3 + 0x0, f2);
    stfs(r3 + 0x4, f3);
    stfs(r3 + 0x8, f4);
    stfs(r3 + 0xC, f1);
    return true;
}

/* ---- 0x002B6E00: cellGcmAddressToOffset(r3 = ea, r4 = guest u32*) -----------
 * What ps3_hle_call does for it, minus the per-call diagnostics: the caller's
 * TOC kept in r2 and in its ABI slot 0x28(r1), pending vblank/flip delivered,
 * the handler's 32-bit result zero-extended into r3. */
bool a2o_native(ppu_context* c)
{
    const uint64_t toc = c->gpr[2];
    w64(c->gpr[1] + 0x28, toc);
    if (ppu_gcm_pump) ppu_gcm_pump();
    const int32_t r = cellGcmAddressToOffset((uint32_t)c->gpr[3], (uint32_t*)(uintptr_t)c->gpr[4]);
    c->gpr[3] = (uint32_t)r;
    c->gpr[2] = toc;
    return true;
}

/* ---- small hot helpers (step 2) ---------------------------------------------
 * Same operations, same order and same helpers as the lift. Scratch registers
 * the lift leaves behind (volatile in the PPC ABI) are not reproduced, nor the
 * stack bytes below the caller's r1. */
inline double fs_mul(double a, double b) { return ppu_fp_single(ppu_fmul(a, b)); }
inline double fs_madd(double a, double c, double b) { return ppu_fp_single(ppu_fmadd_core(a, c, b, 0, 0)); }
inline double fs_msub(double a, double c, double b) { return ppu_fp_single(ppu_fmadd_core(a, c, b, 1, 0)); }
const uint32_t kM = 0x0047A300u;   // the global matrix

/* 0x0001EEF8 body: out = rotation part of M times v, out.w = v.w. */
void rot_vec(uint64_t r3, uint64_t r4)
{
    double f1, f2, f3, f4, f5, f6, f7, f8, f9;
    f1 = lfs(r4 + 0x4);
    f2 = lfs(kM + 0x10);
    f3 = lfs(kM + 0x14);
    f4 = lfs(r4 + 0x0);
    f5 = lfs(kM + 0x0);
    f2 = fs_mul(f1, f2);
    f6 = lfs(kM + 0x18);
    f3 = fs_mul(f1, f3);
    f7 = lfs(kM + 0x4);
    f1 = fs_mul(f1, f6);
    f6 = lfs(kM + 0x8);
    f8 = lfs(r4 + 0x8);
    f9 = lfs(kM + 0x20);
    f2 = fs_madd(f4, f5, f2);
    f3 = fs_madd(f4, f7, f3);
    f5 = lfs(kM + 0x24);
    f7 = lfs(kM + 0x28);
    f1 = fs_madd(f4, f6, f1);
    f2 = fs_madd(f8, f9, f2);
    f3 = fs_madd(f8, f5, f3);
    f1 = fs_madd(f8, f7, f1);
    stfs(r3 + 0x0, f2);
    stfs(r3 + 0x4, f3);
    stfs(r3 + 0x8, f1);
    f1 = lfs(r4 + 0xC);
    stfs(r3 + 0xC, f1);
}

/* 0x000297B8 body: a.xyz += b.xyz. */
void add3(uint64_t r3, uint64_t r4)
{
    double f1, f2, f3;
    f1 = lfs(r3 + 0x0);
    f2 = lfs(r4 + 0x0);
    f1 = ppu_fp_single(ppu_fadd(f1, f2));
    f2 = lfs(r3 + 0x4);
    f3 = lfs(r3 + 0x8);
    stfs(r3 + 0x0, f1);
    f1 = lfs(r4 + 0x4);
    f1 = ppu_fp_single(ppu_fadd(f2, f1));
    stfs(r3 + 0x4, f1);
    f1 = lfs(r4 + 0x8);
    f1 = ppu_fp_single(ppu_fadd(f3, f1));
    stfs(r3 + 0x8, f1);
}

/* ---- 0x0001EEF8: r3 = rot(M) * v(r4) --------------------------------------- */
bool rotvec_native(ppu_context* c) { rot_vec(c->gpr[3], c->gpr[4]); return true; }

/* ---- 0x0001F164: scale the rows of M by x, y, z at r3 ----------------------- */
bool scale_native(ppu_context* c)
{
    const uint64_t r3 = c->gpr[3];
    double f1, f2, f3, f4, f5, f6, f7, f8, f9, f10;
    f1 = lfs(r3 + 0x0);
    f2 = lfs(r3 + 0x4);
    f3 = lfs(r3 + 0x8);
    f4 = lfs(kM + 0x0);
    f5 = lfs(kM + 0x4);
    f4 = fs_mul(f4, f1);
    f6 = lfs(kM + 0x8);
    f5 = fs_mul(f5, f1);
    f7 = lfs(kM + 0x10);
    f1 = fs_mul(f6, f1);
    f6 = lfs(kM + 0x14);
    f8 = lfs(kM + 0x18);
    f7 = fs_mul(f7, f2);
    f9 = lfs(kM + 0x20);
    f6 = fs_mul(f6, f2);
    f2 = fs_mul(f8, f2);
    f8 = lfs(kM + 0x24);
    f9 = fs_mul(f9, f3);
    f10 = lfs(kM + 0x28);
    f8 = fs_mul(f8, f3);
    f3 = fs_mul(f10, f3);
    stfs(kM + 0x0, f4);
    stfs(kM + 0x4, f5);
    stfs(kM + 0x8, f1);
    stfs(kM + 0x10, f7);
    stfs(kM + 0x14, f6);
    stfs(kM + 0x18, f2);
    stfs(kM + 0x20, f9);
    stfs(kM + 0x24, f8);
    stfs(kM + 0x28, f3);
    c->gpr[4] = 0x00480000u;
    c->gpr[5] = kM;
    return true;
}

/* ---- 0x0001EF6C: translate M by rot(M) * (x, y, z) at r3 --------------------
 * The lift builds (0, 0, 0, 1) at 0x70(r1 - 0x90), turns it with 0x0001EEF8
 * (in place of r3's vector: r3 = temp, r4 = r3) and adds it to M's row 3 with
 * 0x000297B8. */
bool translate_native(ppu_context* c)
{
    const uint64_t tmp = c->gpr[1] - 0x90 + 0x70;
    w32(tmp + 0x0, 0); w32(tmp + 0x4, 0); w32(tmp + 0x8, 0); w32(tmp + 0xC, 0x3F800000u);
    rot_vec(tmp, c->gpr[3]);
    add3(kM + 0x30, tmp);
    c->gpr[3] = kM + 0x30;
    c->gpr[4] = tmp;
    return true;
}

/* ---- 0x000297B8: r3.xyz += r4.xyz ------------------------------------------- */
bool add3_native(ppu_context* c) { add3(c->gpr[3], c->gpr[4]); return true; }

/* ---- 0x000299AC: f1 = |v| at r3 --------------------------------------------- */
bool len3_native(ppu_context* c)
{
    const uint64_t r3 = c->gpr[3];
    double f1 = lfs(r3 + 0x4);
    const double f2 = lfs(r3 + 0x0);
    f1 = fs_mul(f1, f1);
    const double f3 = lfs(r3 + 0x8);
    f1 = fs_madd(f2, f2, f1);
    f1 = fs_madd(f3, f3, f1);
    c->fpr[1] = (double)(float)sqrt(f1);
    return true;
}

/* ---- 0x000357A0: sphere (point r4, radius f1) against the 6 planes at
 * r3 + 0xA50 (double nx, ny, nz, d; 0x20 each). r3 = 1 when it is outside one
 * of them, else 0. Constants at 0x00035790 (eps) and 0x00035798. */
bool planes_native(ppu_context* c)
{
    const double f1 = c->fpr[1];
    const uint64_t r4 = c->gpr[4];
    uint64_t r3 = c->gpr[3];
    const uint64_t r6 = 0x00035790u;
    const double f2 = lfd(r6 + 0x0);
    for (int i = 0; i < 6; i++, r3 += 0x20) {
        const uint64_t r8 = (r3 + 0xA50) & 0xFFFFFFFFull;
        double f5 = lfd(r8 + 0x8);
        const double f6 = lfd(r8 + 0x0);
        double f3 = ppu_fmul(f5, f5);
        double f4 = lfd(r8 + 0x10);
        double f7 = lfd(r6 + 0x8);
        f3 = ppu_fmadd_core(f6, f6, f3, 0, 0);
        f3 = ppu_fmadd_core(f4, f4, f3, 0, 0);
        if (!(f3 == f2)) {                          // fcmpu + beq
            f3 = ppu_frsp(f3);
            f7 = lfs(r4 + 0x4);
            f5 = ppu_fmul(f5, f7);
            const double f8 = lfs(r4 + 0x0);
            const double f9 = lfs(r4 + 0x8);
            const double f10 = lfd(r8 + 0x18);
            f7 = (double)(float)sqrt(f3);
            f5 = ppu_fmadd_core(f6, f8, f5, 0, 0);
            f4 = ppu_fmadd_core(f4, f9, f5, 0, 0);
            f3 = ppu_fadd(f4, f10);
            f7 = ppu_fdiv(f3, f7);
        }
        f3 = ppu_fsub(f7, f1);
        if (!(f3 < f2 || f3 == f2)) {               // cror lt|eq, unordered -> 1
            c->gpr[3] = 1;
            return true;
        }
    }
    c->gpr[3] = 0;
    return true;
}

/* ---- 0x00024F58: r3 = (x, y, z, w) from three 16-bit angles r4, r5, r6
 * (scale at 0x00024F4C), six sin/cos through 0x002A733C. */
bool euler_native(ppu_context* c)
{
    const uint64_t out = c->gpr[3], lr = c->lr;
    const double f1i = (double)(int64_t)(int16_t)c->gpr[4];
    const double f4i = (double)(int64_t)(int16_t)c->gpr[5];
    const double f6i = (double)(int64_t)(int16_t)c->gpr[6];
    const uint64_t k = 0x00024F4Cu;
    double f3 = lfs(k + 0x0), f2 = lfs(k + 0x4), f5 = lfs(k + 0x8);
    double f1 = ppu_frsp(f1i);
    f1 = fs_mul(f1, f3);
    f1 = fs_mul(f1, f2);
    double f4 = ppu_frsp(f4i);
    double f6 = ppu_frsp(f6i);
    const double a = fs_mul(f1, f5);
    f4 = fs_mul(f4, f3);
    f3 = fs_mul(f6, f3);
    f4 = fs_mul(f4, f2);
    f2 = fs_mul(f3, f2);
    const double b = fs_mul(f4, f5);
    const double cc = fs_mul(f2, f5);
    auto sc = [&](double x, int cosine) {
        c->fpr[1] = x;
        c->gpr[4] = (uint64_t)cosine;
        func_002A733C(c);
        return c->fpr[1];
    };
    const double sa = sc(a, 0), sb = sc(b, 0), sg = sc(cc, 0);
    const double ca = sc(a, 1), cb = sc(b, 1), cg = sc(cc, 1);
    double g2 = fs_mul(sb, sg);
    double g3 = fs_mul(ca, sb);
    double g4 = fs_mul(sa, sb);
    const double g5 = fs_mul(cb, cg);
    const double g6 = fs_mul(cb, sa);
    const double g7 = fs_mul(ca, cb);
    const double g8 = fs_mul(ca, g2);
    g3 = fs_mul(g3, cg);
    double g1 = fs_mul(g4, cg);
    g2 = fs_mul(sa, g2);
    g4 = fs_msub(g5, sa, g8);
    g3 = fs_madd(g6, sg, g3);
    g1 = fs_msub(g7, sg, g1);
    g2 = fs_madd(ca, g5, g2);
    stfs(out + 0x0, g4);
    stfs(out + 0x4, g3);
    stfs(out + 0x8, g1);
    stfs(out + 0xC, g2);
    c->lr = lr;
    return true;
}

/* ---- 0x002993A0: memset(r3, r4, r5), r3 kept ---------------------------------
 * Only plain guest RAM with no SPU reservation on any of its 128-byte lines
 * (the lift's lf_reserved test); anything else goes to the lift. */
inline bool nm_plain(uint32_t a, uint32_t n)
{
    const uint64_t e = (uint64_t)a + n;
    return (a >= 0x10000u && e <= 0x20000000u) || (a >= 0x30000000u && e <= 0xDFFFFFF8u);
}
/* Why memset fell back to the lift (verify log only; racy counters). */
uint64_t s_ms_why[3];                       // fast VM off, not plain RAM, reserved line
uint32_t s_ms_a[3], s_ms_n[3];
inline bool ms_refuse(int why, uint32_t a, uint32_t n)
{
    s_ms_why[why]++; s_ms_a[why] = a; s_ms_n[why] = n;
    return false;
}
bool memset_native(ppu_context* c)
{
    const uint32_t a = (uint32_t)c->gpr[3], n = (uint32_t)c->gpr[5];
    if (!n) return true;
    if (!nm_fast_vm) return ms_refuse(0, a, n);
    if (!nm_plain(a, n)) return ms_refuse(1, a, n);
    if (*nm_coh_armed) {                    // a line an SPU holds a reservation on: the lift
        for (uint32_t l = a >> 7, l1 = (a + n - 1u) >> 7; l <= l1; l++)
            if ((nm_coh_bitmap[l >> 3] >> (l & 7)) & 1u) return ms_refuse(2, a, n);
    }
    dbz3_store_hook(a, n);
    memset(vm_base + a, (int)(c->gpr[4] & 0xFFu), n);
    return true;
}

/* ---- step 3: draw-state writers --------------------------------------------
 * A state object (r3) keeps flags at +0x20, a byte counter at +0x1C, a draw
 * counter at +0x18 and its command list at +0x4. 0x0004D1A8 picks the list's
 * current entry (list + 0x28 + 12 * index); the entry's word at +8 is the write
 * cursor. Stack bytes below the caller's r1 (register saves) are not written,
 * except the frames the virtual calls run under. */
inline uint8_t r8u(uint64_t a) { return vm_base[(uint32_t)a]; }
inline void w8(uint64_t ea, uint32_t v)
{
    const uint32_t a = (uint32_t)ea;
    dbz3_store_hook(a, 1u);
    vm_base[a] = (uint8_t)v;
}
inline uint64_t gcm_entry(uint64_t list)          // 0x0004D1A8
{
    const uint64_t idx = r32((uint32_t)list + 0x10u);
    return (list + idx * 12u + 0x28u) & 0xFFFFFFFFull;
}

/* 0x0004A39C: 4 bytes from r4 into the list, flag 0x10, counter + 4. */
bool put4_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3], src = c->gpr[4];
    uint64_t r5 = r32((uint32_t)obj + 0x20u);
    const uint64_t list = r32((uint32_t)obj + 0x4u);
    if (!(r5 & 0x10u)) {
        const uint64_t r6 = r8u(obj + 0x1C);
        r5 |= 0x10u;
        w32(obj + 0x20, (uint32_t)r5);
        w8(obj + 0x1C, (uint32_t)(r6 + 4u));
    }
    const uint64_t e = gcm_entry(list);
    const uint64_t p = r32((uint32_t)e + 8u);
    const uint8_t b0 = r8u(src + 0), b1 = r8u(src + 1), b2 = r8u(src + 2), b3 = r8u(src + 3);
    w8(p + 0, b0); w8(p + 1, b1); w8(p + 2, b2); w8(p + 3, b3);
    w32(e + 8, (uint32_t)(r32((uint32_t)e + 8u) + 4u));
    c->gpr[3] = e;
    return true;
}

/* 0x0004A428: the single-precision bits of f1 and f2 into the list, flag 0x20,
 * counter + 8. */
bool put2f_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3];
    const float x = (float)c->fpr[1], y = (float)c->fpr[2];
    uint32_t xb, yb; memcpy(&xb, &x, 4); memcpy(&yb, &y, 4);
    uint64_t r5 = r32((uint32_t)obj + 0x20u);
    const uint64_t list = r32((uint32_t)obj + 0x4u);
    if (!(r5 & 0x20u)) {
        const uint64_t r6 = r8u(obj + 0x1C);
        r5 |= 0x20u;
        w32(obj + 0x20, (uint32_t)r5);
        w8(obj + 0x1C, (uint32_t)(r6 + 8u));
    }
    const uint64_t e = gcm_entry(list);
    const uint64_t p = r32((uint32_t)e + 8u);
    w8(p + 0, xb >> 24); w8(p + 1, xb >> 16); w8(p + 2, xb >> 8); w8(p + 3, xb);
    w8(p + 4, yb >> 24); w8(p + 5, yb >> 16); w8(p + 6, yb >> 8); w8(p + 7, yb);
    w32(e + 8, (uint32_t)(r32((uint32_t)e + 8u) + 8u));
    c->gpr[3] = e;
    return true;
}

/* 0x0004A300: 12 bytes from r4 into the list (through the title's memcpy),
 * flag 0x1, counter + 12, draw counter + 1. */
bool put12_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3], src = c->gpr[4], lr = c->lr;
    uint64_t r4 = r32((uint32_t)obj + 0x20u);
    const uint64_t list = r32((uint32_t)obj + 0x4u);
    if (!(r4 & 0x1u)) {
        r4 |= 0x1u;
        const uint64_t r5 = r8u(obj + 0x1C);
        w32(obj + 0x20, (uint32_t)r4);
        w8(obj + 0x1C, (uint32_t)(r5 + 0xCu));
    }
    const uint64_t e = gcm_entry(list);
    c->gpr[3] = r32((uint32_t)e + 8u);
    c->gpr[4] = src;
    c->gpr[5] = 0xC;
    c->lr = 0x0004A364u;
    func_00292628(c); DRAIN_TRAMPOLINE(c);
    w32(e + 8, (uint32_t)(r32((uint32_t)e + 8u) + 0xCu));
    const uint64_t n = (uint64_t)r32((uint32_t)obj + 0x18u) + 1u;
    w32(obj + 0x18, (uint32_t)n);
    c->gpr[3] = n;
    c->lr = lr;
    return true;
}

/* Guest frame for natives that call other guest code: back chain at the new
 * r1, LR at old r1 + 0x10, as the lift's prologue. */
struct GuestFrame {
    ppu_context* c; uint64_t size, lr;
    GuestFrame(ppu_context* ctx, uint64_t sz) : c(ctx), size(sz), lr(ctx->lr)
    {
        w64(c->gpr[1] - size, c->gpr[1]);
        c->gpr[1] -= size;
        w64(c->gpr[1] + size + 0x10, lr);
    }
    ~GuestFrame() { c->gpr[1] += size; c->lr = lr; }
};
/* bctrl through the function descriptor at opd, TOC saved at 0x28(r1). */
inline void call_opd(ppu_context* c, uint64_t opd, int code_reg)
{
    w64(c->gpr[1] + 0x28, c->gpr[2]);
    const uint64_t code = r32((uint32_t)opd);
    c->gpr[code_reg] = code;
    c->gpr[2] = r32((uint32_t)opd + 4u);
    c->ctr = (uint32_t)code;
    ps3_indirect_call(c); DRAIN_TRAMPOLINE(c);
    c->gpr[2] = r64((uint32_t)c->gpr[1] + 0x28u);
}

/* 0x0004A118: obj->vt[0x24](obj, r4), vt[0x28](obj, r5), vt[0x2C](obj, f1, f2). */
bool vcall3_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3], a5 = c->gpr[5];
    const double x = c->fpr[1], y = c->fpr[2];
    GuestFrame fr(c, 0x90);
    uint64_t opd = r32(r32((uint32_t)obj) + 0x24u);
    c->gpr[5] = opd;
    call_opd(c, opd, 6);
    c->gpr[4] = a5;
    c->gpr[3] = obj;
    opd = r32(r32((uint32_t)obj) + 0x28u);
    c->gpr[5] = opd;
    call_opd(c, opd, 6);
    c->gpr[3] = obj;
    c->fpr[1] = x;
    c->fpr[2] = y;
    opd = r32(r32((uint32_t)obj) + 0x2Cu);
    c->gpr[4] = opd;
    call_opd(c, opd, 5);
    return true;
}

/* 0x0004E100: transpose the 4x4 matrix at r4 into obj + 0x190 and pass it on
 * with obj->vt[0xAC](obj, 8, 0x10, obj + 0x190). Word moves on the guest's
 * bytes, as the lift's vmrghw/vmrglw. */
bool transpose_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3], m = c->gpr[4];
    uint32_t A[4], B[4], C[4], D[4];
    memcpy(A, vm_base + (uint32_t)(m & ~0xFull), 16);
    memcpy(B, vm_base + (uint32_t)(((m + 0x20) & 0xFFFFFFFFull) & ~0xFull), 16);
    memcpy(C, vm_base + (uint32_t)(((m + 0x10) & 0xFFFFFFFFull) & ~0xFull), 16);
    memcpy(D, vm_base + (uint32_t)(((m + 0x30) & 0xFFFFFFFFull) & ~0xFull), 16);
    const uint32_t c0[4] = { A[0], C[0], B[0], D[0] }, c1[4] = { A[1], C[1], B[1], D[1] };
    const uint32_t c2[4] = { A[2], C[2], B[2], D[2] }, c3[4] = { A[3], C[3], B[3], D[3] };
    auto st = [&](uint64_t ea, const uint32_t* v) {
        const uint32_t a = (uint32_t)(ea & ~0xFull);
        dbz3_store_hook(a, 16u);
        memcpy(vm_base + a, v, 16);
    };
    st(obj + 0x190, c0);
    st(obj + 0x1A0, c1);
    st(obj + 0x1B0, c2);
    st(obj + 0x1C0, c3);
    GuestFrame fr(c, 0x70);
    c->gpr[6] = (obj + 0x190) & 0xFFFFFFFFull;
    c->gpr[4] = 8;
    c->gpr[5] = 0x10;
    c->gpr[3] = obj;
    const uint64_t opd = r32(r32((uint32_t)obj) + 0xACu);
    c->gpr[7] = opd;
    call_opd(c, opd, 8);
    return true;
}

/* 0x0004EE80: if r5: offset = cellGcmAddressToOffset(r6), then
 * obj->vt[0x64](obj, offset, r7, r8) and obj->vt[0x6C](obj, r4, r5). */
bool vcall_a2o_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3], a4 = c->gpr[4], a5 = c->gpr[5];
    const uint64_t a6 = c->gpr[6], a7 = c->gpr[7], a8 = c->gpr[8];
    GuestFrame fr(c, 0xA0);
    if ((int32_t)a5 == 0) return true;
    const uint64_t slot = c->gpr[1] + 0x70;
    w32(slot, 0);
    c->gpr[5] = 0;
    c->gpr[4] = slot;
    c->gpr[3] = a6;
    c->lr = 0x0004EED0u;
    func_002B6E00(c); DRAIN_TRAMPOLINE(c);
    c->gpr[2] = r64((uint32_t)c->gpr[1] + 0x28u);
    c->gpr[3] = obj;
    c->gpr[4] = r32((uint32_t)slot);
    c->gpr[5] = a7;
    c->gpr[6] = a8;
    uint64_t opd = r32(r32((uint32_t)obj) + 0x64u);
    c->gpr[7] = opd;
    call_opd(c, opd, 8);
    c->gpr[3] = obj;
    c->gpr[4] = a4;
    c->gpr[5] = a5;
    opd = r32(r32((uint32_t)obj) + 0x6Cu);
    c->gpr[6] = opd;
    call_opd(c, opd, 7);
    return true;
}

/* ---- dispatch and verification --------------------------------------------- */
uint64_t guest_hash(uint32_t ea)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned i = 0; i < 8; ++i) {
        const uint32_t w = r32(ea + 4u * i);
        for (int s = 24; s >= 0; s -= 8) { h ^= (w >> s) & 0xFFu; h *= 0x100000001B3ull; }
    }
    return h;
}

bool sd_flag(const char* name)
{
    char path[128];
    snprintf(path, sizeof path, "sdmc:/switch/dbz-budokai-3-switch/%s", name);
    FILE* f = fopen(path, "r");
    if (f) fclose(f);
    return f != nullptr;
}

typedef bool (*NativeFn)(ppu_context*);
typedef void (*LiftedFn)(ppu_context*);
struct Stats { uint64_t calls, verified, mismatch, lifted; };
enum Result { kF1, kGlobalMatrix, kOut16, kA2O, kR3, kMemset, kList, kNone };   // what verify compares
/* kList: the state object's counters, the list cursor and the bytes written
 * (put*); kNone: calls other guest code, so it cannot run twice: native only. */
struct Entry {
    const char* name; const char* off_flag; NativeFn native; LiftedFn lifted;
    Result result;
    uint32_t hash_ea[4]; uint64_t hash[4];
    int mode;                        // 0 undecided, -1 lifted, 1 native, 2 native + verify
    Stats st;
};
Entry s_fn[18] = {
    { "sincos 0x002A733C", "native-sincos-off", sincos_native, func_002A733C_lifted, kF1,
      { 0x002A733Cu, 0x002A6EB4u }, { 0x898D1DFC4B4A86C9ull, 0x78F3A78B7569B3CFull }, 0, {} },
    { "matrix 0x000252B0", "native-matrix-off", matrix_native, func_000252B0_lifted, kGlobalMatrix,
      { 0x000252B0u, 0x00025130u }, { 0x7CB25F0F718BE53Eull, 0xA5E0778B0892EBB2ull }, 0, {} },
    { "point 0x0001F1E8", "native-point-off", point_native, func_0001F1E8_lifted, kOut16,
      { 0x0001F1E8u }, { 0x81671BA7F8B3AC3Cull }, 0, {} },
    { "a2o 0x002B6E00", "native-a2o-off", a2o_native, func_002B6E00_lifted, kA2O,
      { 0x002B6E00u }, { 0xD6807D1F048A6A5Eull }, 0, {} },
    { "rotvec 0x0001EEF8", "native-rotvec-off", rotvec_native, func_0001EEF8_lifted, kOut16,
      { 0x0001EEF8u, 0x0001EF18u, 0x0001EF38u },
      { 0x81671BA7F8B3AC3Cull, 0x602629EEA375C393ull, 0xD28333FEAA8CAC44ull }, 0, {} },
    { "scale 0x0001F164", "native-scale-off", scale_native, func_0001F164_lifted, kGlobalMatrix,
      { 0x0001F164u, 0x0001F184u, 0x0001F1A4u },
      { 0x205D623A964B1E11ull, 0x46D0DE0F6A011414ull, 0x7B2716D688CC1477ull }, 0, {} },
    { "translate 0x0001EF6C", "native-translate-off", translate_native, func_0001EF6C_lifted, kGlobalMatrix,
      { 0x0001EF6Cu, 0x0001EF8Cu, 0x0001EEF8u, 0x000297B8u },
      { 0xB3DB8508B80057F6ull, 0x8B8AC156B5E09EB6ull, 0x81671BA7F8B3AC3Cull, 0xFA2AB25AA3A71244ull }, 0, {} },
    { "add3 0x000297B8", "native-add3-off", add3_native, func_000297B8_lifted, kOut16,
      { 0x000297B8u, 0x000297D8u }, { 0xFA2AB25AA3A71244ull, 0xE73725E3D0C7E8FAull }, 0, {} },
    { "len3 0x000299AC", "native-len3-off", len3_native, func_000299AC_lifted, kF1,
      { 0x000299ACu }, { 0xBE142344A3BEFEA4ull }, 0, {} },
    { "planes 0x000357A0", "native-planes-off", planes_native, func_000357A0_lifted, kR3,
      { 0x000357A0u, 0x000357C0u, 0x000357E0u, 0x00035800u },
      { 0xF14D88A6C2936D37ull, 0x1C41A418CF51B42Full, 0xEF4869F0F7C12A36ull, 0x045F9A179C1E9EEFull }, 0, {} },
    { "euler 0x00024F58", "native-euler-off", euler_native, func_00024F58_lifted, kOut16,
      { 0x00024F58u, 0x00024F78u, 0x00024F98u },
      { 0x01022CA9668A9925ull, 0xE3674DA49AB162E8ull, 0xC38CDB276D671642ull }, 0, {} },
    { "memset 0x002993A0", "native-memset-off", memset_native, func_002993A0_lifted, kMemset,
      { 0x002993A0u, 0x002993C0u, 0x002993E0u, 0x00299400u },
      { 0x56097ED3056F6419ull, 0xA775869AC9B7D6B8ull, 0x96220E7BD226D798ull, 0xA94501583F0A1DAFull }, 0, {} },
    { "put4 0x0004A39C", "native-put4-off", put4_native, func_0004A39C_lifted, kList,
      { 0x0004A39Cu, 0x0004A3BCu, 0x0004D1A8u },
      { 0xA7396DFD6B6199A2ull, 0x8EBB24F8C1F9BB80ull, 0x0969DB452A789228ull }, 0, {} },
    { "put2f 0x0004A428", "native-put2f-off", put2f_native, func_0004A428_lifted, kList,
      { 0x0004A428u, 0x0004A448u, 0x0004A468u, 0x0004D1A8u },
      { 0x6EB69FAB7BA542BEull, 0x169B6330CECCA678ull, 0xDF9C9983A2FB73DEull, 0x0969DB452A789228ull }, 0, {} },
    { "put12 0x0004A300", "native-put12-off", put12_native, func_0004A300_lifted, kList,
      { 0x0004A300u, 0x0004A320u, 0x0004A340u, 0x0004D1A8u },
      { 0x0D83B46B9FA09784ull, 0xF95EE8E684BE8F4Bull, 0x6A3D7BC6FA3FF0DEull, 0x0969DB452A789228ull }, 0, {} },
    { "vcall3 0x0004A118", "native-vcall3-off", vcall3_native, func_0004A118_lifted, kNone,
      { 0x0004A118u, 0x0004A138u, 0x0004A158u },
      { 0x350FF8A6576A8B04ull, 0x0456CB6939FB1876ull, 0x31BED2ECC4DA1B8Bull }, 0, {} },
    { "transpose 0x0004E100", "native-transpose-off", transpose_native, func_0004E100_lifted, kNone,
      { 0x0004E100u, 0x0004E120u, 0x0004E140u },
      { 0x808B3E1CDD5E4C40ull, 0xF508F3B46F400454ull, 0x52BC41A7145C651Dull }, 0, {} },
    { "vcall-a2o 0x0004EE80", "native-vcall-a2o-off", vcall_a2o_native, func_0004EE80_lifted, kNone,
      { 0x0004EE80u, 0x0004EEA0u, 0x0004EEC0u },
      { 0x87F120F87D1B2AB7ull, 0x09061E51AE76FED6ull, 0x08595D12BEB9BF64ull }, 0, {} },
};
const uint32_t kMatrix = 0x0047A300u;

uint64_t s_window;
uint64_t s_vskip; uint32_t s_vskip_a, s_vskip_n;   // memsets verify could not check
FILE* s_log;
unsigned s_logged;

void log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char* fmt, ...)
{
    if (!s_log) {
        s_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t37-native-math.log", "w");
        if (!s_log) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(s_log, fmt, ap);
    va_end(ap);
    fflush(s_log);
}

void window_tick()
{
    const uint64_t now = armGetSystemTick();
    if (!s_window) { s_window = now; return; }
    if (armTicksToNs(now - s_window) < 5000000000ull) return;
    for (Entry& e : s_fn) {
        if (e.mode != 2) continue;
        log_line("window %s calls=%llu verified=%llu mismatch=%llu lifted_path=%llu\n", e.name,
                 (unsigned long long)e.st.calls, (unsigned long long)e.st.verified,
                 (unsigned long long)e.st.mismatch, (unsigned long long)e.st.lifted);
        e.st = Stats{};
    }
    log_line("memset refused: fastvm_off=%llu not_plain=%llu (last 0x%08X+%u) reserved=%llu (last 0x%08X+%u) "
             "verify_skip=%llu (last 0x%08X+%u)\n",
             (unsigned long long)s_ms_why[0], (unsigned long long)s_ms_why[1], s_ms_a[1], s_ms_n[1],
             (unsigned long long)s_ms_why[2], s_ms_a[2], s_ms_n[2],
             (unsigned long long)s_vskip, s_vskip_a, s_vskip_n);
    s_ms_why[0] = s_ms_why[1] = s_ms_why[2] = 0; s_vskip = 0;
    s_window = now;
}

void decide(Entry& e)
{
    bool ok = true;
    for (int h = 0; h < 4 && e.hash_ea[h]; h++) ok = ok && guest_hash(e.hash_ea[h]) == e.hash[h];
    const bool off = sd_flag(e.off_flag);
    const bool verify = sd_flag("native-verify");
    e.mode = (!ok || off) ? -1 : verify ? 2 : 1;
    fprintf(stderr, "[native-A] %s: %s\n", e.name,
            !ok ? "lifted (guest code does not match)" : off ? "lifted (switched off)" :
            verify ? "native + verify" : "native");
    if (e.mode == 2) log_line("%s: verify mode\n", e.name);
}

struct Regs { uint64_t gpr[32]; double fpr[32]; uint32_t cr, xer; uint64_t lr, ctr; };
void save_regs(const ppu_context* c, Regs& r)
{
    memcpy(r.gpr, c->gpr, sizeof r.gpr); memcpy(r.fpr, c->fpr, sizeof r.fpr);
    r.cr = c->cr; r.xer = c->xer; r.lr = c->lr; r.ctr = c->ctr;
}
void load_regs(ppu_context* c, const Regs& r)
{
    memcpy(c->gpr, r.gpr, sizeof r.gpr); memcpy(c->fpr, r.fpr, sizeof r.fpr);
    c->cr = r.cr; c->xer = r.xer; c->lr = r.lr; c->ctr = r.ctr;
}

/* The guest memory a function's result lives in (memsets above kVerifyMax
 * bytes run the lift unchecked). */
constexpr uint32_t kVerifyMax = 65536;
void result_region(const Entry& e, const Regs& r, uint32_t& addr, uint32_t& len)
{
    switch (e.result) {
    case kGlobalMatrix: addr = kMatrix; len = 64; break;
    case kOut16:  addr = (uint32_t)r.gpr[3]; len = 16; break;
    case kA2O:    addr = (uint32_t)r.gpr[4]; len = 4; break;
    case kMemset: addr = (uint32_t)r.gpr[3]; len = (uint32_t)r.gpr[5]; break;
    default:      addr = 0; len = 0; break;
    }
}

/* kList: obj + 0x18..0x23, the cursor word, and the 4/8/12 bytes at the cursor. */
void verify_list(ppu_context* c, Entry& e)
{
    const uint64_t obj = c->gpr[3];
    const uint32_t n = e.native == put4_native ? 4u : e.native == put2f_native ? 8u : 12u;
    const uint64_t ent = gcm_entry(r32((uint32_t)obj + 0x4u));
    const uint32_t cur = r32((uint32_t)ent + 8u);
    const uint32_t ra[3] = { (uint32_t)obj + 0x18u, (uint32_t)ent + 8u, cur };
    const uint32_t rn[3] = { 12u, 4u, n };
    for (int i = 0; i < 3; i++)
        if (ra[i] < 0x10000u || ra[i] >= 0xE0000000u) { e.st.lifted++; e.lifted(c); return; }
    uint8_t pre[3][16], a[3][16];
    for (int i = 0; i < 3; i++) memcpy(pre[i], vm_base + ra[i], rn[i]);
    Regs before; save_regs(c, before);
    e.lifted(c);
    Regs after; save_regs(c, after);
    for (int i = 0; i < 3; i++) memcpy(a[i], vm_base + ra[i], rn[i]);
    for (int i = 0; i < 3; i++) memcpy(vm_base + ra[i], pre[i], rn[i]);
    load_regs(c, before);
    if (!e.native(c)) {
        e.st.lifted++;
        load_regs(c, after);
        for (int i = 0; i < 3; i++) memcpy(vm_base + ra[i], a[i], rn[i]);
        return;
    }
    e.st.verified++;
    bool same = after.gpr[3] == c->gpr[3] && after.gpr[1] == c->gpr[1] && after.lr == c->lr;
    for (int i = 0; i < 3; i++) same = same && !memcmp(a[i], vm_base + ra[i], rn[i]);
    if (same) return;
    e.st.mismatch++;
    if (s_logged < 60) {
        s_logged++;
        log_line("MISMATCH %s obj=0x%08X cursor=0x%08X lifted r3=0x%08X native r3=0x%08X\n", e.name,
                 (uint32_t)obj, cur, (uint32_t)after.gpr[3], (uint32_t)c->gpr[3]);
        for (int i = 0; i < 3; i++)
            for (uint32_t o = 0; o < rn[i]; o++)
                if (a[i][o] != vm_base[ra[i] + o])
                    log_line("  mem 0x%08X lifted=%02X native=%02X\n", ra[i] + o, a[i][o],
                             vm_base[ra[i] + o]);
    }
    load_regs(c, after);
    for (int i = 0; i < 3; i++) memcpy(vm_base + ra[i], a[i], rn[i]);
}

void verify_call(ppu_context* c, Entry& e)
{
    if (e.result == kList) { verify_list(c, e); return; }
    if (e.result == kNone) { e.st.verified++; if (!e.native(c)) e.lifted(c); return; }
    Regs before; save_regs(c, before);
    uint32_t addr, len;
    result_region(e, before, addr, len);
    if (len && (addr < 0x10000u || addr >= 0xE0000000u || len > kVerifyMax)) {
        if (e.result == kMemset) { s_vskip++; s_vskip_a = addr; s_vskip_n = len; }
        e.st.lifted++; e.lifted(c); return;
    }
    uint8_t pre_s[64], a_s[64];
    uint8_t* pre = pre_s; uint8_t* a = a_s;
    struct Heap { uint8_t* p = nullptr; ~Heap() { free(p); } } heap;
    if (len > 64) {
        heap.p = (uint8_t*)malloc(2u * len);
        if (!heap.p) { e.st.lifted++; e.lifted(c); return; }
        pre = heap.p; a = heap.p + len;
    }
    if (len) memcpy(pre, vm_base + addr, len);
    e.lifted(c);
    Regs after; save_regs(c, after);
    if (len) memcpy(a, vm_base + addr, len);

    auto keep_lifted = [&]() {
        load_regs(c, after);
        if (len) memcpy(vm_base + addr, a, len);
    };
    if (len) memcpy(vm_base + addr, pre, len);
    load_regs(c, before);
    if (!e.native(c)) { e.st.lifted++; keep_lifted(); return; }
    e.st.verified++;
    bool same = !len || !memcmp(a, vm_base + addr, len);
    if (e.result == kF1) same = !memcmp(&after.fpr[1], &c->fpr[1], 8);
    if (e.result == kA2O) same = same && (uint32_t)after.gpr[3] == (uint32_t)c->gpr[3];
    if (e.result == kR3) same = after.gpr[3] == c->gpr[3];
    if (e.result == kGlobalMatrix || e.result == kOut16 || e.result == kMemset)
        same = same && after.gpr[1] == c->gpr[1] && after.lr == c->lr;
    if (same) return;
    e.st.mismatch++;
    if (s_logged < 60) {
        s_logged++;
        uint64_t x, ya, yb;
        memcpy(&x, &before.fpr[1], 8); memcpy(&ya, &after.fpr[1], 8); memcpy(&yb, &c->fpr[1], 8);
        log_line("MISMATCH %s r3=0x%08X r4=0x%08X f1=%016llX lifted f1=%016llX r3=0x%08X, "
                 "native f1=%016llX r3=0x%08X\n", e.name, (uint32_t)before.gpr[3],
                 (uint32_t)before.gpr[4], (unsigned long long)x, (unsigned long long)ya,
                 (uint32_t)after.gpr[3], (unsigned long long)yb, (uint32_t)c->gpr[3]);
        for (uint32_t o = 0; o + 4 <= len; o += 4) {
            uint32_t wa, wb;
            memcpy(&wa, a + o, 4); memcpy(&wb, vm_base + addr + o, 4);
            if (wa != wb)
                log_line("  mem 0x%08X lifted=%08X native=%08X\n", addr + o,
                         __builtin_bswap32(wa), __builtin_bswap32(wb));
        }
    }
    keep_lifted();
}

void dispatch(ppu_context* c, Entry& e)
{
    if (__builtin_expect(e.mode == 1, 1)) {
        if (!e.native(c)) e.lifted(c);
        return;
    }
    if (e.mode == 0) decide(e);
    if (e.mode == 1) { if (!e.native(c)) e.lifted(c); return; }
    if (e.mode < 0) { e.lifted(c); return; }
    e.st.calls++;
    verify_call(c, e);
    window_tick();
}

}  // namespace

void func_002A733C(ppu_context* ctx) { dispatch(ctx, s_fn[0]); }
void func_000252B0(ppu_context* ctx) { dispatch(ctx, s_fn[1]); }
void func_0001F1E8(ppu_context* ctx) { dispatch(ctx, s_fn[2]); }
void func_002B6E00(ppu_context* ctx) { dispatch(ctx, s_fn[3]); }
void func_0001EEF8(ppu_context* ctx) { dispatch(ctx, s_fn[4]); }
void func_0001F164(ppu_context* ctx) { dispatch(ctx, s_fn[5]); }
void func_0001EF6C(ppu_context* ctx) { dispatch(ctx, s_fn[6]); }
void func_000297B8(ppu_context* ctx) { dispatch(ctx, s_fn[7]); }
void func_000299AC(ppu_context* ctx) { dispatch(ctx, s_fn[8]); }
void func_000357A0(ppu_context* ctx) { dispatch(ctx, s_fn[9]); }
void func_00024F58(ppu_context* ctx) { dispatch(ctx, s_fn[10]); }
void func_002993A0(ppu_context* ctx) { dispatch(ctx, s_fn[11]); }
void func_0004A39C(ppu_context* ctx) { dispatch(ctx, s_fn[12]); }
void func_0004A428(ppu_context* ctx) { dispatch(ctx, s_fn[13]); }
void func_0004A300(ppu_context* ctx) { dispatch(ctx, s_fn[14]); }
void func_0004A118(ppu_context* ctx) { dispatch(ctx, s_fn[15]); }
void func_0004E100(ppu_context* ctx) { dispatch(ctx, s_fn[16]); }
void func_0004EE80(ppu_context* ctx) { dispatch(ctx, s_fn[17]); }
