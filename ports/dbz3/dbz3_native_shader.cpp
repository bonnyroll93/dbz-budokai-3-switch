/*
 * DBZ3 native-A step 2a-i: shader program binds.
 *
 *   0x00053B30  vertex program bind: r3 = program object, uploads
 *               {obj+4 = program header, obj+8 = microcode} through libgcm's
 *               cellGcmSetVertexProgram (0x001FC674 -> 0x001FC6B0)
 *   0x00053880  fragment program bind: {obj+4 = program, obj+0xC = offset}
 *               through 0x001FD594 -> 0x001FF808 + 0x001FF9B8
 *
 * Both only append RSX methods to the command buffer (t32-wtrack.log, run74).
 * The native versions build the same words. When the buffer has no room for
 * the whole sequence, or the vertex program has parameters with default
 * values (handled by 0x001FD5F0), they run the lifted function instead, which
 * calls the game's callback or that routine exactly as before.
 *
 * Off with sdmc:/switch/dbz-budokai-3-switch/native-shader-off. With
 * native-verify present, every call runs the lifted function first, then the
 * native one from the same state, compares the commands byte for byte and
 * keeps the lifted result on any difference (t34-native-shader.log).
 */
#define PPU_FAST_VM 1
#include "dbz3_lifted_prelude.inc"
#include "dbz3_store_hook.h"

#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

void func_00053B30_lifted(ppu_context* ctx);
void func_00053880_lifted(ppu_context* ctx);

namespace {

inline uint8_t  r8(uint32_t a)  { return vm_base[a]; }
inline uint16_t r16(uint32_t a) { uint16_t v; memcpy(&v, vm_base + a, 2); return __builtin_bswap16(v); }
inline uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }
inline void w32(uint32_t a, uint32_t v) { dbz3_store_hook(a, 4u); v = __builtin_bswap32(v); memcpy(vm_base + a, &v, 4); }

const uint32_t kGcmCtxPtr = 0x005D5610u;   // CellGcmContextData*: begin, end, current, callback

/* ---- cellGcmSetVertexProgram (0x001FC6B0) --------------------------------
 * prog = program header (r4), ucode = microcode (r5). Returns false (nothing
 * written) when the lifted function has to run instead. */
