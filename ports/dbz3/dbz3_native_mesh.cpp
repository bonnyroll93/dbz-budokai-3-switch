/*
 * DBZ3 native-A step 2c: the mesh draw bodies.
 *
 *   0x0001AA00  prepares one mesh draw and calls 0x000328A8
 *   0x000328A8  emits the draw (state, shaders, textures, vertex arrays,
 *               constants, draw call)
 *
 * Their callees write far more than can be listed by hand (shader reference
 * counts through stwcx., matrices through vector stores, the native
 * replacements' own stores), so their verify harness uses the undo log of
 * dbz3_wtrack.cpp: run one version inside a transaction, read back the final
 * value of every byte it wrote, roll the transaction back, run the other
 * version in a new transaction and compare every byte either one wrote, plus
 * the command buffer, the GCM context and the non-volatile registers.
 *
 * The fast versions (func_*_fast) are generated at build time from the lift
 * by scripts/ppu_mesh_fast.py: registers in locals, NEON vector blocks, the
 * liftfast accessors inlined in this small translation unit.
 *
 * SD switches (sdmc:/switch/dbz-budokai-3-switch/):
 *   native-mesh-off  run the lifted functions
 *   mesh-verify      (PORT_DBZ3_WTRACK build) lifted vs fast on every
 *                    outermost call, t36-mesh.log; the lifted result is kept
 *                    on any difference
 *   mesh-selftest    (PORT_DBZ3_WTRACK build) lifted vs lifted, to check the
 *                    harness itself
 */
#define PPU_FAST_VM 1
#include "dbz3_lifted_prelude.inc"
#include "dbz3_liftfast.h"

#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

void func_0001AA00_lifted(ppu_context* ctx);
void func_000328A8_lifted(ppu_context* ctx);
#include "dbz3_mesh_fast.inc"

#ifdef DBZ3_WTRACK
extern "C" void dbz3_undo_begin(void);
extern "C" void dbz3_undo_end(void);
extern "C" int dbz3_undo_overflow(void);
extern "C" unsigned dbz3_undo_count(void);
extern "C" void dbz3_undo_entry(unsigned i, uint32_t* addr, uint32_t* len, const uint8_t** old);
extern "C" void dbz3_undo_rollback(void);
#endif

namespace {

inline uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, vm_base + a, 4); return __builtin_bswap32(v); }

const uint32_t kGcmCtxPtr = 0x005D5610u;

bool sd_flag(const char* name)
{
    char path[128];
    snprintf(path, sizeof path, "sdmc:/switch/dbz-budokai-3-switch/%s", name);
    FILE* f = fopen(path, "r");
    if (f) fclose(f);
    return f != nullptr;
}

typedef void (*Fn)(ppu_context*);

int s_mode;        // 0 = not decided, 1 = lifted, 2 = self-test, 3 = fast, 4 = verify fast
int s_depth;       // nesting of the two functions on the main thread
int s_fast;        // version nested calls use: 0 lifted, 1 fast
const Fn kLifted[2] = { func_0001AA00_lifted, func_000328A8_lifted };
const Fn kFast[2] = { func_0001AA00_fast, func_000328A8_fast };

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
Stats s_st[2];
const char* const kName[2] = { "0x0001AA00", "0x000328A8" };
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
    for (int k = 0; k < 2; k++)
        log_line("window %s calls=%llu verified=%llu mismatch=%llu skip_room=%llu skip_heap=%llu "
                 "undo_overflow=%llu map_full=%llu\n", kName[k],
                 (unsigned long long)s_st[k].calls, (unsigned long long)s_st[k].verified,
                 (unsigned long long)s_st[k].mismatch, (unsigned long long)s_st[k].skip_room,
                 (unsigned long long)s_st[k].skip_heap, (unsigned long long)s_st[k].overflow,
                 (unsigned long long)s_st[k].map_full);
    memset(s_st, 0, sizeof s_st);
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
/* What a caller may rely on after the call: r1, r2, r13-r31, f14-f31, cr2-cr4. */
bool same_nonvolatile(const Regs& a, const Regs& b)
{
    if (a.gpr[1] != b.gpr[1] || a.gpr[2] != b.gpr[2]) return false;
    for (int i = 13; i < 32; i++) if (a.gpr[i] != b.gpr[i]) return false;
    if (memcmp(&a.fpr[14], &b.fpr[14], 18 * sizeof(double))) return false;
    return (a.cr & 0x00FFF000u) == (b.cr & 0x00FFF000u);
}

const uint32_t kCmdMax = 0x8000u;
uint8_t s_cmdA[kCmdMax];

