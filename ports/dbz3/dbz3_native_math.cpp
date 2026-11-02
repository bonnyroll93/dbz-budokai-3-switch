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
// batch 2 (natives below) and the guest code they call
void func_0002C7D8_lifted(ppu_context* ctx);
void func_00031DC8_lifted(ppu_context* ctx);
void func_0002F56C_lifted(ppu_context* ctx);
void func_00030790_lifted(ppu_context* ctx);
void func_00030DF0_lifted(ppu_context* ctx);
void func_00031024_lifted(ppu_context* ctx);
void func_000207C0_lifted(ppu_context* ctx);
void func_0002C7D8(ppu_context* ctx);
void func_00030790(ppu_context* ctx);
void func_00024F58(ppu_context* ctx);
void func_00017464(ppu_context* ctx);
void func_0002BAE8(ppu_context* ctx);
void func_0002C088(ppu_context* ctx);
void func_00029734(ppu_context* ctx);
void func_000204D0(ppu_context* ctx);
void func_00020658(ppu_context* ctx);
void func_00026190(ppu_context* ctx);
void func_00029784(ppu_context* ctx);
void func_000298A0(ppu_context* ctx);
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

/* ---- batch 2: draw and animation helpers (run136 profile) --------------------
 * Same operations as the lift, in the same order, with the lift's helpers for
 * the scalar floating point (fmuls, fdivs, fmadds, frsp, fctiwz) and plain
 * float arithmetic for the AltiVec lanes (multiply, then add: no fusion, as the
 * lift's NEON rewrite). Vector results are only ever used through a splat of
 * element 0 or stored with stvewx from a splat, so element 0 is computed alone.
 * Calls to other guest code go through their func_ symbols with the registers
 * the lift sets; stack scratch the guest never reads back is not written. */
inline uint32_t r16(uint64_t a) { uint16_t v; memcpy(&v, vm_base + (uint32_t)a, 2); return __builtin_bswap16(v); }
inline void w16(uint64_t ea, uint32_t v)
{
    const uint32_t a = (uint32_t)ea;
    dbz3_store_hook(a, 2u);
    const uint16_t s = __builtin_bswap16((uint16_t)v);
    memcpy(vm_base + a, &s, 2);
}
/* Element 0 of the lift's lvlx: bytes ea.. up to the end of the 16-byte block,
 * zeros after. */