bool set_vertex_program(uint32_t g, uint32_t prog, uint32_t ucode)
{
    const uint32_t hdr = prog + r32(prog + 0x14u);
    const uint32_t w0 = r32(hdr + 0x0u);
    const uint32_t start = r32(hdr + 0x4u);
    const uint32_t rem = ppc_rlwinm(w0, 2, 27, 29);       // words after the full blocks
    const uint32_t blocks = ppc_rlwinm(w0, 29, 3, 31);    // 32-word blocks

    // Parameters with default values go through 0x001FD5F0
    // (cellGcmSetVertexProgramParameter). Its jump table on the Cg type
    // (DBZ3.elf 0x001FD694) sends float/float2/float3/float4/bool to one
    // 6-word constant load, which is done here; the matrix types read past
    // the 16-byte default the caller copies to its stack, so those programs
    // stay with the lift. Other types write nothing.
    const uint32_t nparams = r32(prog + 0xCu);
    const uint32_t pbase = prog + r32(prog + 0x10u);
    uint32_t nconst = 0;
    for (uint32_t i = 0; i < nparams; i++) {
        const uint32_t p = pbase + i * 0x30u;
        if (!r32(p + 0x14u) || r32(p + 0x8u) - 4102u > 1u) continue;
        if (r32(p + 0x4u) == 0xCB8u) continue;
        const uint32_t type = r32(p + 0x0u);
        if (type == 1059u || type == 1060u || type == 1063u || type == 1064u) return false;
        const bool vec = (type >= 1045u && type <= 1048u) || type == 1091u;
        if (vec && r32(p + 0xCu) != 0xFFFFFFFFu) nconst++;
    }

    uint32_t cur = r32(g + 8u);
    const uint32_t words = ppc_rlwinm(blocks, 5, 0, 26) + blocks + (rem ? rem + 1u : 0u);
    if ((uint32_t)(cur + ppc_rlwinm(words, 2, 0, 29) + 0x1Cu + nconst * 0x18u) > r32(g + 4u))
        return false;

    w32(cur + 0u, 0x00081E9Cu);                           // transform program load + start
    w32(cur + 4u, start);
    w32(cur + 8u, start);
    cur += 0xCu;
    uint32_t src = ucode;
    for (uint32_t b = 0; b < blocks; b++, src += 0x80u, cur += 0x84u) {
        w32(cur, 0x00800B80u);                            // 32 words of microcode
        dbz3_store_hook(cur + 4u, 0x80u);
        memcpy(vm_base + cur + 4u, vm_base + src, 0x80u); // big-endian words, copied as is
    }
    if (rem) {
        w32(cur, ppc_rlwinm(rem, 18, 0, 13) | 0xB80u);
        dbz3_store_hook(cur + 4u, rem * 4u);
        memcpy(vm_base + cur + 4u, vm_base + src, (size_t)rem * 4u);
        cur += 4u + rem * 4u;
    }
    w32(cur + 0u, 0x00041FF0u);
    w32(cur + 4u, r32(hdr + 0xCu));
    cur += 8u;
    w32(cur + 0u, 0x00041EF8u);
    w32(cur + 4u, r32(hdr + 0x8u) > 0x20u ? 0x0030FFFFu : 0x0020FFFFu);
    cur += 8u;

    // Default values, in parameter order (0x001FD5F0 cases D7F0/D750/DDE4/DD44).
    for (uint32_t i = 0; i < nparams; i++) {
        const uint32_t p = pbase + i * 0x30u;
        const uint32_t t = r32(p + 0x14u);
        if (!t || r32(p + 0x8u) - 4102u > 1u) continue;
        if (r32(p + 0x4u) == 0xCB8u) continue;
        const uint32_t type = r32(p + 0x0u);
        uint32_t n;                                       // value words copied
        if (type == 1045u || type == 1091u) n = 1;
        else if (type == 1046u) n = 2;
        else if (type == 1047u) n = 3;
        else if (type == 1048u) n = 4;
        else continue;
        const uint32_t index = r32(p + 0xCu);
        if (index == 0xFFFFFFFFu) continue;
        const uint32_t src = prog + t;
        w32(cur + 0u, 0x00141EFCu);                        // transform constant load, 5 words
        w32(cur + 4u, index);
        for (uint32_t k = 0; k < 4; k++) w32(cur + 8u + 4u * k, k < n ? r32(src + 4u * k) : 0u);
        cur += 0x18u;
    }
    w32(g + 8u, cur);
    return true;
}

/* ---- 0x001FD594: cellGcmSetFragmentProgram (0x001FF808 with r6 = 0, then
 * 0x001FF9B8 with r5 = 0, r6 = 1, r7 = 0). */
