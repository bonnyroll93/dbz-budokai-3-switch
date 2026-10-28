/*
 * DBZ3 (LAUNCH/DBZ3.elf) -- native fast path for the title's inline
 * cellGcmSetVertexProgramConstants (guest 0x001FCAC4), and a hardware-style
 * wrap of the title's command ring.
 *
 * Constants: in fights 0x001FCAC4 was ~20% of the main PPU thread's samples.
 * The lifted body copies one guest word at a time through fast-VM checks and two
 * byte swaps; the data is big-endian on both sides, so memcpy is equivalent.
 *   reserve  blocks*34 + (rem ? rem + 2 : 0) words, blocks = n / 32, rem = n % 32;
 *            if current + that > end, call context->callback(ctx, words) and
 *            return its result when it is non-zero;
 *   per block: [0x00841EFC] [index] [32 words]      index += 8, current += 0x88
 *   remainder: [((rem+1) << 18) | 0x1EFC] [index] [rem words]
 *
 * Ring wrap: the title draws into its own context (0x0F800000, 1 MB ring at
 * 0x40D00000) whose buffer-full callback does not recycle. The title then wrote
 * past `end`, and the runtime recycled from the FIFO thread at the end of a
 * pass: the PPU waited ~20 ms (up to 75) each time, the commands past `end`
 * were thrown away, and once that lost the frame's "post-filter done" write the
 * game froze for good (gcm-wrap run01: Start on the title screen).
 * Here the title's context is pointed at the runtime's callback OPD, and the
 * function behind it is re-registered as ring_full_callback below, so every
 * wrap -- from any of the title's inline GCM writers -- happens on the title's
 * own thread, the way libgcm does it on hardware:
 *   1. JUMP back to `begin` at the write head, `put` up to that JUMP, so the
 *      walker reads the whole lap and then follows the jump;
 *   2. wait until the walker is past the head the title is about to overwrite;
 *   3. current = begin, return CELL_OK.
 * The runtime's walker-side recycle of the title ring is switched off while
 * this is in charge (cellGcm_set_title_ring_managed). 0x001FCAC4 wraps
 * directly, and also checks that its writes never reach the unread tail of the
 * previous lap.
 *
 * Hooked with the linker's --wrap (CMakeLists.txt). The only direct caller
 * (0x0004FB8C, ppu_recomp_000.cpp) and the indirect-call table entry are in
 * other translation units, so every call lands here; __real__Z13... is the
 * lifted original, used for anything outside the common case and when the
 * guest image is not the one this was written against.
 *
 * T24 log: sdmc:/switch/dbz-budokai-3-switch/t24-gcm-const.log, one line per ~5 s.
 */

#define PPU_FAST_VM 1
#include "ppu_recomp.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
static inline uint64_t fp_ticks(void) { return armGetSystemTick(); }
static inline uint64_t fp_ticks_to_us(uint64_t t) { return t / 19u; } /* 19.2 MHz */
static inline void fp_sleep_100us(void) { svcSleepThread(100000); }
#else
static inline uint64_t fp_ticks(void) { return 0; }
static inline uint64_t fp_ticks_to_us(uint64_t t) { return t; }
static inline void fp_sleep_100us(void) {}
#endif

/* The lifted functions have C++ linkage, so --wrap names the mangled symbol. */
extern "C" void real_func_001FCAC4(ppu_context* ctx)
    __asm__("__real__Z13func_001FCAC4P11ppu_context");
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));

/* libs/video/cellGcmSys.c */
extern "C" uint32_t cellGcm_fifo_get_offset(void);
extern "C" uint32_t cellGcm_ea_to_io(uint32_t ea);
extern "C" void cellGcm_set_put_io(uint32_t io);
extern "C" uint32_t cellGcm_hle_callback_opd(void);
extern "C" void cellGcm_set_title_ring_managed(int on);
extern "C" void cellGcm_fifo_recycle(unsigned int ctx_ea);

/* Same as the lifted TUs' DRAIN_TRAMPOLINE (defined there, not in the header). */
#define DRAIN_TRAMPOLINE(ctx) do { \
    while (g_trampoline_fn) { \
        void (*_tf)(void*) = g_trampoline_fn; \
        g_trampoline_fn = 0; \
        _tf((void*)(ctx)); \
    } \
} while (0)

