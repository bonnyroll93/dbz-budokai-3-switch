#ifndef PS3RECOMP_SWITCH_DISPLAY_H
#define PS3RECOMP_SWITCH_DISPLAY_H

#include "ps3emu/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Release libnx's text console before opening the default window. */
const Ps3PlatformDisplay *ps3_platform_switch_display(void);

#ifdef __cplusplus
}
#endif

#endif
