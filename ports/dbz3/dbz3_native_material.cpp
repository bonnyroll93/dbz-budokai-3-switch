/*
 * DBZ3 native-A step 1: native material apply (guest 0x0002C088).
 *
 * The game calls 0x0002C088(material, renderer) about 250 times a frame in a
 * fight. It reads the material description field by field and, for each
 * state, calls a method of the renderer object (vtable 0x002C4690), which
 * calls a libgcm writer that appends one RSX method to the command buffer.
 * The lifted chain runs ~40 small functions per call; this is the same work
 * as one native function.
 *
 * Only the caller-visible effects are reproduced (t32-wtrack.log, run69):
 *   material +0x37, +0x38 (bytes)
 *   renderer +0x284 (byte), +0x288..+0x294, +0x29C, +0x2A0 (floats)
 *   GCM context current pointer and the command words it covers
 * Volatile registers are not reproduced (the PPC ABI does not let a caller
 * read them after a call). Arithmetic uses the lifted code's own helpers
 * (ppu_fsub, ppu_fp_single, ... from the lifted prelude) in the same order, so
 * every value is bit-identical. A writer that finds the buffer full calls the
 * lifted writer instead, which runs the game's callback exactly as before.
 *
 * Off with sdmc:/switch/dbz-budokai-3-switch/native-material-off. With
 * native-verify present, every call runs the lifted function first, then the
 * native one from the same state, compares the results byte for byte and
 * keeps the lifted result on any difference (t33-native.log).
 */
#define PPU_FAST_VM 1
#include "dbz3_lifted_prelude.inc"
#include "dbz3_store_hook.h"

#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

/* The lifted body, kept under this name by dbz3_weak_overrides.h. */
void func_0002C088_lifted(ppu_context* ctx);

namespace {

/* ---- guest memory (all addresses used here are in plain guest RAM) ----- */
inline uint8_t  r8(uint32_t a)  { return vm_base[a]; }
inline uint16_t r16(uint32_t a) { uint16_t v; memcpy(&v, vm_base + a, 2); return __builtin_bswap16(v); }
inline uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }
inline double   rf(uint32_t a)  { const uint32_t w = r32(a); float f; memcpy(&f, &w, 4); return f; }   // lfs
inline void w8(uint32_t a, uint32_t v)  { dbz3_store_hook(a, 1u); vm_base[a] = (uint8_t)v; }
inline void w32(uint32_t a, uint32_t v) { dbz3_store_hook(a, 4u); v = __builtin_bswap32(v); memcpy(vm_base + a, &v, 4); }
inline uint32_t fb(double d) { const float f = (float)d; uint32_t w; memcpy(&w, &f, 4); return w; }      // stfs

const uint32_t kRendererVtable = 0x002C4690u;
const uint32_t kGcmCtxPtr = 0x005D5610u;      // CellGcmContextData*: begin, end, current, callback
const uint32_t kGameState = 0x00473BF8u;      // pointer read by the material code

inline uint32_t rl1(uint64_t x) { return ppc_rlwinm((uint32_t)x, 1, 0, 30); }
inline uint32_t rl2(uint64_t x) { return ppc_rlwinm((uint32_t)x, 2, 0, 29); }
inline uint32_t nz(uint64_t b) { return (b & 0xFFu) ? 1u : 0u; }

/* ---- libgcm writers ------------------------------------------------------
 * Each takes the arguments its lifted writer gets in r4.. / f1, f2. With room
 * for the method it appends it; otherwise it runs the lifted writer, which
 * calls the context's callback and then writes (or gives up) as before. */
typedef void (*LiftedFn)(ppu_context*);

inline bool room(uint32_t g, uint32_t bytes, uint32_t& cur)
{
    cur = r32(g + 8u);
    return (uint32_t)(cur + bytes) <= r32(g + 4u);
}

inline void call_lifted(ppu_context* ctx, LiftedFn fn)
{
    fn(ctx);
    DRAIN_TRAMPOLINE(ctx);
}

/* One data word: header 0x0004xxxx (FBC04, FB59C, FB630, FB6C4, FB994, FBADC,
 * FBB70, FBC98, FBD2C, FC200). */
void gcm1(ppu_context* ctx, uint32_t g, uint32_t method, uint64_t a, LiftedFn lifted)
{
    uint32_t cur;
    if (!room(g, 8u, cur)) {
        ctx->gpr[3] = g; ctx->gpr[4] = a;
        call_lifted(ctx, lifted);
        return;
    }
    w32(g + 8u, cur + 8u);
    w32(cur + 4u, (uint32_t)a);
    w32(cur, 0x00040000u | method);
}

