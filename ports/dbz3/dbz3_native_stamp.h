/*
 * DBZ3: native functions in the T23 PPU profile.
 *
 * Lifted functions stamp themselves as the main thread's current function
 * (scripts/ppu_instrument.py); native replacements did not, so their time was
 * charged to whichever lifted function called them -- directly or through a
 * virtual call. In -ProfilePpuFunctions builds (DBZ3_NATIVE_STAMPS) each native
 * entry point stamps its guest address with the top bit set, so the profile
 * shows it as fn=0x8xxxxxxx. Other builds: nothing.
 */
#pragma once
#include <cstdint>

#if defined(DBZ3_NATIVE_STAMPS)
struct ppu_context;
extern "C" ppu_context* volatile g_ppu_main_ctx;
extern "C" volatile uint32_t g_ppu_fn_stamp;
extern "C" volatile uint32_t g_ppu_fn_stack[256];
extern "C" volatile uint32_t g_ppu_fn_depth;
struct Dbz3NativeStamp {
    uint32_t prev, prev_depth;
    bool on;
    Dbz3NativeStamp(const ppu_context* c, uint32_t guest)
        : prev(0), prev_depth(0), on(c == g_ppu_main_ctx)
    {
        if (on) {
            const uint32_t a = guest | 0x80000000u;
            prev = g_ppu_fn_stamp; g_ppu_fn_stamp = a;
            prev_depth = g_ppu_fn_depth;
            if (prev_depth < 256u) g_ppu_fn_stack[prev_depth] = a;
            g_ppu_fn_depth = prev_depth + 1u;
        }
    }
    ~Dbz3NativeStamp() { if (on) { g_ppu_fn_stamp = prev; g_ppu_fn_depth = prev_depth; } }
};
#define DBZ3_NATIVE_STAMP(ctx, guest) Dbz3NativeStamp dbz3_native_stamp_((ctx), (guest))
#else
#define DBZ3_NATIVE_STAMP(ctx, guest) ((void)0)
#endif
