#ifndef PS3RECOMP_SWITCH_FIBER_CONTEXT_H
#define PS3RECOMP_SWITCH_FIBER_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

/* AAPCS64 callee-saved state. The caller's volatile registers are already
 * dead at a C call boundary. Keep this layout in sync with the assembly. */
typedef struct Ps3SwitchFiberContext {
    uint64_t x19_x30[12];
    uint64_t d8_d15[8];
    uint64_t sp;
} Ps3SwitchFiberContext;

void ps3_switch_fiber_init(Ps3SwitchFiberContext *ctx, void *stack,
                           size_t stack_size, void (*entry)(void));
void ps3_switch_fiber_swap(Ps3SwitchFiberContext *from,
                           const Ps3SwitchFiberContext *to);

#endif
