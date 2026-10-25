#include "switch_time.h"

#include <switch.h>
#include <limits.h>

static uint64_t switch_monotonic_us(void *context)
{
    (void)context;
    uint64_t ticks = armGetSystemTick();
    uint64_t frequency = armGetSystemTickFreq();
    if (!frequency) return 0;
    return (ticks / frequency) * 1000000u +
        ((ticks % frequency) * 1000000u) / frequency;
}

static void switch_sleep_us(void *context, uint64_t duration_us)
{
    (void)context;
    if (!duration_us) { svcSleepThread(YieldType_WithCoreMigration); return; }
    const uint64_t max_us = (uint64_t)INT64_MAX / 1000;
    while (duration_us) {
        uint64_t chunk = duration_us > max_us ? max_us : duration_us;
        svcSleepThread((int64_t)(chunk * 1000));
        duration_us -= chunk;
    }
}

const Ps3PlatformTime *ps3_platform_switch_time(void)
{
    static const Ps3PlatformTime ops = {
        .context = NULL,
        .monotonic_us = switch_monotonic_us,
        .sleep_us = switch_sleep_us,
    };
    return &ops;
}