namespace {

constexpr uint32_t kGuestFunc = 0x001FCAC4u;
/* Fingerprint of the first 32 guest code bytes; no game instructions are stored
 * in the public source tree. */
constexpr uint64_t kGuestCodeHash = 0x98E77BAEEBF1AC31ull;
/* Head room the walker must be past before the title writes from `begin`
 * again: more than one wrap's reservation, about a frame of fight commands. */
constexpr uint32_t kWrapHeadRoom = 0x40000u;
constexpr uint32_t kWrapTimeoutUs = 500000u;   /* then wrap anyway (counted) */
constexpr uint32_t kLapTimeoutUs  = 100000u;   /* then the lifted original */

std::atomic<int> g_state{0};   /* 0 unchecked, 1 enabled, -1 disabled */
std::atomic<uint32_t> g_callback_opd{0};       /* runtime OPD the title now uses */

struct Stats {
    std::atomic<uint64_t> fast_calls{0}, slow_calls{0}, words{0};
    std::atomic<uint64_t> fast_ticks{0}, slow_ticks{0}, slow_max_ticks{0};
    std::atomic<uint64_t> wraps{0}, cb_wraps{0}, wrap_wait_ticks{0}, wrap_max_ticks{0};
    std::atomic<uint64_t> wrap_timeouts{0}, cb_fallbacks{0};
    std::atomic<uint64_t> lap_waits{0}, lap_wait_ticks{0}, lap_timeouts{0};
    std::atomic<uint64_t> window_start{0};
    std::atomic<int> writing{0};
    FILE* log = nullptr;
} g_stats;

int check_guest_image(void)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t word = vm_read32(kGuestFunc + 4u * i);
        for (int shift = 24; shift >= 0; shift -= 8) {
            hash ^= (word >> shift) & 0xFFu;
            hash *= 0x100000001B3ull;
        }
    }
    return hash == kGuestCodeHash;
}

inline void store_be32(uint32_t ea, uint32_t v)
{
    v = __builtin_bswap32(v);
    memcpy(vm_base + ea, &v, 4);
}

/* Whole range [ea, ea+bytes) is plain fast-VM memory. */
inline int plain_range(uint32_t ea, uint32_t bytes)
{
    if (bytes == 0) return 1;
    if ((uint64_t)ea + bytes > 0xE0000000ull - 8u) return 0;
    return ppu_fvm_plain(ea, 1) && ppu_fvm_plain(ea + bytes - 1u, 1) &&
           /* must not straddle the 0x20000000-0x2FFFFFFF control window */
           !(ea < 0x30000000u && ea + bytes > 0x20000000u);
}

/* No SPU reservation on any 128-byte line of [ea, ea+bytes). */
inline int unreserved_range(uint32_t ea, uint32_t bytes)
{
    if (!*g_spu_coh_armed || bytes == 0) return 1;
    for (uint32_t l = ea >> 7, last = (ea + bytes - 1u) >> 7; l <= last; ++l)
        if ((g_spu_coh_bitmap[l >> 3] >> (l & 7)) & 1u) return 0;
    return 1;
}

inline void track_max(std::atomic<uint64_t>& slot, uint64_t v)
{
    uint64_t prev = slot.load(std::memory_order_relaxed);
    while (v > prev && !slot.compare_exchange_weak(prev, v)) {}
}

inline unsigned long long xus(std::atomic<uint64_t>& a)
{
    return (unsigned long long)fp_ticks_to_us(a.exchange(0));
}
inline unsigned long long xn(std::atomic<uint64_t>& a)
{
    return (unsigned long long)a.exchange(0);
}

void report(uint64_t now)
{
#ifdef PORT_DBZ3_LIGHT_BUILD
    (void)now;
    return;
#else
    Stats& s = g_stats;
    uint64_t start = s.window_start.load(std::memory_order_relaxed);
    if (start == 0) { s.window_start.store(now, std::memory_order_relaxed); return; }
    if (fp_ticks_to_us(now - start) < 5000000u) return;
    if (s.writing.exchange(1, std::memory_order_acquire)) return;
    if (!s.log) s.log = fopen("sdmc:/switch/dbz-budokai-3-switch/t24-gcm-const.log", "w");
    if (s.log) {
        fprintf(s.log,
                "window_ms=%llu fast_calls=%llu slow_calls=%llu words=%llu "
                "fast_us=%llu slow_us=%llu slow_max_us=%llu "
                "wraps=%llu cb_wraps=%llu wrap_wait_us=%llu wrap_max_us=%llu "
                "wrap_timeouts=%llu cb_fallbacks=%llu "
                "lap_waits=%llu lap_wait_us=%llu lap_timeouts=%llu\n",
                (unsigned long long)(fp_ticks_to_us(now - start) / 1000u),
                xn(s.fast_calls), xn(s.slow_calls), xn(s.words),
                xus(s.fast_ticks), xus(s.slow_ticks), xus(s.slow_max_ticks),
                xn(s.wraps), xn(s.cb_wraps), xus(s.wrap_wait_ticks), xus(s.wrap_max_ticks),
                xn(s.wrap_timeouts), xn(s.cb_fallbacks),
                xn(s.lap_waits), xus(s.lap_wait_ticks), xn(s.lap_timeouts));
        fflush(s.log);
    }
    s.window_start.store(now, std::memory_order_relaxed);
    s.writing.store(0, std::memory_order_release);
#endif
}

