#include "switch_frame_clock.h"

#include <stddef.h>

#include "switch_thread.h"
#include "switch_time.h"

#define FRAME_PERIOD_US 16667u

static void switch_frame_worker(void *argument)
{
    Ps3SwitchFrameClock *clock = (Ps3SwitchFrameClock *)argument;
    const Ps3PlatformTime *time = ps3_platform_switch_time();
    uint64_t next = time->monotonic_us(time->context) + FRAME_PERIOD_US;
    while (__atomic_load_n(&clock->running, __ATOMIC_ACQUIRE)) {
        uint64_t now = time->monotonic_us(time->context);
        if (now < next) {
            time->sleep_us(time->context, next - now);
            continue;
        }
        clock->callback(clock->argument);
        __atomic_add_fetch(&clock->tick_count, 1, __ATOMIC_RELEASE);
        next += FRAME_PERIOD_US;
        /* Resync after a long pause so we do not burst old callbacks. */
        if (now > next + 4u * FRAME_PERIOD_US)
            next = now + FRAME_PERIOD_US;
    }
}

Ps3PlatformStatus ps3_switch_frame_clock_start(Ps3SwitchFrameClock *clock,
                                                Ps3SwitchFrameTick callback,
                                                void *argument)
{
    if (!clock || !callback || clock->thread) return PS3_PLATFORM_INVALID_ARGUMENT;
    clock->callback = callback;
    clock->argument = argument;
    clock->tick_count = 0;
    __atomic_store_n(&clock->running, 1, __ATOMIC_RELEASE);
    const Ps3PlatformThreadOps *threads = ps3_platform_switch_thread();
    Ps3PlatformStatus status = threads->create(threads->context, &clock->thread,
        switch_frame_worker, clock, 64 * 1024, 0);
    if (status != PS3_PLATFORM_OK)
        __atomic_store_n(&clock->running, 0, __ATOMIC_RELEASE);
    return status;
}

void ps3_switch_frame_clock_stop(Ps3SwitchFrameClock *clock)
{
    if (!clock || !clock->thread) return;
    __atomic_store_n(&clock->running, 0, __ATOMIC_RELEASE);
    const Ps3PlatformThreadOps *threads = ps3_platform_switch_thread();
    threads->join(threads->context, clock->thread, PS3_PLATFORM_WAIT_FOREVER);
    threads->detach(threads->context, clock->thread);
    clock->thread = NULL;
}

uint64_t ps3_switch_frame_clock_ticks(const Ps3SwitchFrameClock *clock)
{
    return clock ? __atomic_load_n(&clock->tick_count, __ATOMIC_ACQUIRE) : 0;
}
