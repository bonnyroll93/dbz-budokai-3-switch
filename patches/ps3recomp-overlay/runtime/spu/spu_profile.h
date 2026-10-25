#ifndef PS3_SPU_PROFILE_H
#define PS3_SPU_PROFILE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Diagnostic function sampling, not a hardware CPU profiler. One slot per
 * host thread. Relaxed atomics keep the observer race-free without barriers
 * in lifted arithmetic. A zero stamp denotes WAIT_SIGNAL/outside lifted code. */
int ps3_spu_profile_start(void);
uint64_t *ps3_spu_profile_slot(void);
void ps3_spu_profile_bind(uint32_t taskset, uint32_t task);
extern _Thread_local uint64_t *ps3_spu_profile_current;
/* T26: per-thread counts of lifted function entries, and of entries to one
 * marked function (image 2 = bnusCore audio SPU, 0x0A9A0 = its hottest DSP
 * loop), so a window says how much work a thread did, not only where. */
extern _Thread_local uint64_t *ps3_spu_profile_calls;
#define PS3_SPU_PROFILE_MARK_IMAGE 2u
#define PS3_SPU_PROFILE_MARK_PC 0x0A9A0u

typedef struct { uint64_t *slot; uint64_t previous; } Ps3SpuProfileScope;
static inline Ps3SpuProfileScope ps3_spu_profile_enter(unsigned image, unsigned pc)
{
    uint64_t *slot = ps3_spu_profile_current;
    if (!slot) slot = ps3_spu_profile_slot();
    uint64_t *calls = ps3_spu_profile_calls;
    if (calls) {
        __atomic_store_n(&calls[0], calls[0] + 1, __ATOMIC_RELAXED);
        if (pc == PS3_SPU_PROFILE_MARK_PC && image == PS3_SPU_PROFILE_MARK_IMAGE)
            __atomic_store_n(&calls[1], calls[1] + 1, __ATOMIC_RELAXED);
    }
    Ps3SpuProfileScope scope = {slot, __atomic_load_n(slot, __ATOMIC_RELAXED)};
    __atomic_store_n(slot, ((uint64_t)image << 32) | pc, __ATOMIC_RELAXED);
    return scope;
}
static inline void ps3_spu_profile_leave(Ps3SpuProfileScope *scope)
{
    __atomic_store_n(scope->slot, scope->previous, __ATOMIC_RELAXED);
}
#define SPU_PROFILE_SCOPE(image, pc) \
    Ps3SpuProfileScope spu_profile_scope __attribute__((cleanup(ps3_spu_profile_leave))) = \
        ps3_spu_profile_enter((image), (pc))
#ifdef __cplusplus
}
#endif
#endif
