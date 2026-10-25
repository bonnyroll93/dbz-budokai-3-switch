#ifndef PS3RECOMP_SWITCH_FRAME_CLOCK_H
#define PS3RECOMP_SWITCH_FRAME_CLOCK_H

#include <stdint.h>

#include "ps3emu/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Ps3SwitchFrameTick)(void *argument);

typedef struct Ps3SwitchFrameClock {
    void *thread;
    Ps3SwitchFrameTick callback;
    void *argument;
    uint32_t running;
    uint64_t tick_count;
} Ps3SwitchFrameClock;

Ps3PlatformStatus ps3_switch_frame_clock_start(Ps3SwitchFrameClock *clock,
                                                Ps3SwitchFrameTick callback,
                                                void *argument);
void ps3_switch_frame_clock_stop(Ps3SwitchFrameClock *clock);
uint64_t ps3_switch_frame_clock_ticks(const Ps3SwitchFrameClock *clock);

#ifdef __cplusplus
}
#endif

#endif