/* 0x001FB078: NV method 0x308, two words (r4, r5). */
void gcm_308(ppu_context* ctx, uint32_t g, uint64_t a, uint64_t b)
{
    uint32_t cur;
    if (!room(g, 12u, cur)) {
        ctx->gpr[3] = g; ctx->gpr[4] = a; ctx->gpr[5] = b;
        call_lifted(ctx, func_001FB078);
        return;
    }
    w32(g + 8u, cur + 12u);
    w32(cur + 8u, (uint32_t)b);
    w32(cur, 0x00080308u);
    w32(cur + 4u, (uint32_t)a);
}

/* 0x001FB11C: method 0x320, (r5 << 16) | (r4 & 0xFFFF). */
void gcm_320(ppu_context* ctx, uint32_t g, uint64_t a, uint64_t b)
{
    uint32_t cur;
    if (!room(g, 8u, cur)) {
        ctx->gpr[3] = g; ctx->gpr[4] = a; ctx->gpr[5] = b;
        call_lifted(ctx, func_001FB11C);
        return;
    }
    const uint32_t w = ppc_rlwinm((uint32_t)a, 0, 16, 31) | ppc_rlwinm((uint32_t)b, 16, 0, 15);
    w32(cur + 4u, w);
    w32(g + 8u, cur + 8u);
    w32(cur, 0x00040320u);
}

/* 0x001FB1C8: method 0x314, two packed words from r4..r7. */
void gcm_314(ppu_context* ctx, uint32_t g, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    uint32_t cur;
    if (!room(g, 12u, cur)) {
        ctx->gpr[3] = g; ctx->gpr[4] = a; ctx->gpr[5] = b; ctx->gpr[6] = c; ctx->gpr[7] = d;
        call_lifted(ctx, func_001FB1C8);
        return;
    }
    const uint32_t w8_ = ppc_rlwinm((uint32_t)b, 0, 16, 31) | ppc_rlwinm((uint32_t)d, 16, 0, 15);
    const uint32_t w4 = ppc_rlwinm((uint32_t)a, 0, 16, 31) | ppc_rlwinm((uint32_t)c, 16, 0, 15);
    w32(cur + 8u, w8_);
    w32(g + 8u, cur + 12u);
    w32(cur, 0x00080314u);
    w32(cur + 4u, w4);
}

/* 0x001FB758 (method 0xA78) and 0x001FC294 (method 0x8D0): two floats. */
void gcm_2f(ppu_context* ctx, uint32_t g, uint32_t method, double f1, double f2, LiftedFn lifted)
{
    uint32_t cur;
    if (!room(g, 12u, cur)) {
        ctx->gpr[3] = g; ctx->fpr[1] = f1; ctx->fpr[2] = f2;
        call_lifted(ctx, lifted);
        return;
    }
    w32(g + 8u, cur + 12u);
    w32(cur + 8u, fb(f2));
    w32(cur, 0x00080000u | method);
    w32(cur + 4u, fb(f1));
}

/* 0x001FB8E0 (method 0x330) and 0x001FBA28 (method 0x33C): three words. */
void gcm_3(ppu_context* ctx, uint32_t g, uint32_t method, uint64_t a, uint64_t b, uint64_t c,
           LiftedFn lifted)
{
    uint32_t cur;
    if (!room(g, 16u, cur)) {
        ctx->gpr[3] = g; ctx->gpr[4] = a; ctx->gpr[5] = b; ctx->gpr[6] = c;
        call_lifted(ctx, lifted);
        return;
    }
    w32(g + 8u, cur + 16u);
    w32(cur + 12u, (uint32_t)c);
    w32(cur, 0x000C0000u | method);
    w32(cur + 4u, (uint32_t)a);
    w32(cur + 8u, (uint32_t)b);
}