inline uint32_t lvlx_w0(uint64_t ea)
{
    const uint32_t a = (uint32_t)ea, sh = a & 0xFu;
    if (sh <= 12u) return r32(a);
    const uint8_t* m = vm_base + (a & ~0xFu);
    uint32_t w = 0;
    for (uint32_t i = 0; i < 4; i++) w = (w << 8) | (sh + i < 16u ? m[sh + i] : 0u);
    return w;
}
inline float fbits(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
inline uint32_t bitsf(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }
inline double rt(double d) { return (double)(float)d; }            // stfs then lfs
inline double fs_div(double a, double b) { return ppu_fp_single(ppu_fdiv(a, b)); }
inline double fs_sub(double a, double b) { return ppu_fp_single(ppu_fsub(a, b)); }
inline double fcfid_s(int64_t v) { return ppu_frsp((double)v); }    // fcfid + frsp
constexpr float kNegZero = -0.0f;                                   // vslw(-1, -1)

/* 0x0002C7D8: material color word. r3 = material, r4 = color source (0: the
 * zero block), r5 = tint (0: the default). Includes 0x0002C6E8. */
bool color_native(ppu_context* c)
{
    const uint32_t mat = (uint32_t)c->gpr[3];
    const uint64_t src = c->gpr[4], tint = c->gpr[5];
    const double Ka = lfs(0x2C7CCu), Kb = lfs(0x2C7D0u);
    const uint32_t F = r32(mat);
    const uint32_t m = F & 0x700u;
    double s[4] = { 0.0, 0.0, 0.0, 0.0 };
    if (m != 0x400u && m != 0x200u && (int32_t)src != 0)
        for (int i = 0; i < 4; i++) s[i] = lfs(src + 4u * i);
    double o[4];
    if (m == 0x400u) {
        const double v = lfs(0x2C6E0u);
        o[0] = o[1] = o[2] = o[3] = v;
    } else if (m == 0x300u) {
        for (int i = 0; i < 4; i++) o[i] = fs_mul(s[i], lfs(0x40C750u + 4u * i));
    } else if (m == 0x200u) {
        for (int i = 0; i < 4; i++) o[i] = lfs(0x40C750u + 4u * i);
    } else {
        for (int i = 0; i < 4; i++) o[i] = s[i];
    }
    for (int i = 0; i < 4; i++) o[i] = rt(o[i]);
    double t[4];
    for (int i = 0; i < 4; i++) t[i] = (int32_t)tint != 0 ? lfs(tint + 4u * i) : Ka;
    double p0, p1, p2, p3;
    if ((F & 2u) && m != 0x400u && m != 0x100u) { p0 = t[0]; p1 = t[1]; p2 = t[2]; p3 = t[3]; }
    else p0 = p1 = p2 = p3 = Kb;
    if (m != 0) {
        p0 = fs_mul(p0, o[0]);
        p2 = fs_mul(p2, o[2]);
        p1 = fs_mul(p1, o[1]);
        if ((F & 0x20u) || !(F & 2u)) p3 = fs_mul(p3, o[3]);
    }
    const double S = lfs(0x2C7D4u);
    auto q = [&](double p) -> uint32_t {
        const int32_t v = (int16_t)ppu_f2i32(fs_mul(p, S), 0);
        return v < 0 ? 0u : v > 255 ? 255u : (uint32_t)v;
    };
    const uint32_t a = q(p0), b = q(p1), g = q(p2), d = q(p3);
    c->gpr[3] = (a << 24) | (b << 16) | (g << 8) | d;
    return true;
}

/* 0x00031DC8: texture reference of obj (+0x944, 0x20 bytes) from the
 * renderer's table; zeros when there is none. Keeps the port's pointer
 * checks of the lift. */
bool texref_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3], a = c->gpr[4], b = c->gpr[5];
    const uint32_t dst = (uint32_t)(obj + 0x944u);
    auto zero = [&]() {
        w8(dst, 0); w16(dst + 2u, 0); w16(dst + 4u, 0); w32(dst + 8u, 0); w32(dst + 0xCu, 0);
        w8(dst + 0x10u, 0); w32(dst + 0x14u, 0); w32(dst + 0x18u, 0); w32(dst + 0x1Cu, 0);
        c->gpr[3] = 0;
        return true;
    };
    const uint32_t base = r32(0x47BD50u);
    if ((int32_t)base == 0 || (int32_t)b == -1) return zero();
    GuestFrame fr(c, 0x90);
    const uint32_t r7 = r32(base + 0x18u);
    const uint32_t r8 = r32(0x40CB90u);
    const int64_t off = (int64_t)(int32_t)(uint32_t)((b - (uint64_t)r7) << 2);
    const uint32_t R = r32(0x47C5C0u);
    const uint32_t opd1 = r32(r32(R) + 0xB8u);
    const uint32_t ent = (uint32_t)((uint64_t)base + r32((uint32_t)((uint64_t)r8 + (uint64_t)off)));
    w64(c->gpr[1] + 0x28u, c->gpr[2]);
    if (ent < 0x10000u || ent >= 0x80000000u || (ent & 3u)) return zero();
    const uint32_t tex = r32(ent + 0x14u);
    if (tex & 3u) {
        static unsigned misaligned_reports = 0;
        if (misaligned_reports++ < 8u)
            fprintf(stderr, "[dbz3] misaligned object ptr=0x%08X owner=0x%08X lr=0x%08X\n",
                    tex, ent, (uint32_t)c->lr);
        return zero();
    }
    if (tex < 0x10000u || tex >= 0x80000000u) return zero();
    c->gpr[3] = R;
    c->gpr[4] = tex;
    c->gpr[5] = a & 0xFFu;
    c->gpr[7] = r7;
    c->gpr[8] = r8;
    call_opd(c, opd1, 6);
    const uint32_t opd2 = r32(r32(tex) + 8u);
    c->gpr[3] = tex;
    c->gpr[4] = opd2;
    call_opd(c, opd2, 5);
    const uint32_t sp = (uint32_t)c->gpr[3];
    const uint32_t v0 = r8u(sp), v2 = r16(sp + 2u), v4 = r16(sp + 4u), v8 = r32(sp + 8u);
    const uint32_t vc = r32(sp + 0xCu), v10 = r8u(sp + 0x10u), v14 = r32(sp + 0x14u), v18 = r32(sp + 0x18u);
    w8(dst, v0); w16(dst + 2u, v2); w16(dst + 4u, v4); w32(dst + 8u, v8); w32(dst + 0xCu, vc);
    w8(dst + 0x10u, v10); w32(dst + 0x14u, v14); w32(dst + 0x18u, v18); w32(dst + 0x1Cu, ent);
    c->gpr[3] = v18;
    return true;
}

/* 0x0002F56C: draw setup of obj: parameter block (0x0002BAE8) copied to
 * obj + 0x858, material (0x0002C088), renderer state calls, and the globals
 * at 0x00478B40.. remembered for the next call. */
