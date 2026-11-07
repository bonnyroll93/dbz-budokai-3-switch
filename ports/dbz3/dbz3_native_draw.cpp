/*
 * DBZ3 native-A steps 2a-ii and 2b: draw-state functions under the mesh path.
 *
 *   0x0002BAE8  fills the 0x98-byte draw parameter block at r3 (2b)
 *   0x00053A7C  one RSX method 0x8E4 (via 0x001FDE88 -> 0x001FF760)
 *   0x0004E288  one RSX method 0x324 from four flag bits (via 0x001FB474)
 *   0x0004EB98  renderer vtable +0x64: the six vertex arrays of a mesh
 *               (twelve cellGcmSetVertexDataArray methods, 0x001FCD78)
 *   0x00050844  renderer vtable +0xB8: bind or unbind one texture unit
 *               (0x001FFAC4 + 0x001FF580, or 0x001FBEE8)
 *
 * Their guest effects are listed in t32-wtrack.log (run76): the 0x98 bytes at
 * r3 for 0x0002BAE8; renderer +0x1D0 / +0x1D4 + 4*unit, the GCM context and
 * the command words for the others. The native versions produce the same
 * bytes. When the command buffer has no room for the whole sequence they run
 * the lifted function, which calls the game's callback as before.
 *
 * Off with sdmc:/switch/dbz-budokai-3-switch/native-draw-off. With
 * native-verify present, every call runs the lifted function first, then the
 * native one from the same state, compares every byte the function may write
 * and keeps the lifted result on any difference (t35-native-draw.log).
 */
#define PPU_FAST_VM 1
#include "dbz3_lifted_prelude.inc"
#include "dbz3_store_hook.h"
#include "dbz3_native_stamp.h"

#include <switch.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

void func_0002BAE8_lifted(ppu_context* ctx);
void func_00053A7C_lifted(ppu_context* ctx);
void func_0004E288_lifted(ppu_context* ctx);
void func_0004EB98_lifted(ppu_context* ctx);
void func_00050844_lifted(ppu_context* ctx);

namespace {

inline uint8_t  r8(uint32_t a)  { return vm_base[a]; }
inline uint16_t r16(uint32_t a) { uint16_t v; memcpy(&v, vm_base + a, 2); return __builtin_bswap16(v); }
inline uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }
inline double   rf(uint32_t a)  { const uint32_t w = r32(a); float f; memcpy(&f, &w, 4); return f; }   // lfs
inline void w8(uint32_t a, uint32_t v)  { dbz3_store_hook(a, 1u); vm_base[a] = (uint8_t)v; }
inline void w32(uint32_t a, uint32_t v) { dbz3_store_hook(a, 4u); v = __builtin_bswap32(v); memcpy(vm_base + a, &v, 4); }
inline uint32_t fb(double d) { const float f = (float)d; uint32_t w; memcpy(&w, &f, 4); return w; }      // stfs
inline uint32_t is0(uint32_t x) { return x ? 0u : 1u; }    // cntlzw + rlwinm(27,31,31)

const uint32_t kGcmCtxPtr = 0x005D5610u;   // CellGcmContextData*: begin, end, current, callback
inline uint32_t gcm_ctx() { return r32(kGcmCtxPtr); }

/* Room for `bytes` more command bytes; `cur` gets the current pointer. */
inline bool room(uint32_t g, uint32_t bytes, uint32_t& cur)
{
    cur = r32(g + 8u);
    return (uint32_t)(cur + bytes) <= r32(g + 4u);
}

/* ---- 0x0002BAE8: draw parameter block ------------------------------------
 * r3 = block (0x98 bytes, cleared first), r4 = flags, r5 = mode (64-bit),
 * r6, r7, r8 = extra flags. Returns false when the lifted function must run
 * (a stack the table lookup below does not expect). */