/* 0x001FC484: method (0x1A14 + (r4 << 5)), packed from r5..r8. */
void gcm_1a14(ppu_context* ctx, uint32_t g, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
    uint32_t cur;
    if (!room(g, 8u, cur)) {
        ctx->gpr[3] = g; ctx->gpr[4] = a; ctx->gpr[5] = b; ctx->gpr[6] = c;
        ctx->gpr[7] = d; ctx->gpr[8] = e;
        call_lifted(ctx, func_001FC484);
        return;
    }
    const uint32_t w = ppc_rlwinm((uint32_t)b, 0, 19, 31) | ppc_rlwinm((uint32_t)c, 16, 8, 15) |
                       ppc_rlwinm((uint32_t)e, 13, 11, 18) | ppc_rlwinm((uint32_t)d, 24, 0, 7);
    const uint32_t hdr = (uint32_t)(((uint64_t)ppc_rlwinm((uint32_t)a, 5, 19, 26) + 0x1A14u) | 0x40000u);
    w32(cur + 4u, w);
    w32(g + 8u, cur + 8u);
    w32(cur, hdr);
}

/* 0x001FC568: method (0x1A08 + (r4 << 5)), packed from r5..r10. */
void gcm_1a08(ppu_context* ctx, uint32_t g, uint64_t a, uint64_t b, uint64_t c, uint64_t d,
              uint64_t e, uint64_t f, uint64_t h)
{
    uint32_t cur;
    if (!room(g, 8u, cur)) {
        ctx->gpr[3] = g; ctx->gpr[4] = a; ctx->gpr[5] = b; ctx->gpr[6] = c;
        ctx->gpr[7] = d; ctx->gpr[8] = e; ctx->gpr[9] = f; ctx->gpr[10] = h;
        call_lifted(ctx, func_001FC568);
        return;
    }
    // r24=a r28=b r27=c r30=d r29=e r26=f r25=h
    const uint32_t w = ppc_rlwinm((uint32_t)b, 0, 24, 31) | ppc_rlwinm((uint32_t)c, 8, 16, 23) |
                       ppc_rlwinm((uint32_t)d, 16, 8, 15) | ppc_rlwinm((uint32_t)e, 12, 12, 19) |
                       ppc_rlwinm((uint32_t)f, 28, 0, 3) | ppc_rlwinm((uint32_t)h, 20, 4, 11);
    const uint32_t hdr = (uint32_t)(((uint64_t)ppc_rlwinm((uint32_t)a, 5, 19, 26) + 0x1A08u) | 0x40000u);
    w32(cur + 4u, w);
    w32(g + 8u, cur + 8u);
    w32(cur, hdr);
}

/* ---- renderer methods (vtable 0x002C4690); R = the renderer object ------ */
inline uint32_t gcm_ctx() { return r32(kGcmCtxPtr); }