bool drawsetup_native(ppu_context* c)
{
    const uint64_t obj = c->gpr[3], in4 = c->gpr[4], in5 = c->gpr[5], in6 = c->gpr[6];
    const uint64_t in7 = c->gpr[7], in8 = c->gpr[8], in9 = c->gpr[9], in10 = c->gpr[10];
    GuestFrame fr(c, 0x150);
    const uint64_t sp = c->gpr[1];
    const uint32_t prev = r32(0x478B40u);
    if ((int32_t)prev != -1) {
        c->gpr[3] = prev;
        c->gpr[11] = 0x480000u;
        c->lr = 0x0002F5D0u;
        func_00017464(c); DRAIN_TRAMPOLINE(c);
        w32(0x478B40u, 0xFFFFFFFFu);
    }
    w32(obj + 0x964u, 0);
    w32(obj + 0x96Cu, 0);
    w32(obj + 0x970u, 0);
    c->gpr[3] = sp + 0x70u;
    c->gpr[4] = (uint32_t)in6;
    c->gpr[5] = in9;
    c->gpr[6] = in10;
    c->gpr[7] = r8u(obj + 0xB24u);
    c->gpr[8] = r8u(obj + 0xB49u);
    c->lr = 0x0002F60Cu;
    func_0002BAE8(c); DRAIN_TRAMPOLINE(c);
    const uint32_t dst = (uint32_t)(obj + 0x858u);
    for (uint32_t i = 0; i < 0x98u; i += 8u) {
        const uint32_t s = (uint32_t)(sp + i);
        const uint32_t x = r32(s + 0x70u), y = r32(s + 0x74u);
        w32(dst + i, x);
        w32(dst + i + 4u, y);
    }
    c->gpr[3] = dst;
    c->gpr[4] = r32(0x47C5C0u);
    c->lr = 0x0002F65Cu;
    func_0002C088(c); DRAIN_TRAMPOLINE(c);
    {
        const uint32_t R = r32(0x47C5C0u);
        const uint32_t opd = r32(r32(R) + 0x3Cu);
        c->gpr[3] = R;
        c->gpr[4] = 0x47A300u;
        c->gpr[5] = opd;
        call_opd(c, opd, 6);
    }
    const int32_t m = (int32_t)in5;
    auto vsel = [&](uint64_t r4v, uint32_t r5v) {
        const uint32_t X = r32(0x47C5C8u);
        const uint32_t opd = r32(r32(X) + 8u);
        c->gpr[3] = X;
        c->gpr[4] = r4v;
        c->gpr[5] = r5v;
        c->gpr[6] = opd;
        call_opd(c, opd, 7);
        w32(obj + 0x964u, r5v);
    };
    if (m == 2 || m == 3) vsel((uint32_t)in4, 0);
    else if (m == 4 || m == 5) vsel(0, 1);
    else if (m == 6 || m == 7) vsel((uint32_t)in4, 1);
    uint32_t flag = 0;
    if (m >= 4 && m <= 6) {
        flag = (r32(obj + 0x858u) & 0x20u) ? 1u : 0u;
    } else if (m == 7) {
        const uint32_t F = r32(obj + 0x858u);
        flag = (F & 4u) ? 1u : 0u;
        if (F & 0x2000u) flag = 0;
        if (!(F & 0x20u)) flag = 0;
        else if (F & 0x10000000u) flag = 0;
    }
    uint32_t on = 0;
    if (flag) on = r8u(obj + 0x85Cu) ? 1u : 0u;
    {
        const uint32_t R = r32(0x47C5C0u);
        const uint32_t opd = r32(r32(R) + 0xE0u);
        c->gpr[3] = R;
        c->gpr[4] = on;
        c->gpr[5] = opd;
        call_opd(c, opd, 6);
    }
    if ((int32_t)in8 >= 0) {
        c->gpr[3] = 0; c->gpr[4] = in8;
        c->gpr[5] = c->gpr[6] = c->gpr[7] = c->gpr[8] = 0;
        c->lr = 0x0002F868u;
        func_00029734(c); DRAIN_TRAMPOLINE(c);
    }
    {
        const int32_t v = (int32_t)r32(obj + 0x9C0u);
        if (v >= 0 && (r32(obj + 0x858u) & 0x2000u)) {
            c->gpr[3] = 1; c->gpr[4] = (uint64_t)(int64_t)v;
            c->gpr[5] = c->gpr[6] = c->gpr[7] = c->gpr[8] = 0;
            c->lr = 0x0002F8A4u;
            func_00029734(c); DRAIN_TRAMPOLINE(c);
        }
    }
    w32(obj + 0x968u, (uint32_t)in7);
    w32(0x478B44u, (uint32_t)in6);
    w32(0x478B40u, (uint32_t)in5);
    w32(0x478B5Cu, (uint32_t)in4);
    return true;
}

/* 0x00030790: textured quad. r3 = obj, r4 = position, r5 = color source,
 * r6 = size (x, y, u, v). With the "scaled" flag the UVs are divided by the
 * texture size (Newton refined reciprocals, as the lift); on odd calls the
 * quad is drawn with six vertex calls, on even calls it is remembered. */
