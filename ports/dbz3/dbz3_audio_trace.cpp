/*
 * DBZ3 -- trace of CRI's output into the surround mixer (A4, music missing).
 *
 * func_0021ED98 is a callback (only reached through the indirect-call table,
 * ppu_recomp_005.cpp) that forwards to func_001FFD20 =
 * cellSurMixerSurBusAddData(bus = r4, offset = r5, addr = r6, samples = r7)
 * of the statically linked libmixer, and prints "E2005081906 : Failed in
 * cellSurMixerSurBusAddData()" when it returns < 0. Music (stereo 48 kHz ADX
 * streams) and movie audio are missing while mono voices play, so this logs
 * whether the stream output reaches the mixer: calls, failures, buses, sample
 * counts and the peak of the float PCM handed over.
 *
 * T27 log: sdmc:/switch/dbz-budokai-3-switch/t27-adx.log, one line per ~5 s.
 */

#define PPU_FAST_VM 1
#include "ppu_recomp.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
static inline uint64_t at_ticks(void) { return armGetSystemTick(); }
static inline uint64_t at_us(uint64_t t) { return t / 19u; }
#else
static inline uint64_t at_ticks(void) { return 0; }
static inline uint64_t at_us(uint64_t t) { return t; }
#endif

extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#define DRAIN_TRAMPOLINE(ctx) do { \
    while (g_trampoline_fn) { \
        void (*_tf)(void*) = g_trampoline_fn; \
        g_trampoline_fn = 0; \
        _tf((void*)(ctx)); \
    } \
} while (0)

namespace {

struct Stats {
    uint64_t calls, fails, samples;
    uint32_t bus_mask;          /* bit per bus number 0..31 */
    uint32_t min_samples, max_samples;
    float peak;
    int32_t last_ret;
    uint32_t last_addr, last_off;
} g_s;

FILE* g_log = nullptr;
uint64_t g_window = 0;

} // namespace

/* Also printed by dbz3_loop_prof.cpp every 5 s, so "never called" shows. */
extern "C" { uint64_t g_dbz3_adx_sur_calls = 0; }
#define g_total_calls g_dbz3_adx_sur_calls

namespace {

void flush(uint64_t now)
{
    if (!g_log) g_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t27-adx.log", "w");
    if (!g_log) return;
    fprintf(g_log, "window_ms=%llu calls=%llu fails=%llu last_ret=%d buses=0x%X samples=%llu"
                   " per_call=%u..%u peak=%.4f last_addr=0x%08X last_off=%u total_calls=%llu\n",
            (unsigned long long)(at_us(now - g_window) / 1000u),
            (unsigned long long)g_s.calls, (unsigned long long)g_s.fails, g_s.last_ret,
            g_s.bus_mask, (unsigned long long)g_s.samples,
            g_s.calls ? g_s.min_samples : 0u, g_s.max_samples, g_s.peak,
            g_s.last_addr, g_s.last_off, (unsigned long long)g_total_calls);
    fflush(g_log);
    memset(&g_s, 0, sizeof g_s);
    g_window = now;
}

} // namespace

extern "C" void real_func_0021ED98(ppu_context* ctx)
    __asm__("__real__Z13func_0021ED98P11ppu_context");
extern "C" void wrap_func_0021ED98(ppu_context* ctx)
    __asm__("__wrap__Z13func_0021ED98P11ppu_context");
extern "C" void wrap_func_0021ED98(ppu_context* ctx)
{
    const uint32_t bus = (uint32_t)ctx->gpr[4];
    const uint32_t off = (uint32_t)ctx->gpr[5];
    const uint32_t addr = (uint32_t)ctx->gpr[6];
    const uint32_t samples = (uint32_t)ctx->gpr[7];

    /* Peak of the PCM handed over (float, big-endian), at most 4096 values. */
    float peak = 0.0f;
    if (addr >= 0x10000u && samples && samples <= 0x10000u) {
        const uint32_t n = samples > 4096u ? 4096u : samples;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t w = vm_read32(addr + 4u * i);
            float f; memcpy(&f, &w, 4);
            f = fabsf(f);
            if (f > peak && f < 1e6f) peak = f;
        }
    }

    real_func_0021ED98(ctx);
    DRAIN_TRAMPOLINE(ctx);
    const int32_t ret = (int32_t)ctx->gpr[3];

    const uint64_t now = at_ticks();
    if (!g_window) g_window = now;
    if (g_s.calls == 0 || samples < g_s.min_samples) g_s.min_samples = samples;
    if (samples > g_s.max_samples) g_s.max_samples = samples;
    g_s.calls++;
    g_total_calls++;
    if (ret == 0) g_s.fails++;             /* the callback returns 1 = ok, 0 = failed */
    g_s.samples += samples;
    if (bus < 32u) g_s.bus_mask |= 1u << bus;
    if (peak > g_s.peak) g_s.peak = peak;
    g_s.last_ret = ret;
    g_s.last_addr = addr;
    g_s.last_off = off;
    if (at_us(now - g_window) >= 5000000u) flush(now);
}
