/*
 * DBZ3 native-A step 2c: the mesh draw body 0x000328A8 in native code.
 *
 * 0x000328A8(obj, model, part) emits one mesh draw: draw parameters
 * (0x0002BAE8), material (0x0002C088), render state through the renderer's
 * virtual methods, the vertex and fragment programs picked from the
 * material flags, their constants (0x000538BC / 0x000539AC), vertex arrays
 * and the draw call. About 30 calls per mesh. This version keeps every call
 * and every argument the lift passes, but does its own work -- flag tests,
 * the program choice, the colour unpacking, the stack blocks it hands to the
 * callees -- with locals instead of the PPU register file, and does the small
 * leaf 0x0002C6E8 (four floats picked by the material's 0x700 bits) inline.
 *
 * Volatile registers are set as the lift sets them before each call (r3-r10,
 * f1-f3 where the lift sets them); the non-volatile ones the function uses
 * are locals and never written back, so the caller gets r14-r31, f14-f31 and
 * r1 unchanged, as the ABI requires. The register save area of the frame is
 * not written (nothing reads it); the blocks passed to callees by pointer are.
 *
 * The earlier generated fast copy (scripts/ppu_mesh_fast.py, run83-86) kept the
 * lift's instructions and gave no gain; it is gone.
 *
 * Its callees write far more than can be listed by hand (shader reference
 * counts through stwcx., matrices through vector stores, the native
 * replacements' own stores), so the verify harness uses the undo log of
 * dbz3_wtrack.cpp: run one version inside a transaction, read back the final
 * value of every byte it wrote, roll the transaction back, run the other
 * version in a new transaction and compare every byte either one wrote, plus
 * the command buffer, the GCM context and the non-volatile registers.
 *
 * SD switches (sdmc:/switch/dbz-budokai-3-switch/):
 *   native-mesh-off  run the lifted function
 *   mesh-verify      (PORT_DBZ3_WTRACK build) lifted vs native on every call,
 *                    t36-mesh.log; the lifted result is kept on any difference
 *   mesh-selftest    (PORT_DBZ3_WTRACK build) lifted vs lifted, to check the
 *                    harness itself
 */
#define PPU_FAST_VM 1
#include "dbz3_lifted_prelude.inc"
#include "dbz3_liftfast.h"
#include "dbz3_store_hook.h"
#include "dbz3_native_stamp.h"

#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

void func_000328A8_lifted(ppu_context* ctx);

#ifdef DBZ3_WTRACK
extern "C" void dbz3_undo_begin(void);
extern "C" void dbz3_undo_end(void);
extern "C" int dbz3_undo_overflow(void);
extern "C" unsigned dbz3_undo_count(void);
extern "C" void dbz3_undo_entry(unsigned i, uint32_t* addr, uint32_t* len, const uint8_t** old);
extern "C" void dbz3_undo_rollback(void);
#endif