bool quad_native(ppu_context* c)
{
    const uint32_t obj = (uint32_t)c->gpr[3];
    const uint64_t P = c->gpr[4], B = c->gpr[5], Q = c->gpr[6];
    const uint32_t u946 = r16(obj + 0x946u), u948 = r16(obj + 0x948u);
    const uint32_t flags = r32(obj + 0x858u), Z = r32(obj + 0x960u);
    double f31 = lfs(Q), f30 = lfs(Q + 4u);
    const uint32_t w946 = lvlx_w0(obj + 0x946u), w948 = lvlx_w0(obj + 0x948u);
    uint32_t v4 = lvlx_w0(Q), v2 = lvlx_w0(Q + 4u);
    const float q0 = fbits(v4), q1 = fbits(v2);
    const double f5 = fcfid_s((int64_t)u946), f4 = fcfid_s((int64_t)u948);
    double f1 = lfs(Q + 8u), f2 = lfs(Q + 0xCu);
    const float x7 = (float)(w946 >> 16), x8 = (float)(w948 >> 16);
    if ((flags & 0x40000000u) && x7 > 0.0f && x8 > 0.0f) {
        const double f3c = lfs(0x30788u);
        const float K = fbits(lvlx_w0(0x3078Cu));
        float s3 = q0, s2 = q1;
        if ((int32_t)Z != 0) {
            const float s10 = (float)((int32_t)lvlx_w0(Z + 0x10u) >> 16);
            const float s12 = (float)((int32_t)lvlx_w0(Z + 0x12u) >> 16);
            const float s8 = (float)((int32_t)lvlx_w0(Z + 8u) >> 16);
            const float sA = (float)((int32_t)lvlx_w0(Z + 0xAu) >> 16);
            const double zf1 = fs_div(fcfid_s((int16_t)r16(Z + 8u)), fcfid_s((int16_t)r16(Z + 0x10u)));
            const double zf2 = fs_div(fcfid_s((int16_t)r16(Z + 0xAu)), fcfid_s((int16_t)r16(Z + 0x12u)));
            const float r11 = 1.0f / s10, r13 = 1.0f / s12;
            const float v12 = K - r11 * s10;
            const float v14 = K - r13 * s12;
            const float v10 = r11 * v12 + kNegZero;
            const float v1 = K - v10 * s10;
            const float v10b = v10 * v1 + kNegZero;
            const float v11 = r13 * v14 + kNegZero;
            const float v3 = K - v10b * s10;
            const float v16 = K - v11 * s12;
            f31 = fs_mul(f31, zf1);
            const float v3b = v10b * v3 + kNegZero;
            const float v10c = v11 * v16 + kNegZero;
            const float v3c = v3b * s8 + kNegZero;
            const float v9 = K - v10c * s12;
            const float v3d = q0 * v3c + kNegZero;
            const float v9b = v10c * v9 + kNegZero;
            f30 = fs_mul(f30, zf2);
            const float v2a = v9b * sA + kNegZero;
            const float v2b = q1 * v2a + kNegZero;
            s3 = v3d;
            s2 = v2b;
        }
        const float a5 = 1.0f / x7, a6 = 1.0f / x8;
        f31 = fs_div(f31, f5);
        const float e12 = K - a5 * x7;
        const float e13 = K - a6 * x8;
        f30 = fs_div(f30, f4);
        const float b5 = a5 * e12 + kNegZero;
        const float b6 = a6 * e13 + kNegZero;
        const float g12 = K - b5 * x7;
        const float g13 = K - b6 * x8;
        const float c5 = b5 * g12 + kNegZero;
        const float c6 = b6 * g13 + kNegZero;
        const float h7 = K - c5 * x7;
        const float h4 = K - c6 * x8;
        const float d5 = c5 * h7 + kNegZero;
        const float d6 = c6 * h4 + kNegZero;
        v4 = bitsf(d5 * s3 + kNegZero);
        v2 = bitsf(d6 * s2 + kNegZero);
        f1 = f2 = f3c;
    }
    const uint32_t cnt = r32(obj + 0x970u);
    if ((cnt & 1u) == 0) {
        for (uint32_t i = 0; i < 16u; i += 4u) stfs(obj + 0x980u + i, lfs(P + i));
        w32((obj + 0x990u) & ~3u, v4);
        w32((obj + 0x994u) & ~3u, v2);
        stfs(obj + 0x998u, f1);
        stfs(obj + 0x99Cu, f2);
        return true;
    }
    const double X0 = lfs(obj + 0x980u), Y0 = lfs(obj + 0x984u), Z1 = lfs(P + 8u);
    const double X1 = lfs(P), Y1 = lfs(P + 4u);
    const double U0 = lfs(obj + 0x990u), V0 = lfs(obj + 0x994u);
    GuestFrame fr(c, 0x120);
    const uint64_t sp = c->gpr[1];
    c->gpr[3] = (obj + 0x858u) & 0xFFFFFFFFull;
    c->gpr[4] = r32(obj + 0x968u);
    c->gpr[5] = B;
    c->lr = 0x00030B78u;
    func_0002C7D8(c); DRAIN_TRAMPOLINE(c);
    w32(sp + 0x70u, (uint32_t)c->gpr[3]);
    struct V { double x, y, u, v; };
    const V vs[6] = { { X0, Y0, U0, V0 }, { X1, Y0, f31, V0 }, { X0, Y1, U0, f30 },
                      { X0, Y1, U0, f30 }, { X1, Y0, f31, V0 }, { X1, Y1, f31, f30 } };
    for (const V& v : vs) {
        const double sx = lfs(obj + 0x9A8u), ox = lfs(obj + 0x9A0u);
        const double sy = lfs(obj + 0x9ACu), oy = lfs(obj + 0x9A4u);
        const double x = ppu_fp_single(ppu_fmadd_core(v.x, sx, ox, 0, 0));
        const double y = ppu_fp_single(ppu_fmadd_core(v.y, sy, oy, 0, 0));
        const uint32_t R = r32(0x47C5C8u);
        stfs(sp + 0x7Cu, Z1);
        stfs(sp + 0x74u, x);
        stfs(sp + 0x78u, y);
        const uint32_t opd = r32(r32(R) + 0x1Cu);
        c->gpr[3] = R;
        c->gpr[4] = sp + 0x74u;
        c->gpr[5] = sp + 0x70u;
        c->gpr[6] = opd;
        c->fpr[1] = v.u;
        c->fpr[2] = v.v;
        call_opd(c, opd, 7);
    }
    return true;
}