static inline uint64_t profile_ticks(void)
{
#ifdef PORT_DBZ3_LIGHT_BUILD
    return 0;
#else
    return fp_ticks();
#endif
}
#ifdef PORT_DBZ3_LIGHT_BUILD
#define PROFILE_STAT(expr) do { } while (0)
#else
#define PROFILE_STAT(expr) do { expr; } while (0)
#endif

void call_original(ppu_context* ctx)
{
    const uint64_t t0 = profile_ticks();
    real_func_001FCAC4(ctx);
    /* The lifted body is split in two: the first half returns through the
     * trampoline into the second, which does the work (and any callback). Run
     * it here so the time below covers it; the caller's own drain then finds
     * nothing left to run. */
    DRAIN_TRAMPOLINE(ctx);
    const uint64_t dt = profile_ticks() - t0;
    Stats& s = g_stats;
    PROFILE_STAT(s.slow_calls.fetch_add(1, std::memory_order_relaxed));
    PROFILE_STAT(s.slow_ticks.fetch_add(dt, std::memory_order_relaxed));
    PROFILE_STAT(track_max(s.slow_max_ticks, dt));
    report(t0 + dt);
}

/* The title's ring, in EA and in the IO offsets the walker reports. */
struct Ring {
    uint32_t begin, end;       /* EA */
    uint32_t io_begin, io_end; /* IO offsets, contiguous with the EAs */
    bool in(uint32_t io) const { return io >= io_begin && io < io_end; }
    uint32_t io(uint32_t ea) const { return io_begin + (ea - begin); }
};

bool read_ring(uint32_t c, Ring& r)
{
    if (!plain_range(c, 0x10)) return false;
    r.begin = vm_read32(c + 0x0);
    r.end   = vm_read32(c + 0x4);
    if (r.begin == 0 || r.end <= r.begin || r.end - r.begin < 0x1000u) return false;
    r.io_begin = cellGcm_ea_to_io(r.begin);
    /* `end` itself must be mapped too: the JUMP can land exactly there. */
    const uint32_t io_last = cellGcm_ea_to_io(r.end);
    if (r.io_begin == 0xFFFFFFFFu || io_last == 0xFFFFFFFFu ||
        io_last - r.io_begin != r.end - r.begin) return false;
    r.io_end = r.io_begin + (r.end - r.begin);
    return true;
}

/* Wrap context `c` back to its ring's start on the calling (title) thread,
 * leaving room for `need` bytes there. False when the context is not a ring
 * this can wrap; nothing has been changed then. */
bool ring_wrap(uint32_t c, uint32_t need)
{
    Ring r;
    if (!read_ring(c, r)) return false;
    const uint32_t cur = vm_read32(c + 0x8);
    if (cur < r.begin || cur > r.end || (cur & 3u)) return false;
    if ((uint64_t)need + 4u > r.end - r.begin) return false;
    if (!plain_range(cur, 4) || !unreserved_range(cur, 4) ||
        !unreserved_range(c + 8u, 4u)) return false;

    const uint32_t io_cur = r.io(cur);
    uint32_t head = need + 4u > kWrapHeadRoom ? need + 4u : kWrapHeadRoom;
    if (head > io_cur - r.io_begin) head = io_cur - r.io_begin;

    /* 1. JUMP begin at the write head; publish everything before it. The jump
     *    itself is published by the title's next flush, which lands in the new
     *    lap -- the walker then reads it and follows. */
    store_be32(cur, 0x20000000u | r.io_begin);
    cellGcm_set_put_io(io_cur);

    /* 2. The walker must be in this lap's tail [begin+head, cur]. Before it --
     *    in the head, or still in a CALLed buffer -- it has unread bytes where
     *    the title is about to write. It is moving: `put` now covers the lap. */
    auto clear = [&](uint32_t g) { return g >= r.io_begin + head && g <= io_cur; };
    uint64_t waited = 0;
    Stats& s = g_stats;
    if (!clear(cellGcm_fifo_get_offset())) {
        const uint64_t t0 = fp_ticks();
        for (;;) {
            fp_sleep_100us();
            waited = fp_ticks() - t0;
            if (clear(cellGcm_fifo_get_offset())) break;
            if (fp_ticks_to_us(waited) >= kWrapTimeoutUs) {
                PROFILE_STAT(s.wrap_timeouts.fetch_add(1, std::memory_order_relaxed));
                fprintf(stderr, "[t24] ring wrap: walker did not clear the head in %u ms "
                        "(get=0x%08X ring=0x%08X-0x%08X cur=0x%08X) -- wrapping anyway\n",
                        kWrapTimeoutUs / 1000u, cellGcm_fifo_get_offset(),
                        r.io_begin, r.io_end, io_cur);
                break;
            }
        }
    }

    /* 3. Resume at the start of the ring. */
    store_be32(c + 8, r.begin);
    PROFILE_STAT(s.wraps.fetch_add(1, std::memory_order_relaxed));
    PROFILE_STAT(s.wrap_wait_ticks.fetch_add(waited, std::memory_order_relaxed));
    PROFILE_STAT(track_max(s.wrap_max_ticks, waited));
    return true;
}

