#ifndef PS3RECOMP_SWITCH_FILE_H
#define PS3RECOMP_SWITCH_FILE_H

#include "ps3emu/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paths must already be translated from PS3 mounts by switch_vfs. */
const Ps3PlatformFileOps *ps3_platform_switch_file(void);

#ifdef __cplusplus
}
#endif

#endif