/* Runs `first`, then `second` from the same state, and compares. Leaves the
 * result of `first` in place when they differ. */
void verify_call(ppu_context* c, int k, Fn first, Fn second)
{
    const uint32_t g = r32(kGcmCtxPtr);
    const uint32_t cur0 = r32(g + 8u);
    if (r32(g + 4u) - cur0 < kCmdMax) { s_st[k].skip_room++; first(c); return; }
    const Range cmd = { r32(g), r32(g + 4u) + 4u };
    const uint32_t r1 = (uint32_t)c->gpr[1];
    const Range stack = { r1 - 0x20000u, r1 + 0x40u };   // callees' frames, dead after the call
    Regs pre; save_regs(c, pre);
    uint8_t gctx0[16], gctxA[16];
    memcpy(gctx0, vm_base + g, 16);

    // Run A.
    s_fast = first == kFast[k];
    dbz3_undo_begin();
    first(c);
    dbz3_undo_end();
    if (dbz3_undo_overflow()) { s_st[k].overflow++; return; }      // keep A, nothing to compare
    Regs postA; save_regs(c, postA);
    memcpy(gctxA, vm_base + g, 16);
    const uint32_t lenA = r32(g + 8u) - cur0;
    if (lenA > kCmdMax) { s_st[k].skip_room++; return; }
    memcpy(s_cmdA, vm_base + cur0, lenA);

    map_clear();
    const unsigned nA = dbz3_undo_count();
    for (unsigned i = 0; i < nA; i++) {
        uint32_t a, n; const uint8_t* old;
        dbz3_undo_entry(i, &a, &n, &old);
        if (in(kHeap, a)) { s_st[k].skip_heap++; return; }           // keep A
        for (uint32_t j = 0; j < n; j++) {
            const uint32_t x = a + j;
            if (in(cmd, x) || in(stack, x)) continue;
            Byte* b = map_find(x, true);
            if (!b) { s_st[k].map_full++; return; }                    // keep A
            if (b->used == 1) { b->pre = old[j]; b->used = 2; }      // oldest value first
        }
    }
    for (unsigned i = 0; i < s_map_n; i++) { Byte& b = s_map[s_used_list[i]]; b.post = vm_base[b.addr]; }

    // Back to the state before A, run B.
    dbz3_undo_rollback();
    memcpy(vm_base + g, gctx0, 16);
    load_regs(c, pre);
    s_fast = second == kFast[k];
    dbz3_undo_begin();
    second(c);
    dbz3_undo_end();
    Regs postB; save_regs(c, postB);

    bool same = !dbz3_undo_overflow() && !memcmp(gctxA, vm_base + g, 16) &&
                r32(g + 8u) - cur0 == lenA && !memcmp(s_cmdA, vm_base + cur0, lenA) &&
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
    s_st[k].verified++;
    if (same) return;
    s_st[k].mismatch++;
    if (s_logged++ < 40)
        log_line("MISMATCH %s r3=0x%08X r4=0x%08X r5=0x%08X cmd lifted=%u second=%u regs %s\n", kName[k],
                 (uint32_t)pre.gpr[3], (uint32_t)pre.gpr[4], (uint32_t)pre.gpr[5], lenA,
                 r32(g + 8u) - cur0, same_nonvolatile(postA, postB) ? "same" : "differ");

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
                                         "fast", "fast + verify" };
    fprintf(stderr, "[native-A] mesh bodies 0x0001AA00/0x000328A8: %s\n", kMode[s_mode]);
#ifdef DBZ3_WTRACK
    if (s_mode == 2) log_line("mesh harness self-test: lifted vs lifted\n");
    if (s_mode == 4) log_line("mesh bodies verify: lifted vs fast\n");
#endif
    s_fast = s_mode == 3;
}

void dispatch(ppu_context* c, int k)
{
    if (__builtin_expect(s_mode == 0, 0)) decide();
#ifdef DBZ3_WTRACK
    if ((s_mode == 2 || s_mode == 4) && s_depth == 0) {
        s_depth++;
        s_st[k].calls++;
        s_fast = 0;
        verify_call(c, k, kLifted[k], s_mode == 4 ? kFast[k] : kLifted[k]);
        s_fast = 0;
        s_depth--;
        window_tick();
        return;
    }
#endif
    s_depth++;
    (s_fast ? kFast[k] : kLifted[k])(c);
    s_depth--;
}

}  // namespace

void func_0001AA00(ppu_context* ctx) { dispatch(ctx, 0); }
void func_000328A8(ppu_context* ctx) { dispatch(ctx, 1); }
