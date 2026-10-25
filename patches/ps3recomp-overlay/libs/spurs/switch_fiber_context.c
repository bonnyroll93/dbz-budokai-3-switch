#include "switch_fiber_context.h"

#include <string.h>

_Static_assert(offsetof(Ps3SwitchFiberContext, d8_d15) == 96, "fiber SIMD offset");
_Static_assert(offsetof(Ps3SwitchFiberContext, sp) == 160, "fiber SP offset");

void ps3_switch_fiber_init(Ps3SwitchFiberContext *ctx, void *stack,
                           size_t stack_size, void (*entry)(void))
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->sp = ((uintptr_t)stack + stack_size) & ~(uintptr_t)15;
    ctx->x19_x30[11] = (uintptr_t)entry; /* saved LR, loaded into x30 */
}

/* A global assembly function avoids a compiler-generated prologue. Switches
 * happen at a C call boundary; x18 is reserved by the platform ABI. */
__asm__(
        ".text\n"
        ".p2align 2\n"
        ".global ps3_switch_fiber_swap\n"
        ".type ps3_switch_fiber_swap, %function\n"
        "ps3_switch_fiber_swap:\n"
        "stp x19, x20, [x0, #0]\n"
        "stp x21, x22, [x0, #16]\n"
        "stp x23, x24, [x0, #32]\n"
        "stp x25, x26, [x0, #48]\n"
        "stp x27, x28, [x0, #64]\n"
        "stp x29, x30, [x0, #80]\n"
        "stp d8, d9, [x0, #96]\n"
        "stp d10, d11, [x0, #112]\n"
        "stp d12, d13, [x0, #128]\n"
        "stp d14, d15, [x0, #144]\n"
        "mov x9, sp\n"
        "str x9, [x0, #160]\n"
        "ldp x19, x20, [x1, #0]\n"
        "ldp x21, x22, [x1, #16]\n"
        "ldp x23, x24, [x1, #32]\n"
        "ldp x25, x26, [x1, #48]\n"
        "ldp x27, x28, [x1, #64]\n"
        "ldp x29, x30, [x1, #80]\n"
        "ldp d8, d9, [x1, #96]\n"
        "ldp d10, d11, [x1, #112]\n"
        "ldp d12, d13, [x1, #128]\n"
        "ldp d14, d15, [x1, #144]\n"
        "ldr x9, [x1, #160]\n"
        "mov sp, x9\n"
        "ret\n"
        ".size ps3_switch_fiber_swap, .-ps3_switch_fiber_swap\n"
);
