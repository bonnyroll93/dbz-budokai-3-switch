#ifndef PS3_SWITCH_DEBUG_H
#define PS3_SWITCH_DEBUG_H

#include <stdint.h>

typedef struct SwitchDebugSettings {
    int scale_tenths;
    int active_scale_tenths;
    int show_fps;
    int show_frame_ms;
    int menu_open;
    int selected;
    int reboot_error;
    int recording;
    int draw_start;
    int draw_step;
    int gpu_aa;
} SwitchDebugSettings;

void switch_debug_init(void);
SwitchDebugSettings switch_debug_snapshot(void);
/* Returns nonzero when the controller belongs to the debug menu. */
int switch_debug_input(uint64_t buttons);
int switch_debug_take_reboot(void);
void switch_debug_reboot_failed(int result);
int switch_debug_take_screenshot(void);
int switch_debug_recording(void);
void switch_debug_stop_recording(void);
int switch_debug_take_draw_capture(int *start, int *step);

#endif