/* 0x00030DF0 / 0x00031024: sprite. Mode 4 (0x00478B40) hands over to the
 * quad; otherwise one vertex call with the size scaled like the quad's UVs. */
bool sprite_common(ppu_context* c, uint64_t P, uint64_t Bc, uint64_t Q, uint32_t first, bool v2)
{
    const uint32_t obj = (uint32_t)c->gpr[3];
    const uint64_t in4 = c->gpr[4], in5 = c->gpr[5], in6 = c->gpr[6];
    const uint32_t cnt = r32(obj + 0x970u), flags = r32(obj + 0x858u);
    const uint32_t w946 = lvlx_w0(obj + 0x946u), w948 = lvlx_w0(obj + 0x948u);
    const uint32_t u946 = r16(obj + 0x946u), u948 = r16(obj + 0x948u);
    const uint32_t Z = r32(obj + 0x960u), sel = r32(obj + 0x968u);
    if ((int32_t)cnt == 0) w32(obj + 0x96Cu, first);
    GuestFrame fr(c, 0xB0);
    if ((int32_t)r32(0x478B40u) == 4) {
        c->gpr[3] = obj;
        if (v2) { c->gpr[4] = in4; c->gpr[5] = in5; c->gpr[6] = in6; c->lr = 0x000310C8u; }
        else { c->gpr[4] = in4; c->gpr[5] = 0; c->gpr[6] = in5; c->lr = 0x00030E7Cu; }
        func_00030790(c); DRAIN_TRAMPOLINE(c);
    } else {
        double f31 = lfs(Q), f30 = lfs(Q + 4u);
        const double f2 = fcfid_s((int64_t)u946), f1 = fcfid_s((int64_t)u948);
        if ((flags & 0x40000000u) && (float)(w946 >> 16) > 0.0f && (float)(w948 >> 16) > 0.0f) {
            if ((int32_t)Z != 0) {
                const double d3 = fs_div(fcfid_s((int16_t)r16(Z + 8u)), fcfid_s((int16_t)r16(Z + 0x10u)));
                const double d4 = fs_div(fcfid_s((int16_t)r16(Z + 0xAu)), fcfid_s((int16_t)r16(Z + 0x12u)));
                f31 = fs_mul(f31, d3);
                f30 = fs_mul(f30, d4);
            }
            f31 = fs_div(f31, f2);
            f30 = fs_div(f30, f1);
        }
        const double x = ppu_fp_single(ppu_fmadd_core(lfs(P), lfs(obj + 0x9A8u), lfs(obj + 0x9A0u), 0, 0));
        const double y = ppu_fp_single(ppu_fmadd_core(lfs(P + 4u), lfs(obj + 0x9ACu), lfs(obj + 0x9A4u), 0, 0));
        const uint32_t R = r32(0x47C5C8u);
        const double z = lfs(P + 8u);
        const uint64_t sp = c->gpr[1];
        stfs(sp + 0x7Cu, z);
        stfs(sp + 0x74u, x);
        stfs(sp + 0x78u, y);
        c->gpr[3] = (obj + 0x858u) & 0xFFFFFFFFull;
        c->gpr[4] = sel;
        c->gpr[5] = v2 ? Bc : 0;
        c->lr = v2 ? 0x00031210u : 0x00030FC0u;
        func_0002C7D8(c); DRAIN_TRAMPOLINE(c);
        w32(sp + 0x70u, (uint32_t)c->gpr[3]);
        const uint32_t opd = r32(r32(R) + 0x1Cu);
        c->gpr[3] = R;
        c->gpr[4] = sp + 0x74u;
        c->gpr[5] = sp + 0x70u;
        c->gpr[6] = opd;
        c->fpr[1] = f31;
        c->fpr[2] = f30;
        call_opd(c, opd, 7);
    }
    w32(obj + 0x970u, r32(obj + 0x970u) + 1u);
    return true;
}
bool sprite_native(ppu_context* c) { return sprite_common(c, c->gpr[4], 0, c->gpr[5], 0x21u, false); }
bool sprite2_native(ppu_context* c) { return sprite_common(c, c->gpr[4], c->gpr[5], c->gpr[6], 0x31u, true); }