bool set_fragment_program(uint32_t g, uint32_t fp, uint32_t offset)
{
    const uint32_t hdr = fp + r32(fp + 0x14u);
    const uint32_t mask = r16(hdr + 0xCu);
    // Worst case: 4 words, one method per set mask bit, then 2 words.
    const uint32_t bits = (uint32_t)__builtin_popcount(mask);
    uint32_t cur = r32(g + 8u);
    if ((uint32_t)(cur + 0x10u + bits * 8u + 8u) > r32(g + 4u)) return false;

    // 0x001FF808
    w32(cur + 4u, 1u | ppc_rlwinm(offset, 0, 3, 31));
    w32(cur + 0u, 0x000408E4u);
    w32(cur + 12u, r32(hdr + 0x4u) | 0x20u);
    w32(cur + 8u, 0x00041FF4u);
    cur += 0x10u;
    if (mask) {
        uint32_t m = mask, a = ppc_rlwinm(r16(hdr + 0xEu), 0, 16, 31);
        uint32_t b = ppc_rlwinm(r16(hdr + 0x10u), 0, 16, 31), method = 0xB40u;
        for (;;) {
            const uint32_t bit = m & 1u;
            m = ppc_rlwinm(m, 31, 1, 31);
            if (bit) {
                w32(cur + 4u, ppc_rlwinm(a, 0, 31, 31) | ppc_rlwinm(b, 4, 27, 27));
                w32(cur + 0u, method | 0x40000u);
                cur += 8u;
            }
            a = ppc_rlwinm(a, 31, 1, 31);
            b = ppc_rlwinm(b, 31, 1, 31);
            method += 4u;
            if (!m) break;
        }
    }

    // 0x001FF9B8 (r28 = 0)
    const uint32_t p = fp + r32(fp + 0x14u);
    uint32_t r7 = 0x02000000u;
    const uint32_t b12 = r8(p + 0x12u);
    if (b12 > 1u) r7 = ppc_rlwinm(b12, 24, 0, 7);
    uint32_t xer = 0;
    uint64_t r9 = (uint64_t)r8(p + 0x14u) - 1u;
    uint64_t r0 = (uint64_t)0 - (uint64_t)r8(p + 0x15u);
    uint64_t r11 = (uint64_t)r8(p + 0x13u) - 1u;
    r9 = ppc_sraw(&xer, (int32_t)r9, 0x1F);
    r9 = ~r9;
    r11 = ppc_rlwinm((uint32_t)r11, 7, 25, 25);
    r9 = ppc_rlwinm((uint32_t)r9, 0, 28, 30);
    r0 = ppc_rlwinm((uint32_t)r0, 8, 24, 24);
    r9 |= r7;
    r11 |= 0x400u;
    r0 |= r9;
    r11 |= r0;
    w32(cur + 0u, 0x00041D60u);
    w32(cur + 4u, (uint32_t)r11);
    cur += 8u;
    w32(g + 8u, cur);
    return true;
}

/* ---- switches, verification ---------------------------------------------- */
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

const uint32_t kCmdMax = 0x2000u;   // verified calls need this much room

struct Stats { uint64_t calls, verified, mismatch, skipped, lifted; uint64_t window; };
Stats s_st[2];
const char* const kName[2] = { "vertex 0x00053B30", "fragment 0x00053880" };
uint64_t s_window;
FILE* s_log;
unsigned s_logged;