void m074(ppu_context* c, uint64_t a) { gcm1(c, gcm_ctx(), 0x183C, nz(a), func_001FBC04); }   // 0x4F018
void m078(ppu_context* c, uint64_t i) { gcm1(c, gcm_ctx(), 0x1830, r32(0x2B98A0u + rl2(i)), func_001FB59C); }
void m07C(ppu_context* c, uint64_t a) { gcm1(c, gcm_ctx(), 0xA74, nz(a), func_001FBC98); }
void m080(ppu_context* c, uint64_t i) { gcm1(c, gcm_ctx(), 0xA6C, r32(0x2B98A8u + rl2(i)), func_001FB630); }
void m084(ppu_context* c, uint64_t a) { gcm1(c, gcm_ctx(), 0xA70, nz(a), func_001FB6C4); }
void m088(ppu_context* c, uint64_t a, uint64_t b, uint64_t d6, uint64_t d7)          // 0x4F14C
{
    const uint32_t g = gcm_ctx();
    gcm_1a08(c, g, a & 0xFFu, r8((uint32_t)(b + 0x2B98C8u)), r8((uint32_t)(d6 + 0x2B98C8u)),
             5, 0, r8((uint32_t)(d7 + 0x2B98CBu)), 0);
}
void m08C(ppu_context* c, uint64_t a, uint64_t b, uint64_t d)                        // 0x4F1A4
{
    const uint32_t g = gcm_ctx();
    gcm_1a14(c, g, a & 0xFFu, 0, r8((uint32_t)(b + 0x2B98D3u)), r8((uint32_t)(d + 0x2B98D3u)), 1);
}
void m090(ppu_context* c, uint64_t a) { gcm1(c, gcm_ctx(), 0x310, nz(a), func_001FBB70); }
void m094(ppu_context* c, uint64_t a, uint64_t b)                                    // 0x4F230
{
    gcm_320(c, gcm_ctx(), r16(0x2B98D6u + rl1(a)), r16(0x2B98D6u + rl1(b)));
}
void m098(ppu_context* c, uint64_t a, uint64_t b, uint64_t d6, uint64_t d7)          // 0x4F278
{
    const uint32_t t = 0x2B98E6u;
    gcm_314(c, gcm_ctx(), r16(t + rl1(a)), r16(t + rl1(b)), r16(t + rl1(d6)), r16(t + rl1(d7)));
}
void m09C(ppu_context* c, uint64_t a) { gcm1(c, gcm_ctx(), 0x304, nz(a), func_001FBADC); }
void m0A0(ppu_context* c, uint64_t i, uint64_t x) { gcm_308(c, gcm_ctx(), r32(0x2B9904u + rl2(i)), x); }
void m0E4(ppu_context* c, uint32_t R, double f1)                                     // 0x52028
{
    const double k0 = rf(0x0005201Cu), k1 = rf(0x00052020u);
    const double f5 = ppu_fsub(k0, f1);
    const double f3 = ppu_fp_single(ppu_fmul(f1, k1));
    const double f6 = ppu_fsub(f1, k0);
    const double f4 = rf(R + 0x2A0u);
    double f2 = (f5 >= 0.0) ? k0 : f3;
    f2 = (f6 >= 0.0) ? f2 : f3;
    w32(R + 0x29Cu, fb(f2));
    gcm_2f(c, gcm_ctx(), 0xA78, f4, f2, func_001FB758);
}
void m0E8(ppu_context* c, uint32_t R, double f1)                                     // 0x52088
{
    w32(R + 0x2A0u, fb(f1));
    const double f2 = rf(R + 0x29Cu);
    gcm_2f(c, gcm_ctx(), 0xA78, f1, f2, func_001FB758);
}
void m104(ppu_context* c, uint64_t a) { gcm1(c, gcm_ctx(), 0x328, nz(a), func_001FBD2C); }
void m108(ppu_context* c, uint64_t a, uint64_t b, uint64_t d6)                       // 0x520FC
{
    gcm_3(c, gcm_ctx(), 0x330, r16(0x2B9954u + rl1(a)), (uint64_t)(int64_t)(int32_t)b, d6, func_001FB8E0);
}
void m10C(ppu_context* c, uint64_t a) { gcm1(c, gcm_ctx(), 0x32C, a, func_001FB994); }
void m110(ppu_context* c, uint64_t a, uint64_t b, uint64_t d6)                       // 0x5216C
{
    const uint32_t t = 0x2B9964u;
    gcm_3(c, gcm_ctx(), 0x33C, r16(t + rl1(a)), r16(t + rl1(b)), r16(t + rl1(d6)), func_001FBA28);
}
void m114(ppu_context* c, uint32_t R, uint64_t a)                                    // 0x521C0
{
    w8(R + 0x284u, (uint32_t)a);
    if (a & 0xFFu) return;
    gcm1(c, gcm_ctx(), 0x8CC, 0x800, func_001FC200);
    const double f = rf(0x000521B8u);
    gcm_2f(c, gcm_ctx(), 0x8D0, f, f, func_001FC294);
}
void m118(ppu_context* c, uint32_t R, double a1, double a2, double a3, double a4)    // 0x52228
{
    if (!r8(R + 0x284u)) return;
    const double k = rf(0x00052224u);
    const double d = ppu_fp_single(ppu_fsub(a2, a1));
    const double q = ppu_fp_single(ppu_fdiv(k, d));
    w32(R + 0x290u, fb(a3));
    w32(R + 0x294u, fb(a4));
    const double s = ppu_fp_single(ppu_fmadd_core(a2, q, k, 0, 0));
    const double n = -q;
    w32(R + 0x288u, fb(s));
    w32(R + 0x28Cu, fb(n));
    gcm1(c, gcm_ctx(), 0x8CC, 0x2601, func_001FC200);
    gcm_2f(c, gcm_ctx(), 0x8D0, rf(R + 0x288u), rf(R + 0x28Cu), func_001FC294);
}