/* 0x000207C0: bone animation of one model: for each bone, rotation keys
 * (0x000204D0, 0x00024F58, 0x00026190), translation and scale keys
 * (0x00020658, 0x000298A0), or the bind pose (0x00029784). */
void anim_vec_key(ppu_context* c, uint32_t trk, uint32_t D, uint64_t Bp, uint32_t slot, uint64_t r4v,
                  uint32_t lr1, uint64_t tmp, uint32_t lr2)
{
    c->fpr[1] = lfs(Bp + 8u);
    c->gpr[3] = trk;
    c->gpr[4] = r4v;
    c->gpr[5] = (uint64_t)(int64_t)(int32_t)r32(slot);
    c->lr = lr1;
    func_00020658(c); DRAIN_TRAMPOLINE(c);
    const uint64_t k = c->gpr[3];
    w32(slot, (uint32_t)k);
    const uint32_t K = (uint32_t)(trk + (k << 4) + 0xCu);
    const uint32_t kt = r32(K);
    const float t0 = (float)(int32_t)lvlx_w0(K);
    const float tv = fbits(lvlx_w0(Bp + 8u));
    const double ft = lfs(Bp + 8u);
    const double f1k = fcfid_s((int64_t)(int32_t)kt);
    auto copy = [&]() {
        stfs(D, lfs(K + 4u));
        stfs(D + 4u, lfs(K + 8u));
        const double z = lfs(K + 0xCu);
        w32(D + 0xCu, 0x3F800000u);
        stfs(D + 8u, z);
    };
    if (t0 == tv) { copy(); return; }
    const int32_t n = (int32_t)r32(trk + 4u);
    if (n > 1 || n <= -1) return;
    copy();
    const uint32_t K2 = (uint32_t)(K + 0x10u);
    stfs(tmp, lfs(K2 + 4u));
    stfs(tmp + 4u, lfs(K2 + 8u));
    const double z2 = lfs(K2 + 0xCu);
    w32(tmp + 0xCu, 0x3F800000u);
    stfs(tmp + 8u, z2);
    double f1 = fs_sub(ft, f1k);
    const int64_t d = (int64_t)(int32_t)(r32(K2) - kt);
    const double f2 = fcfid_s(d);
    f1 = fs_div(f1, f2);
    c->gpr[3] = D;
    c->gpr[4] = D;
    c->gpr[5] = tmp;
    c->fpr[1] = f1;
    c->fpr[2] = f2;
    c->fpr[3] = z2;
    c->lr = lr2;
    func_000298A0(c); DRAIN_TRAMPOLINE(c);
}