/* context->callback(context, words) for every inline GCM writer of the title,
 * registered over the runtime's own handler at its OPD's function EA. */
void ring_full_callback(ppu_context* ctx)
{
    const uint32_t c = (uint32_t)ctx->gpr[3];
    const uint32_t words = (uint32_t)ctx->gpr[4];
    if (words < 0x10000000u && ring_wrap(c, words * 4u)) {
        PROFILE_STAT(g_stats.cb_wraps.fetch_add(1, std::memory_order_relaxed));
    } else {
        PROFILE_STAT(g_stats.cb_fallbacks.fetch_add(1, std::memory_order_relaxed));
        cellGcm_fifo_recycle(c);           /* the runtime's own behaviour */
    }
    ctx->gpr[3] = 0;                       /* CELL_OK: the caller re-reads current */
}

/* Point the title's context at the runtime's callback OPD, whose function is
 * ours. Checked on every call: the title may set its context up again. */
inline void ensure_title_callback(uint32_t c)
{
    const uint32_t opd = g_callback_opd.load(std::memory_order_relaxed);
    if (!opd || vm_read32(c + 0xC) == opd) return;
    const uint32_t old_opd = vm_read32(c + 0xC);
    const uint32_t old_fn = ppu_fvm_plain(old_opd, 8) ? vm_read32(old_opd) : 0;
    Ring r;
    if (!read_ring(c, r)) return;           /* not a ring we can manage */
    store_be32(c + 0xC, opd);
    cellGcm_set_title_ring_managed(1);
    fprintf(stderr, "[t24] title gcm ctx=0x%08X ring=0x%08X-0x%08X: callback opd 0x%08X "
            "(fn 0x%08X) -> runtime opd 0x%08X (ring wrap on the title thread)\n",
            c, r.begin, r.end, old_opd, old_fn, opd);
}

void enable_once(void)
{
    int ok = g_ppu_fast_vm && check_guest_image();
    if (ok) {
        const uint32_t opd = cellGcm_hle_callback_opd();
        const uint32_t fn = (opd && ppu_fvm_plain(opd, 8)) ? vm_read32(opd) : 0;
        if (fn) {
            ppu_register_function(fn, ring_full_callback);
            g_callback_opd.store(opd, std::memory_order_relaxed);
        }
        fprintf(stderr, "[t24] runtime callback opd=0x%08X fn=0x%08X -> %s\n", opd, fn,
                fn ? "ring_full_callback" : "UNAVAILABLE (title callback kept)");
    }
    g_state.store(ok ? 1 : -1, std::memory_order_release);
    fprintf(stderr, "[t24] gcm-const fast path for 0x%08X %s\n", kGuestFunc,
            ok ? "enabled" : "DISABLED (guest code mismatch or fast VM off)");
}

} // namespace

extern "C" void wrap_func_001FCAC4(ppu_context* ctx)
    __asm__("__wrap__Z13func_001FCAC4P11ppu_context");