bool params_native(ppu_context* c)
{
    const uint32_t out = (uint32_t)c->gpr[3];
    uint32_t r30 = (uint32_t)c->gpr[4];
    uint64_t r24 = c->gpr[5];
    const uint64_t r29 = c->gpr[6];
    const uint64_t r28 = c->gpr[7];
    const uint64_t r27 = c->gpr[8];
    if ((uint32_t)c->gpr[1] & 0xCu) return false;   // the 0x78 table lookup assumes a 16-byte stack

    dbz3_store_hook(out, 0x98u);
    memset(vm_base + out, 0, 0x98);

    const double k = rf(0x0002BAE0u);
    uint32_t r26 = 0;
    if (!(std::fabs(rf(0x004789D4u)) >= k) && !(std::fabs(rf(0x0040C76Cu)) >= k)) r26 = 1;
    uint32_t r25 = 0;
    {
        const double d = ppu_fp_single(ppu_fsub(rf(0x0040C760u), rf(0x0040C764u)));
        if (!(std::fabs(d) >= k)) r25 = 1;
    }
    uint32_t r3 = r8(out + 0x39u);

    uint32_t s70, s74;
    if (r32(0x004789D0u) != 0 && r26 == 0 && r25 == 0) {
        const uint32_t a = r32(0x0040C748u);
        s70 = ~a & r32(0x004789CCu);
        s74 = a | 0x80u;
    } else {
        s74 = ppc_rlwimi(r32(0x0040C748u), 0, 0, 24, 24);
        s70 = ppc_rlwimi(~s74 & r32(0x004789CCu), 0, 0, 24, 24);
    }

    r3 |= ppc_rlwinm((uint32_t)r27, 0, 24, 31);
    r3 = is0(r3) ^ 1u;
    r30 = (r30 & s74) | s70;
    w32(out + 0x0u, r30);
    w8(out + 0x39u, r3);
    w8(out + 0x36u, is0(ppc_rlwinm(r30, 10, 28, 31) - 3u));

    if (ppc_rlwinm(r30, 0, 20, 20)) {
        const uint32_t m = r32(0x00478B40u);
        if ((m == 5 || m == 7) && ppc_rlwinm(r30, 0, 2, 2)) {
            uint64_t v; memcpy(&v, vm_base + 0x0040C728u, 8);
            r24 = __builtin_bswap64(v);
        }
        const int64_t s = (int64_t)r24;
        uint32_t a8, aC, a10, v14;
        if (s == 0x48)      { a8 = 0; aC = 0; a10 = 2; v14 = 0; }
        else if (s == 0x44) { a8 = 0; aC = 0; a10 = 1; v14 = 0; }
        else if (s == 0x42) { a8 = 2; aC = 0; a10 = 2; v14 = 2; }
        else if (s == 6)    { a8 = 1; aC = 2; a10 = 0; v14 = 1; }
        else                { a8 = 0; aC = 0; a10 = 1; v14 = 0; }
        w32(out + 0x8u, a8); w32(out + 0xCu, aC); w32(out + 0x10u, a10);
        w8(out + 0x4u, 1);
        w32(out + 0x14u, v14);
        w32(out + 0x18u, 2);
        w32(out + 0x1Cu, 2);
    } else {
        w8(out + 0x4u, 0);
    }

    w32(out + 0x3Cu, 0xFF);
    w32(out + 0x4Cu, 0xFF);
    w32(out + 0x50u, 0);
    w32(out + 0x54u, 0);
    const uint32_t g3 = r32(0x0040C720u);
    if (ppc_rlwinm(g3, 0, 17, 17)) {
        const uint32_t b = ppc_rlwinm(g3, 17, 31, 31);
        w32(out + 0x48u, b);
        w32(out + 0x44u, 4);
        uint32_t v;
        if (b == 0 && r32(0x00478980u) == 1) v = 3;
        else {
            v = 2;
            if (b == 1 && r32(0x00478980u) == 0) v = 4;
        }
        r30 |= 0x1000u;
        w32(out + 0x58u, v);
        w32(out + 0x40u, 1);
        w32(out + 0x0u, r30);
    } else if (ppc_rlwinm((uint32_t)r28, 0, 24, 31)) {
        w32(out + 0x44u, 1);
        w32(out + 0x58u, 2);
        w32(out + 0x48u, r32(0x00478980u));
        w32(out + 0x40u, 1);
    }

    if (ppc_rlwinm(r30, 0, 19, 19)) {
        w32(out + 0x24u, ppc_rlwinm(g3, 28, 24, 31));
        w8(out + 0x20u, 1);
        w32(out + 0x28u, r32(0x002B925Cu + ppc_rlwinm(g3, 1, 27, 29)));
    } else {
        w8(out + 0x20u, 0);
    }

    if (ppc_rlwinm(g3, 0, 15, 15)) {
        // The lift builds {0..7} at r1+0x78 and reads the entry picked by
        // bits 28-29 of rotl(g3, 17) on a 16-byte aligned stack.
        w8(out + 0x2Cu, 1);
        w32(out + 0x30u, ppc_rlwinm(g3, 17, 28, 29) >> 2);
    } else {
        w8(out + 0x2Cu, 0);
    }

    uint32_t b34 = 0;
    if (!ppc_rlwinm(r30, 0, 0, 0)) b34 = is0(r32(0x004789C8u));
    w8(out + 0x34u, b34);
    w8(out + 0x35u, is0(ppc_rlwinm(r30, 10, 28, 31) - 1u));
    if (!ppc_rlwinm(r30, 0, 3, 3) && ppc_rlwinm(r30, 0, 26, 26)) {
        w8(out + 0x5Cu, 1);
        w32(out + 0x60u, ((uint64_t)ppc_rlwinm((uint32_t)r29, 0, 30, 31)) ? 1u : 0u);
        w32(out + 0x64u, ppc_rldicl(r29, 62, 62) ? 1u : 0u);
        w32(out + 0x68u, 1);
        w32(out + 0x6Cu, 1);
        if (ppc_rlwinm(r30, 0, 18, 18)) {
            w8(out + 0x70u, 1);
            w32(out + 0x74u, 1);
            w32(out + 0x78u, 1);
            w32(out + 0x7Cu, 0);
            w32(out + 0x80u, 0);
        }
    }

    const uint32_t m84 = ppc_rlwinm(r30, 0, 6, 9);
    w8(out + 0x84u, is0(m84) ^ 1u);
    if (m84) {
        const uint32_t o = ppc_rlwinm(r30, 12, 26, 29);
        w32(out + 0x88u, fb(rf(0x005D5060u + o)));
        w32(out + 0x8Cu, fb(rf(0x005D50A0u + o)));
    }

    uint32_t r23 = 0;
    if (r8(0x0040C770u) || r8(0x0040C771u) || r8(0x0040C772u) || r8(0x0040C773u))
        r23 = is0(ppc_rlwinm(r30, 0, 28, 28)) ^ 1u;
    w8(out + 0x90u, r23);
    const uint32_t m91 = ppc_rlwinm(r30, 0, 17, 17);
    w8(out + 0x91u, is0(m91) ^ 1u);
    if (m91) {
        const uint64_t n = ppc_rldicl((uint64_t)ppc_rlwinm(r30, 16, 29, 31), 0, 32);
        int64_t iv; memcpy(&iv, &n, 8);
        const double f2 = ppu_frsp((double)iv);
        w32(out + 0x94u, fb(ppu_fp_single(ppu_fmul(f2, rf(0x0002BAE4u)))));
    }
    return true;
}