namespace {

/* ---- guest memory (plain guest RAM: stack, game objects, tables) -------- */
inline uint32_t g8(uint32_t a)  { return vm_base[a]; }
inline uint32_t g16(uint32_t a) { uint16_t v; memcpy(&v, vm_base + a, 2); return __builtin_bswap16(v); }
inline uint32_t g32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }
inline uint64_t g64(uint32_t a) { uint64_t v; memcpy(&v, vm_base + a, 8); return __builtin_bswap64(v); }
inline double   gf(uint32_t a)  { const uint32_t w = g32(a); float f; memcpy(&f, &w, 4); return f; }   // lfs
inline void p8(uint32_t a, uint32_t v)  { dbz3_store_hook(a, 1u); vm_base[a] = (uint8_t)v; }
inline void p16(uint32_t a, uint32_t v) { dbz3_store_hook(a, 2u); const uint16_t s = __builtin_bswap16((uint16_t)v); memcpy(vm_base + a, &s, 2); }
inline void p32(uint32_t a, uint32_t v) { dbz3_store_hook(a, 4u); v = __builtin_bswap32(v); memcpy(vm_base + a, &v, 4); }
inline void p64(uint32_t a, uint64_t v) { dbz3_store_hook(a, 8u); v = __builtin_bswap64(v); memcpy(vm_base + a, &v, 8); }
inline void pf(uint32_t a, double d)    { const float f = (float)d; uint32_t w; memcpy(&w, &f, 4); p32(a, w); }  // stfs
inline float    bitsf(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
inline uint32_t fbits(float f)    { uint32_t w; memcpy(&w, &f, 4); return w; }
inline uint32_t rotl(uint32_t x, unsigned s) { return s ? (x << s) | (x >> (32u - s)) : x; }

/* Word 0 of the lift's unaligned vector load: the bytes from ea to the end of
 * its 16-byte block, zero past it. */
inline uint32_t vload0(uint32_t ea)
{
    const uint32_t sh = ea & 15u;
    const uint8_t* m = vm_base + (ea & ~15u);
    uint32_t w = 0;
    for (uint32_t i = 0; i < 4; i++) w = (w << 8) | (sh + i < 16u ? m[sh + i] : 0u);
    return w;
}

const uint32_t kRendererPtr = 0x0047C5C0u;   // the renderer object (vtable at +0)
const uint32_t kGcmCtxPtr = 0x005D5610u;
const uint32_t kConst = 0x00032890u;         // float/int constants of 0x000328A8
const uint32_t kTint = 0x0040C750u;          // four floats, read by 0x0002C6E8 too

inline uint32_t renderer() { return g32(kRendererPtr); }

/* One call into guest code, as the lift makes it: ctx->gpr[1] is the frame,
 * r2 the TOC, and the caller's TOC is back in r2 afterwards. */
struct Call {
    ppu_context* c;
    uint64_t toc;
    void direct(uint32_t lr, void (*fn)(ppu_context*))
    {
        c->lr = lr;
        fn(c);
        DRAIN_TRAMPOLINE(c);
        c->gpr[2] = toc;
    }
    /* Renderer method at vtable offset `off`, r3 = renderer. The lift leaves
     * the descriptor and the code address in two volatile registers; they are
     * set too (opd_reg, code_reg; 0 = none). */
    void method(uint32_t ren, uint32_t off, int opd_reg, int code_reg)
    {
        const uint32_t opd = g32(g32(ren) + off);
        const uint32_t code = g32(opd);
        c->gpr[3] = ren;
        if (opd_reg) c->gpr[opd_reg] = opd;
        if (code_reg) c->gpr[code_reg] = code;
        c->ctr = code;
        c->gpr[2] = g32(opd + 4u);
        ps3_indirect_call(c);
        DRAIN_TRAMPOLINE(c);
        c->gpr[2] = toc;
    }
};

/* Renderer method +0xAC (set one shader constant block): r4 = register,
 * r5 = count, r6 = data; r7 = descriptor, r8 = code. */
inline void set_const(Call& k, uint32_t reg, uint32_t count, uint32_t data)
{
    ppu_context* c = k.c;
    c->gpr[4] = reg; c->gpr[5] = count; c->gpr[6] = data;
    k.method(renderer(), 0xACu, 7, 8);
}

/* 0x000538BC / 0x000539AC on the fragment program `fp`. */
inline void fp_bind(Call& k, uint32_t fp, uint32_t slot, uint32_t name, uint32_t lr)
{
    ppu_context* c = k.c;
    c->gpr[3] = fp; c->gpr[4] = slot; c->gpr[5] = name;
    k.direct(lr, func_000538BC);
}
inline void fp_set(Call& k, uint32_t fp, uint32_t slot, uint32_t count, uint32_t data, uint32_t lr)
{
    ppu_context* c = k.c;
    c->gpr[3] = fp; c->gpr[4] = slot; c->gpr[5] = count; c->gpr[6] = data;
    k.direct(lr, func_000539AC);
}

/* PPC fcmpu tests as the lift uses them (NaN: all false). */
inline bool fgt(double a, double b) { return a > b; }
inline bool fle(double a, double b) { return a < b || a == b; }   // cror lt|eq

/* The four colour-pair choices the program selection repeats. */
inline uint32_t pick_fp(double fA8, double fC8, double f31, bool a_gt)
{
    // a_gt: first test is "fA8 > f31" (else "fA8 <= f31" form, see callers)
    (void)fA8;
    if (a_gt) return fgt(fC8, f31) ? 1u : 0x1Cu;
    return fgt(fC8, f31) ? 0x37u : 0u;
}

void native_328A8(ppu_context* c)
{
    const uint64_t sp0 = c->gpr[1];
    const uint32_t sp = (uint32_t)sp0 - 0x240u;
    const uint64_t lr0 = c->lr;
    Call k{ c, c->gpr[2] };

    const uint32_t r31 = (uint32_t)c->gpr[3];   // object
    const uint32_t r30 = (uint32_t)c->gpr[4];   // model
    const uint32_t r29 = (uint32_t)c->gpr[5];   // part

    p64(sp, sp0);                     // back chain
    p64((uint32_t)sp0 + 0x10u, lr0);  // LR save slot of the caller's frame
    c->gpr[1] = sp;
    p64(sp + 0x28u, k.toc);           // TOC save slot (the lift rewrites it before each method call)

    const uint32_t mesh = (uint32_t)(g32(r30 + 0x20u) + (uint64_t)g16(r29 + 0x14u) * 80u);
    const uint32_t r26 = g32(mesh + 0x3Cu);       // draw flags
    const uint32_t r27 = g32(mesh + 0x38u);
    const uint32_t r25a = g32(mesh + 0x30u);
    const uint32_t r24 = g32(mesh + 0x34u);

    /* 0x0002BAE8: the 0x98-byte draw parameter block at sp+0x124. */
    c->gpr[3] = sp + 0x124u;
    c->gpr[4] = r26;
    c->gpr[5] = g32(mesh + 0x44u);
    c->gpr[6] = g64(mesh + 0x20u);
    c->gpr[7] = g8(r31 + 0xB24u);
    c->gpr[8] = g8(r31 + 0xB49u);
    k.direct(0x0003293Cu, func_0002BAE8);

    const uint32_t blk = r31 + 0x858u;            // the object's copy of it
    dbz3_store_hook(blk, 0x98u);
    memcpy(vm_base + blk, vm_base + sp + 0x124u, 0x98u);

    c->gpr[3] = blk;
    c->gpr[4] = renderer();
    k.direct(0x00032990u, func_0002C088);

    const uint32_t r23 = r26 & 0x2000u;
    {
        const uint32_t a = g8(r29 + 0x38u), b = g8(blk + 0x5Cu);
        bool f = false;
        if (a == 0) f = (r26 & 0x20u) && r23 == 0;
        else f = b != 0 && r23 == 0;
        c->gpr[4] = (f && g8(blk + 0x4u)) ? 1u : 0u;
        k.method(renderer(), 0xE0u, 5, 6);
    }
    if (g8(blk + 0x37u) == 0) {
        const uint32_t v = g32(r31 + 0xB44u);
        c->gpr[4] = (int32_t)v == -1 ? 0xEu : v;
        k.method(renderer(), 0x50u, 5, 6);
    }
    uint32_t r22 = 0;
    if ((r26 & 0x40u) && r23 == 0 && r24 == 0) {
        c->gpr[4] = g32(r31 + 0xA10u);
        c->gpr[5] = 1;
        k.method(renderer(), 0xB8u, 6, 7);
        r22 = 1;
    }
    if (g8(blk + 0x5Cu) && (int32_t)r25a != -1) {
        c->gpr[3] = 0; c->gpr[4] = (uint64_t)(int64_t)(int32_t)r25a;
        c->gpr[5] = c->gpr[6] = c->gpr[7] = c->gpr[8] = 0;
        k.direct(0x00032B24u, func_00029734);
    }
    if (g8(blk + 0x70u) && !((int32_t)r24 == -1 || r22 != 0)) {
        c->gpr[3] = 1; c->gpr[4] = (uint64_t)(int64_t)(int32_t)r24;
        c->gpr[5] = c->gpr[6] = c->gpr[7] = c->gpr[8] = 0;
        k.direct(0x00032B60u, func_00029734);
    }

    /* Ambient colour words (sp+0x9C..0xA8) and the inline 0x0002C6E8. */
    const double f31 = gf(kConst);
    double f1 = f31;
    uint32_t va = 0, vb = 0, vc = 0;
    if (g32(blk) & 0x8000u) {
        const uint32_t w = vload0(0x004789E0u), m = vload0(kConst + 4u);
        const float s = bitsf(vload0(kConst + 8u));
        va = fbits((float)(w & m) * s + -0.0f);
        vb = fbits((float)(rotl(w, 24) & m) * s + -0.0f);
        vc = fbits((float)(rotl(w, 16) & m) * s + -0.0f);
        const uint64_t n = (uint64_t)(0xFFu - (uint64_t)g32(0x004789DCu)) & 0xFFu;
        const double f2 = ppu_frsp((double)(int64_t)n);
        c->fpr[2] = f2;
        f1 = ppu_fp_single(ppu_fmul(f2, gf(kConst + 8u)));
    }
    p32(sp + 0x9Cu, va); p32(sp + 0xA0u, vb); p32(sp + 0xA4u, vc);
    pf(sp + 0xA8u, f1);
    {
        double s0 = gf(mesh + 0x0u), s1 = gf(mesh + 0x4u), s2 = gf(mesh + 0x8u), s3 = gf(mesh + 0xCu);
        double o4, o3, o2, o1;
        const uint32_t m = g32(blk) & 0x700u;
        if (m == 0x400u) {
            o4 = o3 = o2 = o1 = gf(0x00030000u - 14624u);
        } else if (m == 0x300u) {
            const double k0 = gf(kTint), k1 = gf(kTint + 4u), k2 = gf(kTint + 8u), k3 = gf(kTint + 0xCu);
            o4 = ppu_fp_single(ppu_fmul(s0, k0));
            o3 = ppu_fp_single(ppu_fmul(s1, k1));
            o2 = ppu_fp_single(ppu_fmul(s2, k2));
            o1 = ppu_fp_single(ppu_fmul(s3, k3));
            c->fpr[5] = k2; c->fpr[6] = k3;
        } else if (m == 0x200u) {
            o4 = gf(kTint); o3 = gf(kTint + 4u); o2 = gf(kTint + 8u); o1 = gf(kTint + 0xCu);
        } else {
            o4 = s0; o3 = s1; o2 = s2; o1 = s3;
        }
        // The lift passes the four mesh floats through sp+0x70 (stfs, then lfs):
        // the same values as loading them directly.
        pf(sp + 0xACu, o4); pf(sp + 0xB0u, o3); pf(sp + 0xB4u, o2); pf(sp + 0xB8u, o1);
        c->fpr[4] = o4; c->fpr[3] = o3; c->fpr[2] = o2;
    }

    /* Diffuse colour words (sp+0xBC..0xC8), material colour sp+0xAC..0xB8. */
    const uint32_t fl = g32(blk);
    f1 = f31;
    const uint32_t r25 = g32(r30 + 0x28u);
    uint32_t da = 0, db = 0, dc = 0;
    if (fl & 0x80u) {
        const uint32_t x = vload0(0x004789D8u), m = vload0(kConst + 4u);
        const unsigned sh = vload0(kConst + 0xCu) & 31u;
        const float s = bitsf(vload0(kConst + 8u));
        da = fbits((float)(int32_t)(x & m) * s + -0.0f);
        db = fbits((float)(int32_t)((uint32_t)((int32_t)x >> 8) & m) * s + -0.0f);
        dc = fbits((float)(int32_t)((uint32_t)((int32_t)x >> sh) & m) * s + -0.0f);
        f1 = gf(kConst + 0x10u);
    }
    p32(sp + 0xBCu, da); p32(sp + 0xC0u, db); p32(sp + 0xC4u, dc);
    pf(sp + 0xC8u, f1);
    const uint32_t m7 = fl & 0x700u;
    if (m7 == 0) {
        p32(sp + 0xB8u, 0x3F800000u); p32(sp + 0xB4u, 0x3F800000u);
        p32(sp + 0xB0u, 0x3F800000u); p32(sp + 0xACu, 0x3F800000u);
    } else if ((fl & 0x20u) || (fl & 0x2u)) {
        f1 = gf(kTint + 0xCu);
        pf(sp + 0xB8u, f1);
    }
    const uint32_t r28b = g32(r25 + 0x2Cu);       // vertex buffer descriptor
    {
        const uint32_t a5 = g32(r29 + 0x1Cu);
        c->gpr[4] = g32(r25 + 0x1Cu);
        c->gpr[5] = a5;
        c->gpr[6] = 0x3F;
        c->gpr[9] = 0xC4;
        c->gpr[10] = m7;
        c->fpr[1] = f1;
        k.method(renderer(), 0x60u, 7, 8);
    }

    const uint32_t s38 = g8(r29 + 0x38u);
    if (s38) {
        const uint32_t n = g32(r30 + 0x3Cu);
        if (n <= 0x44u) {
            p32(sp + 0x80u, 0);
            set_const(k, 0x12Fu, 1, sp + 0x80u);
            static const uint32_t kReg[4] = { 0x14u, 0x58u, 0x9Cu, 0xE0u };
            for (int i = 0; i < 4; i++) {
                const uint32_t ren = renderer();
                const uint32_t p = (uint32_t)(r28b + (uint64_t)((g32(r28b + 0x4Cu) << 4) & ~0xFu) + 4u);
                const uint32_t n4 = (g32(r30 + 0x3Cu) << 2) & ~3u;
                const uint32_t base = g32(p), q = g32(p + 8u);
                c->gpr[4] = kReg[i];
                c->gpr[5] = n4;
                if (i == 0) {
                    c->gpr[6] = base;
                    k.method(ren, 0xACu, 7, 8);
                } else if (i == 1) {
                    c->gpr[6] = (uint32_t)(base + q);
                    k.method(ren, 0xACu, 8, 7);
                } else if (i == 2) {
                    const uint32_t q2 = (q << 1) & ~1u;
                    c->gpr[7] = q2; c->gpr[8] = base;
                    c->gpr[6] = (uint32_t)(base + q2);
                    k.method(ren, 0xACu, 0, 9);
                } else {
                    c->gpr[7] = base; c->gpr[9] = (uint64_t)q * 2u; c->gpr[8] = (uint64_t)q * 3u;
                    c->gpr[6] = (uint32_t)(base + (uint64_t)q * 3u);
                    k.method(ren, 0xACu, 0, 10);
                }
            }
        } else {
            p32(sp + 0x84u, fbits((float)vload0(r28b + 0x48u)));
            set_const(k, 0x12Fu, 1, sp + 0x84u);
            const uint32_t x48 = g32(r28b + 0x48u);
            const uint32_t addr = g32((uint32_t)(r28b + (uint64_t)((g32(r28b + 0x4Cu) << 4) & ~0xFu) + 4u));
            p8(sp + 0x10Cu, 0xBC); p8(sp + 0x10Du, 1); p8(sp + 0x10Eu, 2); p8(sp + 0x10Fu, 0);
            p32(sp + 0x110u, 0xAAE4u);
            p16(sp + 0x114u, (x48 << 2) & ~3u); p16(sp + 0x116u, 4); p16(sp + 0x118u, 1);
            p32(sp + 0x11Cu, (x48 << 4) & ~0xFu);
            p8(sp + 0x11Au, 0);
            c->gpr[3] = addr; c->gpr[4] = sp + 0x120u;
            c->gpr[5] = (x48 << 2) & ~3u; c->gpr[6] = (x48 << 4) & ~0xFu;
            c->gpr[7] = 1; c->gpr[8] = 4; c->gpr[9] = 2; c->gpr[10] = 0xAAE4u;
            k.direct(0x00033070u, func_002B6E00);
            c->gpr[3] = g32(kGcmCtxPtr); c->gpr[4] = 0; c->gpr[5] = sp + 0x10Cu;
            k.direct(0x00033088u, func_001FE0CC);
            c->gpr[3] = g32(kGcmCtxPtr); c->gpr[4] = 2;
            k.direct(0x00033098u, func_001FBE54);
            c->gpr[3] = g32(kGcmCtxPtr); c->gpr[4] = 0; c->gpr[5] = 1; c->gpr[6] = 0; c->gpr[7] = 0xC00;
            k.direct(0x000330B4u, func_001FC130);
            c->gpr[3] = g32(kGcmCtxPtr); c->gpr[4] = 0; c->gpr[5] = 5; c->gpr[6] = 5;
            k.direct(0x000330CCu, func_001FBFCC);
            c->gpr[3] = g32(kGcmCtxPtr); c->gpr[4] = 0; c->gpr[5] = 0;
            k.direct(0x000330E0u, func_001FC088);
        }
    }

    /* Program choice: vp = vertex program index, fp = fragment program index. */
    const double fA8 = gf(sp + 0xA8u), fC8 = gf(sp + 0xC8u);
    const uint32_t b5c = g8(blk + 0x5Cu);
    uint32_t vp, fp;
    if (s38 == 0) {
        if (!(r26 & 0x20u)) {
            vp = 0; fp = pick_fp(fA8, fC8, f31, fgt(fA8, f31));
        } else if (r23 == 0) {
            if (r22) { vp = 9; fp = 0xDA; }
            else {
                vp = 0;
                if (fle(fA8, f31)) fp = fgt(fC8, f31) ? 0xA3u : 0x6Cu;
                else fp = fgt(fC8, f31) ? 0x6Du : 0x88u;
            }
        } else if (b5c == 0) {
            vp = 2; fp = pick_fp(fA8, fC8, f31, fgt(fA8, f31));
        } else if (!(r27 & 0x40u)) {
            vp = 2; fp = fle(fC8, f31) ? 0xD8u : 0xD9u;
        } else {
            vp = 4; fp = fgt(fC8, f31) ? 0xD9u : 0xD8u;
        }
    } else {
        if (b5c == 0) {
            vp = 0xD; fp = pick_fp(fA8, fC8, f31, fgt(fA8, f31));
        } else if (r23 == 0) {
            vp = 0xB;
            if (fgt(fA8, f31)) fp = fgt(fC8, f31) ? 0x6Du : 0x88u;
            else fp = fgt(fC8, f31) ? 0xA3u : 0x6Cu;
        } else if (!(r27 & 0x40u)) {
            vp = 0xD; fp = fle(fC8, f31) ? 0xD8u : 0xD9u;
        } else {
            vp = 0xF; fp = fgt(fC8, f31) ? 0xD9u : 0xD8u;
        }
    }
    if (g8(blk + 0x35u)) {
        vp = s38 ? 0xEu : 3u;
        fp = pick_fp(fA8, fC8, f31, !fle(fA8, f31));
    }
    if (g8(blk + 0x36u)) { vp = 0x1D; fp = 0xDB; }
    if (g8(blk + 0x37u)) fp = pick_fp(fA8, fC8, f31, !fle(fA8, f31));
    if (g8(r31 + 0xB14u)) {
        fp = 0xDC;
        c->gpr[4] = 0; c->gpr[5] = 2; c->gpr[6] = 2; c->gpr[7] = 0;
        k.method(renderer(), 0x88u, 8, 9);
        c->gpr[4] = g32(r31 + 0xA48u); c->gpr[5] = 0;
        k.method(renderer(), 0xB8u, 6, 7);
    }
    const uint32_t fslot = (uint32_t)(r31 + (uint64_t)((fp << 3) & ~7u) + 0xF8u);
    c->gpr[4] = (uint32_t)(r31 + (uint64_t)((vp << 3) & ~7u));
    c->gpr[5] = fslot;
    k.method(renderer(), 0xA8u, 6, 7);
    const bool lit = fp == 0xD8u || fp == 0xD9u;
    const uint32_t prog = g32(fslot);             // fragment program object

    /* Fragment program constants. */
    uint32_t b84 = g8(blk + 0x84u);
    if (b84 || g8(0x00410000u - 0x28D0u)) {
        const double a = gf(r31 + 0x8E0u), b = gf(r31 + 0x8E4u);
        pf(sp + 0x8Cu, a); pf(sp + 0x90u, b);
        c->fpr[1] = a; c->fpr[2] = b;
        set_const(k, 0x124u, 2, sp + 0x8Cu);
        b84 = g8(blk + 0x84u);
    }
    p8(0x00410000u - 0x28D0u, b84);
    if (g8(blk + 0x35u)) set_const(k, 0x126u, 1, 0x004736A0u);
    if (g8(blk + 0x36u)) {
        const double b = gf(kConst + 0x14u);
        const double a = ppu_fp_single(ppu_fadd(gf(sp + 0xB8u), b));
        pf(sp + 0xB8u, a);
        c->fpr[1] = a; c->fpr[2] = b;
        set_const(k, 0x127u, 0x10u, 0x005D5360u);
    }
    if (lit) {
        const double a = g16(r29 + 0x16u) == 0 ? gf(kConst + 0x10u) : f31;
        pf(sp + 0x88u, a);
        c->fpr[1] = a;
        set_const(k, 0x130u, 1, sp + 0x88u);
    }
    set_const(k, 0x125u, 4, sp + 0xACu);
    {
        const double a = gf(sp + 0xA8u);
        c->fpr[1] = a;
        if (!fle(a, f31)) {
            fp_bind(k, prog, 0, 0x002C0000u - 28012u, 0x00033634u);
            fp_set(k, prog, 0, 4, sp + 0x9Cu, 0x0003364Cu);
        }
    }
    {
        const double a = gf(sp + 0xC8u);
        c->fpr[1] = a;
        if (!fle(a, f31) && g8(blk + 0x37u) == 0 && g8(blk + 0x36u) == 0) {
            const double kk = gf(kConst + 8u);
            const double f3 = gf(0x00480000u - 0x762Cu);
            const double f2 = ppu_fp_single(ppu_fmul(gf(0x00410000u - 0x3894u), kk));
            const double f1b = ppu_fp_single(ppu_fmul(f3, kk));
            pf(sp + 0x94u, f2); pf(sp + 0x98u, f1b);
            c->fpr[1] = f1b; c->fpr[2] = f2; c->fpr[3] = f3;
            fp_bind(k, prog, 2, 0x002C0000u - 28004u, 0x000336B8u);
            fp_bind(k, prog, 3, 0x002C0000u - 27988u, 0x000336D0u);
            fp_set(k, prog, 2, 4, sp + 0xBCu, 0x000336E8u);
            fp_set(k, prog, 3, 2, sp + 0x94u, 0x00033700u);
        }
    }
    {
        const uint32_t l = 0x005D5480u;
        p32(sp + 0xD8u, 0);
        pf(sp + 0xCCu, gf(l)); pf(sp + 0xD0u, gf(l + 0x10u)); pf(sp + 0xD4u, gf(l + 0x20u));
        c->fpr[1] = gf(l + 0x20u);
    }
    if (lit) {
        k.method(renderer(), 0x38u, 4, 5);
        const uint32_t m = (uint32_t)c->gpr[3];
        pf(sp + 0xDCu, gf(m + 0x30u)); pf(sp + 0xE0u, gf(m + 0x34u));
        pf(sp + 0xE4u, gf(m + 0x38u)); pf(sp + 0xE8u, gf(m + 0x3Cu));
        c->fpr[1] = gf(m + 0x3Cu);
        fp_bind(k, prog, 1, 0x002C0000u - 28036u, 0x00033798u);
        fp_bind(k, prog, 5, 0x002C0000u - 28028u, 0x000337B0u);
        fp_bind(k, prog, 8, 0x002C0000u - 27976u, 0x000337C8u);
        fp_set(k, prog, 1, 1, 0x00480000u - 26004u, 0x000337E4u);
        fp_set(k, prog, 5, 4, sp + 0xCCu, 0x000337FCu);
        fp_set(k, prog, 8, 4, sp + 0xDCu, 0x00033814u);
    }
    if (r22) {
        k.method(renderer(), 0x38u, 4, 5);
        const uint32_t m = (uint32_t)c->gpr[3];
        pf(sp + 0xECu, gf(m + 0x30u)); pf(sp + 0xF0u, gf(m + 0x34u));
        pf(sp + 0xF4u, gf(m + 0x38u)); pf(sp + 0xF8u, gf(m + 0x3Cu));
        c->fpr[1] = gf(m + 0x3Cu);
        fp_bind(k, prog, 8, 0x002C0000u - 27976u, 0x0003387Cu);
        fp_set(k, prog, 8, 4, sp + 0xECu, 0x00033894u);
    }
    if (g8(r31 + 0xB14u)) {
        p32(sp + 0x108u, 0);
        const double a = gf(r31 + 0xB1Cu), b = gf(r31 + 0xB20u), d = gf(r31 + 0xB18u);
        pf(sp + 0xFCu, a); pf(sp + 0x100u, b); pf(sp + 0x104u, d);
        c->fpr[1] = d; c->fpr[2] = b;
        fp_bind(k, prog, 0x16u, g32(0x00473BFCu + 0x58u), 0x000338D8u);
        fp_set(k, prog, 0x16u, 4, sp + 0xFCu, 0x000338F0u);
    }
    c->gpr[3] = prog;
    k.direct(0x000338FCu, func_00053A7C);

    /* The draw call: renderer +0x70. */
    {
        const uint32_t n = g32(r29 + 0x34u);
        if (n && g32(r29 + 0x2Cu)) {
            const int32_t t = (int32_t)g32(r29 + 0x20u);
            if (t == 4 || t == 5) {
                c->gpr[5] = g32(r25 + 0x24u);
                c->gpr[6] = g32(r29 + 0x24u);
                c->gpr[7] = g32(r29 + 0x30u);
                if (t == 4) {
                    c->gpr[4] = 5;
                    c->gpr[8] = (uint32_t)((uint64_t)n * 3u);
                    k.method(renderer(), 0x70u, 10, 9);
                } else {
                    c->gpr[4] = 6;
                    c->gpr[8] = (uint32_t)((uint64_t)n + 2u);
                    k.method(renderer(), 0x70u, 9, 10);
                }
            }
        }
    }
    if (g8(blk + 0x37u) == 0) {
        c->gpr[4] = 0x1E;
        k.method(renderer(), 0x50u, 5, 6);
    }

    c->gpr[0] = lr0;
    c->lr = lr0;
    c->gpr[1] = sp0;
}

/* ---- dispatch and verification ------------------------------------------- */
bool sd_flag(const char* name)
{
    char path[128];
    snprintf(path, sizeof path, "sdmc:/switch/dbz-budokai-3-switch/%s", name);
    FILE* f = fopen(path, "r");
    if (f) fclose(f);
    return f != nullptr;
}

typedef void (*Fn)(ppu_context*);

int s_mode;        // 0 = not decided, 1 = lifted, 2 = self-test, 3 = native, 4 = verify native
int s_depth;

#ifdef DBZ3_WTRACK
/* ---- byte map of one transaction: address -> {value before, value after} -- */
const unsigned kMapSlots = 1u << 16;
struct Byte { uint32_t addr; uint8_t pre, post, used; };
Byte s_map[kMapSlots];
unsigned s_map_n;
uint32_t s_used_list[kMapSlots];

Byte* map_find(uint32_t a, bool create)
{
    unsigned h = (a * 2654435761u) & (kMapSlots - 1u);
    for (unsigned p = 0; p < kMapSlots; p++, h = (h + 1u) & (kMapSlots - 1u)) {
        Byte& b = s_map[h];
        if (b.used && b.addr == a) return &b;
        if (!b.used) {
            if (!create || s_map_n >= kMapSlots / 2) return nullptr;
            b.used = 1; b.addr = a;
            s_used_list[s_map_n++] = h;
            return &b;
        }
    }
    return nullptr;
}
void map_clear()
{
    for (unsigned i = 0; i < s_map_n; i++) s_map[s_used_list[i]].used = 0;
    s_map_n = 0;
}

struct Range { uint32_t lo, hi; };
inline bool in(const Range& r, uint32_t a) { return a - r.lo < r.hi - r.lo; }

/* The heap manager the shader release path writes (run74); a call that frees
 * a shader is not replayed. */
const Range kHeap = { 0x0047C000u, 0x00480000u };

struct Stats { uint64_t calls, verified, mismatch, skip_room, skip_heap, overflow, map_full; };
Stats s_st;
uint64_t s_window;
FILE* s_log;
unsigned s_logged;

void log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char* fmt, ...)
{
    if (!s_log) {
        s_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t36-mesh.log", "w");
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
    log_line("window 0x000328A8 calls=%llu verified=%llu mismatch=%llu skip_room=%llu skip_heap=%llu "
             "undo_overflow=%llu map_full=%llu\n",
             (unsigned long long)s_st.calls, (unsigned long long)s_st.verified,
             (unsigned long long)s_st.mismatch, (unsigned long long)s_st.skip_room,
             (unsigned long long)s_st.skip_heap, (unsigned long long)s_st.overflow,
             (unsigned long long)s_st.map_full);
    s_st = Stats{};
    s_window = now;
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
/* What a caller may rely on after the call: r1, r2, r13-r31, f14-f31, cr2-cr4, lr. */
bool same_nonvolatile(const Regs& a, const Regs& b)
{
    if (a.gpr[1] != b.gpr[1] || a.gpr[2] != b.gpr[2] || a.lr != b.lr) return false;
    for (int i = 13; i < 32; i++) if (a.gpr[i] != b.gpr[i]) return false;
    if (memcmp(&a.fpr[14], &b.fpr[14], 18 * sizeof(double))) return false;
    return (a.cr & 0x00FFF000u) == (b.cr & 0x00FFF000u);
}

const uint32_t kCmdMax = 0x8000u;
uint8_t s_cmdA[kCmdMax];

/* Runs `first`, then `second` from the same state, and compares. Leaves the
 * result of `first` in place when they differ. */
void verify_call(ppu_context* c, Fn first, Fn second)
{
    const uint32_t g = g32(kGcmCtxPtr);
    const uint32_t cur0 = g32(g + 8u);
    if (g32(g + 4u) - cur0 < kCmdMax) { s_st.skip_room++; first(c); return; }
    const Range cmd = { g32(g), g32(g + 4u) + 4u };
    const uint32_t r1 = (uint32_t)c->gpr[1];
    const Range stack = { r1 - 0x20000u, r1 + 0x40u };   // callees' frames, dead after the call
    Regs pre; save_regs(c, pre);
    uint8_t gctx0[16], gctxA[16];
    memcpy(gctx0, vm_base + g, 16);

    // Run A.
    dbz3_undo_begin();
    first(c);
    dbz3_undo_end();
    if (dbz3_undo_overflow()) { s_st.overflow++; return; }      // keep A, nothing to compare
    Regs postA; save_regs(c, postA);
    memcpy(gctxA, vm_base + g, 16);
    const uint32_t lenA = g32(g + 8u) - cur0;
    if (lenA > kCmdMax) { s_st.skip_room++; return; }
    memcpy(s_cmdA, vm_base + cur0, lenA);

    map_clear();
    const unsigned nA = dbz3_undo_count();
    for (unsigned i = 0; i < nA; i++) {
        uint32_t a, n; const uint8_t* old;
        dbz3_undo_entry(i, &a, &n, &old);
        if (in(kHeap, a)) { s_st.skip_heap++; return; }           // keep A
        for (uint32_t j = 0; j < n; j++) {
            const uint32_t x = a + j;
            if (in(cmd, x) || in(stack, x)) continue;
            Byte* b = map_find(x, true);
            if (!b) { s_st.map_full++; return; }                    // keep A
            if (b->used == 1) { b->pre = old[j]; b->used = 2; }      // oldest value first
        }
    }
    for (unsigned i = 0; i < s_map_n; i++) { Byte& b = s_map[s_used_list[i]]; b.post = vm_base[b.addr]; }

    // Back to the state before A, run B.
    dbz3_undo_rollback();
    memcpy(vm_base + g, gctx0, 16);
    load_regs(c, pre);
    dbz3_undo_begin();
    second(c);
    dbz3_undo_end();
    Regs postB; save_regs(c, postB);

    bool same = !dbz3_undo_overflow() && !memcmp(gctxA, vm_base + g, 16) &&
                g32(g + 8u) - cur0 == lenA && !memcmp(s_cmdA, vm_base + cur0, lenA) &&
                same_nonvolatile(postA, postB);
    unsigned shown = 0;
    for (unsigned i = 0; i < s_map_n; i++) {           // bytes A wrote
        const Byte& b = s_map[s_used_list[i]];
        if (vm_base[b.addr] != b.post) {
            same = false;
            if (shown++ < 8 && s_logged < 40)
                log_line("  0x%08X before=%02X first=%02X second=%02X\n", b.addr, b.pre, b.post, vm_base[b.addr]);
        }
    }
    const unsigned nB = dbz3_undo_count();
    for (unsigned i = 0; i < nB; i++) {                 // bytes only B wrote must be unchanged
        uint32_t a, n; const uint8_t* old;
        dbz3_undo_entry(i, &a, &n, &old);
        for (uint32_t j = 0; j < n; j++) {
            const uint32_t x = a + j;
            if (in(cmd, x) || in(stack, x) || map_find(x, false)) continue;
            if (vm_base[x] != old[j]) {
                same = false;
                if (shown++ < 8 && s_logged < 40)
                    log_line("  0x%08X before=%02X first=(unwritten) second=%02X\n", x, old[j], vm_base[x]);
            }
        }
    }
    s_st.verified++;
    if (same) return;
    s_st.mismatch++;
    if (s_logged++ < 40) {
        log_line("MISMATCH 0x000328A8 r3=0x%08X r4=0x%08X r5=0x%08X cmd lifted=%u second=%u regs %s\n",
                 (uint32_t)pre.gpr[3], (uint32_t)pre.gpr[4], (uint32_t)pre.gpr[5], lenA,
                 g32(g + 8u) - cur0, same_nonvolatile(postA, postB) ? "same" : "differ");
        const uint32_t n = lenA < g32(g + 8u) - cur0 ? lenA : g32(g + 8u) - cur0;
        unsigned cs = 0;
        for (uint32_t o = 0; o + 4 <= n && cs < 8; o += 4) {
            uint32_t wa; memcpy(&wa, s_cmdA + o, 4); wa = __builtin_bswap32(wa);
            const uint32_t wb = g32(cur0 + o);
            if (wa != wb) { log_line("  cmd+0x%04X first=%08X second=%08X\n", o, wa, wb); cs++; }
        }
    }

    // Keep A: undo B, then restore A's bytes, commands, context and registers.
    if (!dbz3_undo_overflow()) dbz3_undo_rollback();
    for (unsigned i = 0; i < s_map_n; i++) { const Byte& b = s_map[s_used_list[i]]; vm_base[b.addr] = b.post; }
    memcpy(vm_base + cur0, s_cmdA, lenA);
    memcpy(vm_base + g, gctxA, 16);
    load_regs(c, postA);
}
#endif

void decide()
{
    s_mode = sd_flag("native-mesh-off") ? 1 : 3;
#ifdef DBZ3_WTRACK
    if (sd_flag("mesh-selftest")) s_mode = 2;
    else if (s_mode == 3 && sd_flag("mesh-verify")) s_mode = 4;
#endif
    static const char* const kMode[] = { "", "lifted", "lifted, self-test of the undo-log harness",
                                         "native", "native + verify" };
    fprintf(stderr, "[native-A] mesh body 0x000328A8: %s\n", kMode[s_mode]);
#ifdef DBZ3_WTRACK
    if (s_mode == 2) log_line("mesh harness self-test: lifted vs lifted\n");
    if (s_mode == 4) log_line("mesh body verify: lifted vs native\n");
#endif
}

void dispatch(ppu_context* c)
{
    if (__builtin_expect(s_mode == 0, 0)) decide();
    DBZ3_NATIVE_STAMP(c, 0x000328A8u);
#ifdef DBZ3_WTRACK
    if ((s_mode == 2 || s_mode == 4) && s_depth == 0) {
        s_depth++;
        s_st.calls++;
        verify_call(c, func_000328A8_lifted, s_mode == 4 ? native_328A8 : func_000328A8_lifted);
        s_depth--;
        window_tick();
        return;
    }
#endif
    if (s_mode == 3) native_328A8(c);
    else func_000328A8_lifted(c);
}

}  // namespace

void func_000328A8(ppu_context* ctx) { dispatch(ctx); }
