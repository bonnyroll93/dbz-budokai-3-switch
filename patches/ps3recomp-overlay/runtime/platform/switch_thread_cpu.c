#include <switch.h>
#include <stdint.h>

/* Keep libnx types out of the SPU runtime, which defines its own u128. */
uint32_t ps3_switch_current_thread_handle(void)
{
    return (uint32_t)threadGetCurHandle();
}

uint64_t ps3_switch_thread_cpu_ns(void)
{
    u64 ticks = 0;
    Result rc = svcGetInfo(&ticks, InfoType_ThreadTickCount,
                           CUR_THREAD_HANDLE, TickCountInfo_Total);
    if (R_FAILED(rc))
        rc = svcGetInfo(&ticks, InfoType_ThreadTickCountDeprecated,
                        CUR_THREAD_HANDLE, TickCountInfo_Total);
    return R_SUCCEEDED(rc) ? armTicksToNs(ticks) : 0;
}

uint64_t ps3_switch_thread_cpu_ns_for(uint32_t handle)
{
    u64 ticks = 0;
    Result rc = svcGetInfo(&ticks, InfoType_ThreadTickCount,
                           (Handle)handle, TickCountInfo_Total);
    if (R_FAILED(rc))
        rc = svcGetInfo(&ticks, InfoType_ThreadTickCountDeprecated,
                        (Handle)handle, TickCountInfo_Total);
    return R_SUCCEEDED(rc) ? armTicksToNs(ticks) : 0;
}

/* T33: SPU thread group to run at the lowest host priority (lv2_register.c);
 * set by a port, NULL = none. */
const char* g_spu_lowprio_group = NULL;

int ps3_switch_set_current_thread_priority(int prio)
{
    return R_SUCCEEDED(svcSetThreadPriority(CUR_THREAD_HANDLE, (u32)prio));
}
