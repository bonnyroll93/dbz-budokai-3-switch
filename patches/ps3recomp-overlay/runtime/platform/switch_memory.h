#ifndef PS3RECOMP_SWITCH_MEMORY_H
#define PS3RECOMP_SWITCH_MEMORY_H

#include "ps3emu/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single process-wide 4 GiB guest arena in Horizon's alias region. */
const Ps3PlatformMemory *ps3_platform_switch_memory(void);
uint64_t ps3_switch_used_memory(void);
int ps3_switch_is_no_access(const void *address);
int ps3_switch_is_readable_range(const void *address, uint64_t size);
/* One line of process resource numbers (memory, kernel resource, threads,
 * heap) plus a probe thread, to stderr and to t22-gpu.log. */
void ps3_switch_resource_report(const char *tag);

#ifdef __cplusplus
}
#endif

#endif