void log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char* fmt, ...)
{
    if (!s_log) {
        s_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t34-native-shader.log", "w");
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
    for (int k = 0; k < 2; k++)
        log_line("window %s calls=%llu verified=%llu mismatch=%llu skipped_no_room=%llu lifted_path=%llu\n",
                 kName[k], (unsigned long long)s_st[k].calls, (unsigned long long)s_st[k].verified,
                 (unsigned long long)s_st[k].mismatch, (unsigned long long)s_st[k].skipped,
                 (unsigned long long)s_st[k].lifted);
    memset(s_st, 0, sizeof s_st);
    s_window = now;
}

typedef bool (*NativeFn)(ppu_context*);
typedef void (*LiftedFn)(ppu_context*);

void verify_call(ppu_context* c, int k, NativeFn native, LiftedFn lifted)
{
    const uint32_t g = r32(kGcmCtxPtr);
    const uint32_t cur0 = r32(g + 8u);
    if (r32(g + 4u) - cur0 < kCmdMax) { s_st[k].skipped++; lifted(c); return; }
    Regs regs; save_regs(c, regs);
    uint8_t gctx0[16]; memcpy(gctx0, vm_base + g, 16);

    lifted(c);
    uint8_t gctxA[16]; memcpy(gctxA, vm_base + g, 16);
    const uint32_t lenA = r32(g + 8u) - cur0;
    if (lenA > kCmdMax) { s_st[k].skipped++; return; }
    static uint8_t cmdA[kCmdMax];
    memcpy(cmdA, vm_base + cur0, lenA);

    memcpy(vm_base + g, gctx0, 16);
    load_regs(c, regs);
    if (!native(c)) {                      // the native code declined: lifted path
        s_st[k].lifted++;
        memcpy(vm_base + g, gctxA, 16);
        memcpy(vm_base + cur0, cmdA, lenA);
        return;
    }
    const uint32_t lenB = r32(g + 8u) - cur0;
    const bool same = lenA == lenB && !memcmp(gctxA, vm_base + g, 16) &&
                      !memcmp(cmdA, vm_base + cur0, lenA);
    s_st[k].verified++;
    if (same) return;
    s_st[k].mismatch++;
    if (s_logged < 40) {
        s_logged++;
        log_line("MISMATCH %s obj=0x%08X len lifted=%u native=%u\n", kName[k],
                 (uint32_t)regs.gpr[3], lenA, lenB);
        const uint32_t n = lenA < lenB ? lenA : lenB;
        unsigned shown = 0;
        for (uint32_t o = 0; o + 4 <= n && shown < 8; o += 4) {
            const uint32_t wa = __builtin_bswap32(*(const uint32_t*)(cmdA + o));
            const uint32_t wb = r32(cur0 + o);
            if (wa != wb) { log_line("  cmd+0x%03X lifted=%08X native=%08X\n", o, wa, wb); shown++; }
        }
    }
    memcpy(vm_base + g, gctxA, 16);
    memcpy(vm_base + cur0, cmdA, lenA);
}

bool vp_native(ppu_context* c)
{
    const uint32_t obj = (uint32_t)c->gpr[3];
    return set_vertex_program(r32(kGcmCtxPtr), r32(obj + 4u), r32(obj + 8u));
}
bool fp_native(ppu_context* c)
{
    const uint32_t obj = (uint32_t)c->gpr[3];
    return set_fragment_program(r32(kGcmCtxPtr), r32(obj + 4u), r32(obj + 0xCu));
}

void decide()
{
    const bool ok = guest_hash(0x00053B30u) == 0x6D4A6C044F3108D4ull &&
                    guest_hash(0x00053880u) == 0x7711F013A3E0EBF0ull &&
                    guest_hash(0x001FC6B0u) == 0x017961A34184120Aull &&
                    guest_hash(0x001FF808u) == 0xB4541E97819140C4ull &&
                    guest_hash(0x001FF9B8u) == 0x691CF1509BC83D97ull &&
                    guest_hash(0x001FD5F0u) == 0x4A3B8304D41A11DBull;
    if (!ok) s_mode = -1;
    else if (sd_flag("native-shader-off")) s_mode = -1;
    else s_mode = sd_flag("native-verify") ? 2 : 1;
    fprintf(stderr, "[native-A] shader binds 0x00053B30/0x00053880: %s\n",
            !ok ? "lifted (guest code does not match)" :
            s_mode < 0 ? "lifted (native-shader-off)" :
            s_mode == 2 ? "native + verify" : "native");
    if (s_mode == 2) log_line("native shader binds, verify mode\n");
}

void dispatch(ppu_context* c, int k, NativeFn native, LiftedFn lifted)
{
    if (__builtin_expect(s_mode == 0, 0)) decide();
    if (s_mode < 0) { lifted(c); return; }
    if (s_mode == 2) {
        s_st[k].calls++;
        verify_call(c, k, native, lifted);
        window_tick();
        return;
    }
    if (!native(c)) lifted(c);
}

}  // namespace

void func_00053B30(ppu_context* ctx) { dispatch(ctx, 0, vp_native, func_00053B30_lifted); }
void func_00053880(ppu_context* ctx) { dispatch(ctx, 1, fp_native, func_00053880_lifted); }