/* ---- 0x00053A7C: method 0x8E4 = 1 | obj+0xC -------------------------------- */
bool m8e4_native(ppu_context* c)
{
    const uint32_t v = r32((uint32_t)c->gpr[3] + 0xCu);
    const uint32_t g = gcm_ctx();
    uint32_t cur;
    if (!room(g, 8u, cur)) return false;
    w32(cur + 4u, 1u | v);
    w32(g + 8u, cur + 8u);
    w32(cur, 0x000408E4u);
    return true;
}

/* ---- 0x0004E288: method 0x324 from r4 bits ------------------------------- */
bool m324_native(ppu_context* c)
{
    const uint32_t f = (uint32_t)c->gpr[4];
    uint32_t v = 0;
    if (ppc_rlwinm(f, 0, 30, 30)) v = 0x10000u;
    if (ppc_rlwinm(f, 0, 29, 29)) v |= 0x100u;
    if (ppc_rlwinm(f, 0, 28, 28)) v |= 0x1u;
    if (ppc_rlwinm(f, 0, 27, 27)) v |= 0x1000000u;
    const uint32_t g = gcm_ctx();
    uint32_t cur;
    if (!room(g, 8u, cur)) return false;
    w32(g + 8u, cur + 8u);
    w32(cur + 4u, v);
    w32(cur, 0x00040324u);
    return true;
}

