/* Store hook for native code that writes guest memory directly.
 *
 * In a PORT_DBZ3_WTRACK build every guest store made while the hook is on
 * (store tracker or undo log, dbz3_wtrack.cpp) has to be reported, the native
 * replacements' stores included, or the undo log could not roll them back.
 * Elsewhere the hook compiles to nothing. */
#ifndef DBZ3_STORE_HOOK_H
#define DBZ3_STORE_HOOK_H
#include <stdint.h>

#ifdef DBZ3_WTRACK
extern "C" volatile int g_dbz3_wtrack_on;
extern "C" void dbz3_wtrack_note(uint32_t a, uint32_t n);
static inline void dbz3_store_hook(uint32_t a, uint32_t n)
{
    if (__builtin_expect(g_dbz3_wtrack_on, 0)) dbz3_wtrack_note(a, n);
}
#else
static inline void dbz3_store_hook(uint32_t, uint32_t) {}
#endif

#endif