/* ---- 0x0002C088 --------------------------------------------------------- */
void material_native(ppu_context* c, uint32_t m, uint32_t R)
{
    w8(m + 0x37u, r8(r32(kGameState) + 0xB10u));
    m090(c, r8(m + 0x04u));
    if (r8(m + 0x04u)) {
        m094(c, r32(m + 0x08u), r32(m + 0x14u));
        m098(c, r32(m + 0x0Cu), r32(m + 0x10u), r32(m + 0x18u), r32(m + 0x1Cu));
    }
    m09C(c, r8(m + 0x20u));
    if (r8(m + 0x20u)) m0A0(c, r32(m + 0x28u), r32(m + 0x24u));
    m104(c, r32(m + 0x40u) ? 1u : 0u);
    if (r32(m + 0x40u)) {
        m10C(c, r32(m + 0x3Cu));
        m110(c, r32(m + 0x50u), r32(m + 0x54u), r32(m + 0x58u));
        m108(c, r32(m + 0x44u), r32(m + 0x48u), r32(m + 0x4Cu));
    }
    m07C(c, r8(m + 0x2Cu));
    if (r8(m + 0x2Cu)) m080(c, r32(m + 0x30u));
    m084(c, r8(m + 0x34u));
    {
        const uint32_t st = r32(kGameState);
        const uint32_t on = r8(st + 0xB29u);
        const uint32_t v = r32(st + 0xB2Cu);
        if (on) {
            m074(c, 1);
            m078(c, v);
        } else {
            m074(c, r8(m + 0x35u));
            if (r8(m + 0x35u)) m078(c, 0);
        }
    }
    if (r8(m + 0x36u)) {
        m088(c, 0, 1, 1, 0);
        m08C(c, 0, 0, 0);
        if (r8(m + 0x5Cu)) {
            m088(c, 1, r32(m + 0x60u), r32(m + 0x64u), 0);
            m08C(c, 1, r32(m + 0x68u), r32(m + 0x6Cu));
        }
    } else if (r8(m + 0x5Cu)) {
        m088(c, 0, r32(m + 0x60u), r32(m + 0x64u), 0);
        m08C(c, 0, r32(m + 0x68u), r32(m + 0x6Cu));
    }
    if (r8(m + 0x70u)) {
        m088(c, 1, r32(m + 0x74u), r32(m + 0x78u), 0);
        m08C(c, 1, r32(m + 0x7Cu), r32(m + 0x80u));
    }
    if (r32(m + 0x00u) & 0x80u) {
        m114(c, R, 1);
        const double k = rf(0x0002C084u);
        const double f1 = rf(0x0040C760u), f2 = rf(0x0040C764u);
        const double f5 = rf(0x004789D4u), f6 = rf(0x0040C76Cu);
        const double f3 = ppu_fp_single(ppu_fmul(f6, k));
        const double f4 = ppu_fp_single(ppu_fmul(f5, k));
        m118(c, R, f1, f2, f3, f4);
    } else {
        m114(c, R, 0);
    }
    m0E4(c, R, rf(0x00478984u));
    m0E8(c, R, rf(0x00478988u));
    w8(m + 0x38u, r8(r32(kGameState) + 0xB13u));
}

/* ---- safety check and switches ------------------------------------------ */
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

int s_mode;   // 0 = not decided, -1 = lifted only, 1 = native, 2 = native + verify

/* ---- verification -------------------------------------------------------- */
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

const uint32_t kRendLo = 0x284u, kRendLen = 0x20u;   // renderer +0x284..+0x2A3
const uint32_t kCmdMax = 1024u;                     // verified calls need this much room

struct State {
    uint8_t mat[2];
    uint8_t rend[kRendLen];
    uint8_t gctx[16];
};
void grab(uint32_t m, uint32_t R, uint32_t g, State& s)
{
    s.mat[0] = r8(m + 0x37u); s.mat[1] = r8(m + 0x38u);
    memcpy(s.rend, vm_base + R + kRendLo, kRendLen);
    memcpy(s.gctx, vm_base + g, 16);
}
void put(uint32_t m, uint32_t R, uint32_t g, const State& s)
{
    w8(m + 0x37u, s.mat[0]); w8(m + 0x38u, s.mat[1]);
    memcpy(vm_base + R + kRendLo, s.rend, kRendLen);
    memcpy(vm_base + g, s.gctx, 16);
}

struct Stats { uint64_t calls, verified, mismatch, skipped, foreign; uint64_t window; };
Stats s_st;
FILE* s_log;
unsigned s_logged;