/* ---- 0x0004EB98: six vertex arrays -----------------------------------------
 * r3 = renderer, r4 = buffer offset, r5 = stride, r6 = attribute flags. */
inline void vda(uint32_t& cur, uint32_t idx, uint32_t stride, uint32_t size, uint32_t type, uint32_t offset)
{
    // 0x001FCD78 with frequency 0, location 0
    const uint32_t fmt = ppc_rlwinm(stride, 8, 16, 23) | ppc_rlwinm(size, 4, 20, 27) |
                         ppc_rlwinm(type, 0, 24, 31);
    const uint32_t i4 = ppc_rlwinm(idx, 2, 22, 29);
    w32(cur + 4u, fmt);
    w32(cur + 0u, (i4 + 0x1740u) | 0x40000u);
    w32(cur + 12u, offset);
    w32(cur + 8u, (i4 + 0x1680u) | 0x40000u);
    cur += 16u;
}

bool arrays_native(ppu_context* c)
{
    const uint32_t R = (uint32_t)c->gpr[3];
    uint32_t off = (uint32_t)c->gpr[4];
    const uint32_t stride = ppc_rlwinm((uint32_t)c->gpr[5], 0, 24, 31);
    const uint32_t flags = (uint32_t)c->gpr[6];
    if (r32(R + 0x1D0u) == off) return true;
    const uint32_t g = gcm_ctx();
    uint32_t cur;
    if (!room(g, 96u, cur)) return false;
    w32(R + 0x1D0u, off);
    struct A { uint32_t bit, idx, size, type, step; };
    static const A kA[6] = {
        { 0x01u, 0, 3, 2, 0xC }, { 0x02u, 1, 1, 2, 4 }, { 0x04u, 7, 4, 7, 4 },
        { 0x08u, 2, 3, 2, 0xC }, { 0x10u, 3, 4, 4, 4 }, { 0x20u, 8, 2, 2, 0 },
    };
    for (const A& a : kA) {
        if (flags & a.bit) { vda(cur, a.idx, stride, a.size, a.type, off); off += a.step; }
        else vda(cur, a.idx, 0, 0, 2, 0);
    }
    w32(g + 8u, cur);
    return true;
}

/* ---- 0x00050844: texture unit bind ----------------------------------------
 * r3 = renderer, r4 = texture object (0 = unbind), r5 = unit. */