bool anim_native(ppu_context* c)
{
    const uint64_t A = c->gpr[3], Bp = c->gpr[4];
    const uint32_t M = r32(0x40CAD0u);
    const int64_t key = (int64_t)((int32_t)r32((uint32_t)Bp) >> 16);
    const int32_t N = (int32_t)r32(r32((uint32_t)A + 4u) + 0x10u);
    const uint32_t T = (uint32_t)(A + ((uint64_t)(key - 1) << 9) + 0x4B0u);
    if (N <= 0) return true;
    GuestFrame fr(c, 0x130);
    const uint64_t sp = c->gpr[1];
    uint64_t r20 = 0, r21 = 0, r22 = 0;
    auto bind = [&](uint64_t extra) -> uint32_t {
        return (uint32_t)(r32(r32((uint32_t)A + 4u) + 0x14u) + r20 + extra);
    };
    for (int32_t i = 0; i < N; ++i, r21 += 2u, r20 += 0x50u) {
        const int32_t idx = (int16_t)r16((uint32_t)(T + (uint64_t)(int64_t)(int32_t)r21));
        if (idx < 0) {
            if (idx != -1) continue;
            if ((int32_t)r32(0x40CAE0u) != 1) {
                const uint32_t S = (uint32_t)(r32((uint32_t)Bp + 0x10u) + r22);
                c->gpr[3] = S;
                c->gpr[4] = bind(0);
                c->lr = 0x00020D94u;
                func_00029784(c); DRAIN_TRAMPOLINE(c);
                w32(S + 0x10u, 0); w32(S + 0x14u, 0); w32(S + 0x18u, 0); w32(S + 0x1Cu, 0x3F800000u);
                c->gpr[3] = (uint32_t)(r32((uint32_t)Bp + 0x10u) + r22 + 0x20u);
                c->gpr[4] = bind(0x20);
                c->lr = 0x00020DD4u;
                func_00029784(c); DRAIN_TRAMPOLINE(c);
                w32(S + 0x30u, 0xFFFFFFFFu); w32(S + 0x34u, 0xFFFFFFFFu); w32(S + 0x38u, 0xFFFFFFFFu);
            }
            r22 += 0x40u;
            continue;
        }
        const uint32_t S = (uint32_t)(r32((uint32_t)Bp + 0x10u) + r22);
        const uint64_t i12 = (uint64_t)idx * 12u, i8 = (uint64_t)idx << 3, i4 = (uint64_t)idx << 2;
        /* rotation */
        {
            const uint32_t fl = r32(M);
            bool bindpose = true;
            if (fl & 1u) {
                const uint32_t tb = r32(M + 0xCu);
                const uint32_t trk = r32((uint32_t)(tb + ((fl & 0x10u) ? i12 : i8)));
                if (trk != 0) {
                    bindpose = false;
                    c->fpr[1] = lfs(Bp + 8u);
                    c->gpr[3] = trk;
                    c->gpr[4] = tb;
                    c->gpr[5] = (uint64_t)(int64_t)(int32_t)r32(S + 0x30u);
                    c->lr = 0x000208D8u;
                    func_000204D0(c); DRAIN_TRAMPOLINE(c);
                    const uint64_t k = c->gpr[3];
                    w32(S + 0x30u, (uint32_t)k);
                    const uint32_t K = (uint32_t)(trk + (k << 3) + 0xCu);
                    c->gpr[3] = S;
                    c->gpr[4] = r16(K + 2u); c->gpr[5] = r16(K + 4u); c->gpr[6] = r16(K + 6u);
                    c->lr = 0x00020900u;
                    func_00024F58(c); DRAIN_TRAMPOLINE(c);
                    const float t0 = (float)((int32_t)lvlx_w0(K) >> 16);
                    const float tv = fbits(lvlx_w0(Bp + 8u));
                    if (!(t0 == tv)) {
                        const uint32_t K2 = (uint32_t)(K + 8u);
                        c->gpr[3] = sp + 0x70u;
                        c->gpr[4] = r16(K2 + 2u); c->gpr[5] = r16(K2 + 4u); c->gpr[6] = r16(K2 + 6u);
                        c->lr = 0x0002094Cu;
                        func_00024F58(c); DRAIN_TRAMPOLINE(c);
                        const int64_t a = (int16_t)r16(K), b = (int16_t)r16(K2);
                        double f1 = fs_sub(lfs(Bp + 8u), fcfid_s(a));
                        const int64_t d = (int64_t)(int32_t)(b - a);
                        const double f2 = fcfid_s(d);
                        f1 = fs_div(f1, f2);
                        c->gpr[3] = S;
                        c->gpr[4] = S;
                        c->gpr[5] = sp + 0x70u;
                        c->gpr[6] = (uint64_t)d;
                        c->fpr[1] = f1;
                        c->fpr[2] = f2;
                        c->lr = 0x000209A8u;
                        func_00026190(c); DRAIN_TRAMPOLINE(c);
                    }
                }
            }
            if (bindpose && (int32_t)r32(0x40CAE0u) != 1) {
                c->gpr[3] = S;
                c->gpr[4] = bind(0);
                c->lr = 0x000209D8u;
                func_00029784(c); DRAIN_TRAMPOLINE(c);
                w32(S + 0x30u, 0xFFFFFFFFu);
            }
        }
        /* translation */
        {
            const uint32_t fl = r32(M);
            const uint32_t tb = r32(M + 0xCu);
            const uint32_t trk = !(fl & 1u) ? r32((uint32_t)(tb + i4))
                               : r32((uint32_t)(tb + ((fl & 0x10u) ? i12 : i8) + 4u));
            if (trk != 0) {
                anim_vec_key(c, trk, S + 0x10u, Bp, S + 0x34u, tb, 0x00020A68u, sp + 0x80u, 0x00020B74u);
            } else if ((int32_t)r32(0x40CAE0u) != 1) {
                w32(S + 0x10u, 0); w32(S + 0x14u, 0); w32(S + 0x18u, 0);
                w32(S + 0x1Cu, 0x3F800000u); w32(S + 0x34u, 0xFFFFFFFFu);
            }
        }
        /* scale */
        {
            const uint32_t fl = r32(M);
            uint32_t trk = 0;
            if (fl & 0x10u) trk = r32((uint32_t)(r32(M + 0xCu) + i12 + 8u));
            if (trk != 0) {
                const uint64_t base = (uint64_t)r32((uint32_t)Bp + 0x10u) + r22;
                anim_vec_key(c, trk, (uint32_t)(base + 0x20u), Bp, S + 0x38u, base, 0x00020C08u,
                             sp + 0x90u, 0x00020D08u);
            } else if ((int32_t)r32(0x40CAE0u) != 1) {
                c->gpr[3] = (uint32_t)(r32((uint32_t)Bp + 0x10u) + r22 + 0x20u);
                c->gpr[4] = bind(0x20);
                c->lr = 0x00020D48u;
                func_00029784(c); DRAIN_TRAMPOLINE(c);
                w32(S + 0x38u, 0xFFFFFFFFu);
            }
        }
        r22 += 0x40u;
    }
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
Entry s_fn[25] = {
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
    /* batch 2: no guest-code hash (kept out of the check); native-b2-off turns all off */
    { "color 0x0002C7D8", "native-color-off", color_native, func_0002C7D8_lifted, kR3, {}, {}, 0, {} },
    { "texref 0x00031DC8", "native-texref-off", texref_native, func_00031DC8_lifted, kNone, {}, {}, 0, {} },
    { "drawsetup 0x0002F56C", "native-drawsetup-off", drawsetup_native, func_0002F56C_lifted, kNone, {}, {}, 0, {} },
    { "quad 0x00030790", "native-quad-off", quad_native, func_00030790_lifted, kNone, {}, {}, 0, {} },
    { "sprite 0x00030DF0", "native-sprite-off", sprite_native, func_00030DF0_lifted, kNone, {}, {}, 0, {} },
    { "sprite2 0x00031024", "native-sprite2-off", sprite2_native, func_00031024_lifted, kNone, {}, {}, 0, {} },
    { "anim 0x000207C0", "native-anim-off", anim_native, func_000207C0_lifted, kNone, {}, {}, 0, {} },
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
    const bool off = sd_flag(e.off_flag) || (&e >= &s_fn[18] && sd_flag("native-b2-off"));
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
void func_0002C7D8(ppu_context* ctx) { dispatch(ctx, s_fn[18]); }
void func_00031DC8(ppu_context* ctx) { dispatch(ctx, s_fn[19]); }
void func_0002F56C(ppu_context* ctx) { dispatch(ctx, s_fn[20]); }
void func_00030790(ppu_context* ctx) { dispatch(ctx, s_fn[21]); }
void func_00030DF0(ppu_context* ctx) { dispatch(ctx, s_fn[22]); }
void func_00031024(ppu_context* ctx) { dispatch(ctx, s_fn[23]); }
void func_000207C0(ppu_context* ctx) { dispatch(ctx, s_fn[24]); }

/* ---- 0x002909C0: end-of-frame wait --------------------------------------------
 * The title waits here for the flag at r3 + 0x2C, which its user-command
 * handler raises once the RSX has passed the previous frame's user command.
 * The lift polls with sys_timer_usleep(30), and that syscall never delivers
 * pending GCM callbacks: the handler only ran when some other guest thread
 * happened to make an HLE call. Here the loop delivers them itself
 * (ppu_gcm_pump, as every HLE call does) before each sleep. Nothing is raised
 * early: the callback is still queued by the FIFO walker when it reaches the
 * command. SD switch usercmd-pump-off: the lifted loop.
 *
 * Counters for the pace log (dbz3_loop_prof.cpp), in system ticks: waits that
 * found the flag down, the whole wait, the part before the FIFO queued the
 * command ("fifo") and the part after it ("deliver"), and the longest wait. */
extern "C" int g_t23_postfx_nowait;
extern "C" volatile uint64_t g_gcm_user_queued_tick;   // cellGcmSys.c
void func_002909C0_lifted(ppu_context* ctx);
extern "C" uint64_t g_fw_waits, g_fw_wait_ticks, g_fw_fifo_ticks, g_fw_deliver_ticks,
    g_fw_max_ticks;   // defined in dbz3_loop_prof.cpp
static int s_fw_mode;   // 0 undecided, 1 delivering loop, -1 lifted

void func_002909C0(ppu_context* ctx)
{
    if (g_t23_postfx_nowait) return;
    if (__builtin_expect(!s_fw_mode, 0)) {
        s_fw_mode = sd_flag("usercmd-pump-off") ? -1 : 1;
        fprintf(stderr, "[native-A] frame wait 0x002909C0: %s\n",
                s_fw_mode > 0 ? "delivers GCM callbacks" : "lifted (switched off)");
    }
    const uint32_t obj = (uint32_t)ctx->gpr[3];
    uint32_t f = r32(obj + 0x2Cu);
    if (f) { ctx->gpr[3] = f; return; }

    const uint64_t t0 = armGetSystemTick();
    if (s_fw_mode < 0) {
        func_002909C0_lifted(ctx);
    } else {
        for (;;) {
            if (ppu_gcm_pump) ppu_gcm_pump();
            if ((f = r32(obj + 0x2Cu)) != 0) break;
            ctx->gpr[11] = 0x8D;
            ctx->gpr[3] = 0x1E;
            lv2_syscall(ctx);
            if ((f = r32(obj + 0x2Cu)) != 0) break;
        }
        ctx->gpr[3] = f;
    }
    const uint64_t t1 = armGetSystemTick();
    const uint64_t q = __atomic_load_n(&g_gcm_user_queued_tick, __ATOMIC_RELAXED);
    const uint64_t dt = t1 - t0;
    g_fw_waits++;
    g_fw_wait_ticks += dt;
    if (dt > g_fw_max_ticks) g_fw_max_ticks = dt;
    if (q > t0 && q <= t1) { g_fw_fifo_ticks += q - t0; g_fw_deliver_ticks += t1 - q; }
    else g_fw_deliver_ticks += dt;   // queued before the wait began: all delivery
}