extern "C" void wrap_func_001FCAC4(ppu_context* ctx)
{
    int state = g_state.load(std::memory_order_acquire);
    if (__builtin_expect(state == 0, 0)) {
        enable_once();
        state = g_state.load(std::memory_order_acquire);
    }
    if (state < 0 || !g_ppu_fast_vm) { call_original(ctx); return; }

    const uint64_t t0 = profile_ticks();
    Stats& s = g_stats;
    const uint32_t c      = (uint32_t)ctx->gpr[3];
    const uint32_t n      = (uint32_t)ctx->gpr[5];
    const uint32_t src    = (uint32_t)ctx->gpr[6];
    const uint32_t blocks = n >> 5;
    const uint32_t rem    = n & 31u;

    if (!plain_range(c, 0x10)) { call_original(ctx); return; }
    ensure_title_callback(c);
    const uint32_t end = vm_read32(c + 0x4);
    uint32_t cur = vm_read32(c + 0x8);

    /* Bytes reserved (exactly as the guest computes them, 32-bit wrap included)
     * and bytes written; the two are the same for every n. */
    const uint32_t need = (blocks * 34u + (rem ? rem + 2u : 0u)) << 2;
    const uint64_t out_bytes = (uint64_t)blocks * 0x88u + (rem ? 8u + 4u * rem : 0u);
    const uint64_t in_bytes  = (uint64_t)n * 4u;
    if (out_bytes > 0x10000000u || in_bytes > 0x10000000u ||
        !plain_range(src, (uint32_t)in_bytes)) {
        call_original(ctx);
        return;
    }

    if ((uint32_t)(cur + need) > end) {
        /* Ring full: wrap it here rather than through the callback. */
        if (!g_callback_opd.load(std::memory_order_relaxed) || !ring_wrap(c, need)) {
            call_original(ctx);
            return;
        }
        cur = vm_read32(c + 0x8);
        if ((uint32_t)(cur + need) > end) { call_original(ctx); return; }
    }
    if (!plain_range(cur, (uint32_t)out_bytes) ||
        !unreserved_range(cur, (uint32_t)out_bytes) || !unreserved_range(c + 8u, 4u)) {
        call_original(ctx);
        return;
    }

    /* Do not write into the unread tail of the previous lap: a walker ahead of
     * the write head, inside the ring, is still reading it. */
    Ring ring;
    if (read_ring(c, ring) && cur >= ring.begin && cur < ring.end) {
        const uint32_t lo = ring.io(cur), hi = lo + (uint32_t)out_bytes + 4u;
        auto blocked = [&](uint32_t g) { return ring.in(g) && g > lo && g < hi; };
        if (blocked(cellGcm_fifo_get_offset())) {
            const uint64_t w0 = fp_ticks();
            uint64_t waited = 0;
            bool timed_out = false;
            do {
                fp_sleep_100us();
                waited = fp_ticks() - w0;
                if (fp_ticks_to_us(waited) >= kLapTimeoutUs) { timed_out = true; break; }
            } while (blocked(cellGcm_fifo_get_offset()));
            PROFILE_STAT(s.lap_waits.fetch_add(1, std::memory_order_relaxed));
            PROFILE_STAT(s.lap_wait_ticks.fetch_add(waited, std::memory_order_relaxed));
            if (timed_out) {
                PROFILE_STAT(s.lap_timeouts.fetch_add(1, std::memory_order_relaxed));
                call_original(ctx);
                return;
            }
        }
    }

    uint32_t out = cur, in = src;
    const uint64_t index0 = ctx->gpr[4];
    for (uint32_t b = 0; b < blocks; ++b) {
        store_be32(out + 0, 0x00841EFCu);
        store_be32(out + 4, (uint32_t)(index0 + 8ull * b));
        memcpy(vm_base + out + 8, vm_base + in, 0x80);
        out += 0x88;
        in  += 0x80;
    }
    if (rem) {
        store_be32(out + 0, ((rem + 1u) << 18) | 0x1EFCu);
        store_be32(out + 4, (uint32_t)(index0 + 8ull * blocks));
        memcpy(vm_base + out + 8, vm_base + in, 4u * rem);
        out += 8u + 4u * rem;
    }
    if (out != cur) store_be32(c + 8, out);

    /* r3 as the original leaves it: index after the last full block, or the
     * context pointer untouched when there were no full blocks. */
    if (blocks) ctx->gpr[3] = index0 + 8ull * blocks;

    const uint64_t t1 = profile_ticks();
    PROFILE_STAT(s.fast_calls.fetch_add(1, std::memory_order_relaxed));
    PROFILE_STAT(s.words.fetch_add(n, std::memory_order_relaxed));
    PROFILE_STAT(s.fast_ticks.fetch_add(t1 - t0, std::memory_order_relaxed));
    report(t1);
}