bool texture_native(ppu_context* c)
{
    const uint32_t R = (uint32_t)c->gpr[3];
    const uint32_t tex = (uint32_t)c->gpr[4];
    const uint32_t unit = ppc_rlwinm((uint32_t)c->gpr[5], 0, 24, 31);
    const uint32_t slot = R + ppc_rlwinm((uint32_t)c->gpr[5], 2, 22, 29) + 0x1D4u;
    const uint32_t g = gcm_ctx();
    uint32_t cur;
    if (!tex) {
        if (r32(slot)) {
            if (!room(g, 8u, cur)) return false;
            // 0x001FBEE8 (unit, 0, 0, 0xC00, 0)
            w32(cur + 4u, ppc_rlwinm(0xC00u, 7, 13, 24));
            w32(g + 8u, cur + 8u);
            w32(cur, (ppc_rlwinm(unit, 5, 19, 26) + 0x1A0Cu) | 0x40000u);
        }
        w32(slot, 0);
        return true;
    }
    const uint32_t d = tex + 4u;
    const uint32_t id = r32(d + 0x14u);
    if (r32(slot) == id) return true;
    if (!room(g, 0x24u + 8u, cur)) return false;

    // 0x001FFAC4 (unit, d, 1)
    const uint32_t b0 = r8(d + 0x0u), b1 = r8(d + 0x1u), b2 = r8(d + 0x2u), b3 = r8(d + 0x3u);
    const uint32_t b14 = r8(d + 0xEu);
    uint32_t fmt = (b14 + 1u) | ppc_rlwinm(b3, 2, 0, 29) | ppc_rlwinm(b2, 4, 0, 27) |
                   ppc_rlwinm(b1, 16, 0, 15);
    fmt |= ppc_rlwinm(1u, 3, 21, 28) | ppc_rlwinm(b0, 8, 0, 23);
    const uint32_t u32_ = ppc_rlwinm(unit, 5, 0, 26);
    const uint32_t rect = r16(d + 0xAu) | ppc_rlwinm(r16(d + 0x8u), 16, 0, 15);
    const uint32_t ctl3 = ppc_rlwinm(r16(d + 0xCu), 20, 0, 11) | r32(d + 0x10u);
    w32(cur + 0x8u, fmt);
    w32(cur + 0x0u, (u32_ + 0x1A00u) | 0x80000u);
    w32(cur + 0x4u, r32(d + 0x14u));
    w32(cur + 0x10u, rect);
    w32(cur + 0xCu, (u32_ + 0x1A18u) | 0x40000u);
    w32(cur + 0x18u, ctl3);
    w32(cur + 0x14u, (ppc_rlwinm(unit, 2, 0, 29) + 0x1840u) | 0x40000u);
    w32(cur + 0x20u, r32(d + 0x4u));
    w32(cur + 0x1Cu, (u32_ + 0x1A10u) | 0x40000u);
    cur += 0x24u;

    // 0x001FF580 (unit, 1, 0, 0xC00, 0, anisotropy flag)
    const uint32_t aniso = is0(r8(R + 0x298u)) ^ 1u;
    const uint32_t w = ppc_rlwinm(0xC00u, 7, 13, 24) | ppc_rlwinm(aniso, 2, 22, 29) |
                       ppc_rlwinm(1u, 31, 0, 0);
    w32(cur + 4u, w);
    w32(cur, (ppc_rlwinm(unit, 5, 19, 26) + 0x1A0Cu) | 0x40000u);
    cur += 8u;
    w32(g + 8u, cur);
    w32(slot, r32(d + 0x14u));
    return true;
}

/* ---- dispatch, safety check, verification -------------------------------- */
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
struct Region { uint32_t addr, len; };
typedef unsigned (*RegionFn)(const ppu_context*, Region*);   // memory the function may write

unsigned reg_params(const ppu_context* c, Region* r) { r[0] = { (uint32_t)c->gpr[3], 0x98u }; return 1; }
unsigned reg_none(const ppu_context*, Region*) { return 0; }
unsigned reg_arrays(const ppu_context* c, Region* r) { r[0] = { (uint32_t)c->gpr[3] + 0x1D0u, 4u }; return 1; }
unsigned reg_texture(const ppu_context* c, Region* r)
{
    r[0] = { (uint32_t)c->gpr[3] + ppc_rlwinm((uint32_t)c->gpr[5], 2, 22, 29) + 0x1D4u, 4u };
    return 1;
}