void log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char* fmt, ...)
{
    if (!s_log) {
        s_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t33-native.log", "w");
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
    if (!s_st.window) { s_st.window = now; return; }
    if (armTicksToNs(now - s_st.window) < 5000000000ull) return;
    log_line("window calls=%llu verified=%llu mismatch=%llu skipped_no_room=%llu foreign_vtable=%llu\n",
             (unsigned long long)s_st.calls, (unsigned long long)s_st.verified,
             (unsigned long long)s_st.mismatch, (unsigned long long)s_st.skipped,
             (unsigned long long)s_st.foreign);
    s_st = Stats{};
    s_st.window = now;
}

void verify_call(ppu_context* c, uint32_t m, uint32_t R)
{
    const uint32_t g = gcm_ctx();
    const uint32_t cur0 = r32(g + 8u);
    if (r32(g + 4u) - cur0 < kCmdMax) {           // a callback could run: no replay
        s_st.skipped++;
        func_0002C088_lifted(c);
        return;
    }
    Regs regs; save_regs(c, regs);
    State pre; grab(m, R, g, pre);

    func_0002C088_lifted(c);
    State a; grab(m, R, g, a);
    const uint32_t curA = r32(g + 8u);
    static uint8_t cmdA[kCmdMax];
    const uint32_t lenA = curA - cur0;
    if (lenA > kCmdMax) {                         // cannot replay this one; keep it
        s_st.skipped++;
        return;
    }
    memcpy(cmdA, vm_base + cur0, lenA);

    put(m, R, g, pre);
    load_regs(c, regs);
    material_native(c, m, R);
    State b; grab(m, R, g, b);
    const uint32_t curB = r32(g + 8u);

    const bool same = curA == curB && !memcmp(&a, &b, sizeof a) &&
                      !memcmp(cmdA, vm_base + cur0, lenA);
    s_st.verified++;
    if (same) return;

    s_st.mismatch++;
    if (s_logged < 40) {
        s_logged++;
        log_line("MISMATCH material=0x%08X renderer=0x%08X cmd_len lifted=%u native=%u "
                 "mat37/38 %02X%02X vs %02X%02X\n",
                 m, R, lenA, curB - cur0, a.mat[0], a.mat[1], b.mat[0], b.mat[1]);
        for (uint32_t o = 0; o < kRendLen; o++)
            if (a.rend[o] != b.rend[o])
                log_line("  renderer+0x%03X lifted=%02X native=%02X\n", kRendLo + o, a.rend[o], b.rend[o]);
        const uint32_t n = lenA < curB - cur0 ? lenA : curB - cur0;
        unsigned shown = 0;
        for (uint32_t o = 0; o + 4 <= n && shown < 8; o += 4) {
            const uint32_t wa = __builtin_bswap32(*(const uint32_t*)(cmdA + o));
            const uint32_t wb = r32(cur0 + o);
            if (wa != wb) { log_line("  cmd+0x%03X lifted=%08X native=%08X\n", o, wa, wb); shown++; }
        }
    }
    // Keep the lifted result.
    put(m, R, g, a);
    memcpy(vm_base + cur0, cmdA, lenA);
}

}  // namespace

void func_0002C088(ppu_context* ctx)
{
    if (__builtin_expect(s_mode == 0, 0)) {
        const bool code_ok = guest_hash(0x0002C088u) == 0x8A5D788153FCB62Full &&
                             guest_hash(0x0004F14Cu) == 0x4B05FB5743F929B4ull &&
                             guest_hash(0x00052228u) == 0x773461591396EE8Full &&
                             guest_hash(0x001FC568u) == 0xFC28FC697A16DCACull &&
                             guest_hash(0x001FB758u) == 0xF64143A4FBCFAEA8ull;
        if (!code_ok) s_mode = -1;
        else if (sd_flag("native-material-off")) s_mode = -1;
        else s_mode = sd_flag("native-verify") ? 2 : 1;
        fprintf(stderr, "[native-A] material 0x0002C088: %s\n",
                !code_ok ? "lifted (guest code does not match)" :
                s_mode < 0 ? "lifted (native-material-off)" :
                s_mode == 2 ? "native + verify" : "native");
        if (s_mode == 2) log_line("native material apply, verify mode\n");
    }
    const uint32_t m = (uint32_t)ctx->gpr[3];
    const uint32_t R = (uint32_t)ctx->gpr[4];
    if (s_mode < 0) { func_0002C088_lifted(ctx); return; }
    if (__builtin_expect(r32(R) != kRendererVtable, 0)) {
        if (s_mode == 2) { s_st.foreign++; window_tick(); }
        func_0002C088_lifted(ctx);
        return;
    }
    if (s_mode == 2) {
        s_st.calls++;
        verify_call(ctx, m, R);
        window_tick();
        return;
    }
    material_native(ctx, m, R);
}