struct Stats { uint64_t calls, verified, mismatch, skipped, lifted; };
struct Entry {
    const char* name; NativeFn native; LiftedFn lifted; RegionFn regions; bool cmd;
    uint32_t hash_ea[4]; uint64_t hash[4];
    Stats st;
};
Entry s_fn[5] = {
    { "params 0x0002BAE8", params_native, func_0002BAE8_lifted, reg_params, false,
      { 0x0002BAE8u }, { 0x39316E8B3AF55FB4ull }, {} },
    { "m8e4 0x00053A7C", m8e4_native, func_00053A7C_lifted, reg_none, true,
      { 0x00053A7Cu, 0x001FDE88u, 0x001FF760u }, { 0xBF11843307495D3Bull, 0xCAE4BC0C8C6A7622ull, 0x10AD7FD756ABBD07ull }, {} },
    { "m324 0x0004E288", m324_native, func_0004E288_lifted, reg_none, true,
      { 0x0004E288u, 0x001FB474u }, { 0xA6955DD852DB57DAull, 0xEF26E2560C7A7070ull }, {} },
    { "arrays 0x0004EB98", arrays_native, func_0004EB98_lifted, reg_arrays, true,
      { 0x0004EB98u, 0x001FCD78u }, { 0x2BF775E78480813Aull, 0xFCCC0959550DAD1Full }, {} },
    { "texture 0x00050844", texture_native, func_00050844_lifted, reg_texture, true,
      { 0x00050844u, 0x001FFAC4u, 0x001FF580u, 0x001FBEE8u },
      { 0x11E2D20FF0F1E79Bull, 0x865932987CE5CD6Eull, 0xFC28FC697A16DCACull, 0xDB1D3DB56B8563B6ull }, {} },
};
int s_ok[5];
int s_mode;   // 0 = not decided, -1 = lifted only, 1 = native, 2 = native + verify

uint64_t s_window;
FILE* s_log;
unsigned s_logged;

void log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char* fmt, ...)
{
    if (!s_log) {
        s_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t35-native-draw.log", "w");
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
        log_line("window %s calls=%llu verified=%llu mismatch=%llu skipped_no_room=%llu lifted_path=%llu\n",
                 e.name, (unsigned long long)e.st.calls, (unsigned long long)e.st.verified,
                 (unsigned long long)e.st.mismatch, (unsigned long long)e.st.skipped,
                 (unsigned long long)e.st.lifted);
        e.st = Stats{};
    }
    s_window = now;
}

void decide()
{
    const bool off = sd_flag("native-draw-off");
    const bool verify = sd_flag("native-verify");
    s_mode = off ? -1 : verify ? 2 : 1;
    for (int i = 0; i < 5; i++) {
        bool ok = true;
        for (int h = 0; h < 4 && s_fn[i].hash_ea[h]; h++)
            ok = ok && guest_hash(s_fn[i].hash_ea[h]) == s_fn[i].hash[h];
        s_ok[i] = ok;
        fprintf(stderr, "[native-A] %s: %s\n", s_fn[i].name,
                !ok ? "lifted (guest code does not match)" : off ? "lifted (native-draw-off)" :
                verify ? "native + verify" : "native");
    }
    if (s_mode == 2) log_line("native draw-state functions, verify mode\n");
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

const uint32_t kCmdMax = 0x400u;   // verified calls need this much room
const unsigned kMaxRegions = 2;
const uint32_t kMaxRegionBytes = 0x100u;

void verify_call(ppu_context* c, Entry& e)
{
    const uint32_t g = gcm_ctx();
    const uint32_t cur0 = r32(g + 8u);
    if (e.cmd && r32(g + 4u) - cur0 < kCmdMax) { e.st.skipped++; e.lifted(c); return; }
    Region reg[kMaxRegions];
    const unsigned nreg = e.regions(c, reg);
    Regs regs; save_regs(c, regs);
    uint8_t pre[kMaxRegions][kMaxRegionBytes], a[kMaxRegions][kMaxRegionBytes];
    uint8_t gctx0[16], gctxA[16];
    for (unsigned i = 0; i < nreg; i++) memcpy(pre[i], vm_base + reg[i].addr, reg[i].len);
    memcpy(gctx0, vm_base + g, 16);

    e.lifted(c);
    for (unsigned i = 0; i < nreg; i++) memcpy(a[i], vm_base + reg[i].addr, reg[i].len);
    memcpy(gctxA, vm_base + g, 16);
    const uint32_t lenA = r32(g + 8u) - cur0;
    static uint8_t cmdA[kCmdMax];
    if (e.cmd) {
        if (lenA > kCmdMax) { e.st.skipped++; return; }
        memcpy(cmdA, vm_base + cur0, lenA);
    }

    auto keep_lifted = [&]() {
        for (unsigned i = 0; i < nreg; i++) memcpy(vm_base + reg[i].addr, a[i], reg[i].len);
        memcpy(vm_base + g, gctxA, 16);
        if (e.cmd) memcpy(vm_base + cur0, cmdA, lenA);
    };

    for (unsigned i = 0; i < nreg; i++) memcpy(vm_base + reg[i].addr, pre[i], reg[i].len);
    memcpy(vm_base + g, gctx0, 16);
    load_regs(c, regs);
    if (!e.native(c)) { e.st.lifted++; keep_lifted(); return; }

    bool same = !memcmp(gctxA, vm_base + g, 16);
    for (unsigned i = 0; i < nreg && same; i++) same = !memcmp(a[i], vm_base + reg[i].addr, reg[i].len);
    const uint32_t lenB = r32(g + 8u) - cur0;
    if (e.cmd && same) same = lenA == lenB && !memcmp(cmdA, vm_base + cur0, lenA);
    e.st.verified++;
    if (same) return;
    e.st.mismatch++;
    if (s_logged < 60) {
        s_logged++;
        log_line("MISMATCH %s r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X cmd_len lifted=%u native=%u\n",
                 e.name, (uint32_t)regs.gpr[3], (uint32_t)regs.gpr[4], (uint32_t)regs.gpr[5],
                 (uint32_t)regs.gpr[6], lenA, lenB);
        for (unsigned i = 0; i < nreg; i++)
            for (uint32_t o = 0; o < reg[i].len; o++)
                if (a[i][o] != vm_base[reg[i].addr + o])
                    log_line("  mem 0x%08X lifted=%02X native=%02X\n", reg[i].addr + o, a[i][o],
                             vm_base[reg[i].addr + o]);
        if (e.cmd) {
            const uint32_t n = lenA < lenB ? lenA : lenB;
            unsigned shown = 0;
            for (uint32_t o = 0; o + 4 <= n && shown < 8; o += 4) {
                const uint32_t wa = __builtin_bswap32(*(const uint32_t*)(cmdA + o));
                const uint32_t wb = r32(cur0 + o);
                if (wa != wb) { log_line("  cmd+0x%03X lifted=%08X native=%08X\n", o, wa, wb); shown++; }
            }
        }
    }
    keep_lifted();
}

void dispatch(ppu_context* c, int i)
{
    if (__builtin_expect(s_mode == 0, 0)) decide();
    Entry& e = s_fn[i];
    DBZ3_NATIVE_STAMP(c, e.hash_ea[0]);
    if (s_mode < 0 || !s_ok[i]) { e.lifted(c); return; }
    if (s_mode == 2) {
        e.st.calls++;
        verify_call(c, e);
        window_tick();
        return;
    }
    if (!e.native(c)) e.lifted(c);
}

}  // namespace

void func_0002BAE8(ppu_context* ctx) { dispatch(ctx, 0); }
void func_00053A7C(ppu_context* ctx) { dispatch(ctx, 1); }
void func_0004E288(ppu_context* ctx) { dispatch(ctx, 2); }
void func_0004EB98(ppu_context* ctx) { dispatch(ctx, 3); }
void func_00050844(ppu_context* ctx) { dispatch(ctx, 4); }
