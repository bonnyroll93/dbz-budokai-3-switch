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
    int show_fps_stats;
    int vsync;          /* 1: present on the display refresh, 0: present at once */
    int frame_cap;      /* 60 (drops to 30 when slow), 30, or 0 = off (30-60, never forced to 30) */
    /* OVERCLOCK rows: chosen MHz (0 = system default) and the clock read back
     * from the system about once a second. */
    int oc_mhz[3];      /* CPU, GPU, RAM */
    int oc_now_mhz[3];
    /* EXTRA REPEAT row (Logic60 bisect, not saved): 0 = off, 1 = every draw
     * task, otherwise the guest function of the one draw task repeated. */
    int extra_repeat;
} SwitchDebugSettings;

/* Debug menu rows (switch_input.c and the menu drawing share them). */
enum {
    DBG_ROW_SCALE, DBG_ROW_FXAA, DBG_ROW_VSYNC, DBG_ROW_CAP,
    DBG_ROW_FPS, DBG_ROW_FRAME_MS, DBG_ROW_FPS_STATS, DBG_ROW_FPS_RESET, DBG_ROW_EXTRA,
    DBG_ROW_OC_CPU, DBG_ROW_OC_GPU, DBG_ROW_OC_RAM,
    DBG_ROW_SCREENSHOT, DBG_ROW_RECORD, DBG_ROW_DRAW_START, DBG_ROW_DRAW_STEP, DBG_ROW_DRAW_CAPTURE,
    DBG_ROW_REBOOT, DBG_ROW_CLOSE,
    DBG_ROW_COUNT
};

void switch_debug_init(void);
SwitchDebugSettings switch_debug_snapshot(void);
/* FRAME CAP row: 60, 30 or 0 (off). */
int switch_debug_frame_cap(void);
/* EXTRA REPEAT row: 0 off, 1 all draw tasks, else one task function. */
int switch_debug_extra_repeat(void);
/* Provided by the port (dbz3_loop_prof.cpp) when it has Logic60. next: the
 * row value after cur (off, all, then the draw tasks active in the scene, by
 * address). info: fn's place among the active draw tasks (0 = not active)
 * and their count; returns nonzero if its repeat was stopped (ring). */
uint32_t dbz3_bisect_next(uint32_t cur, int delta) __attribute__((weak));
int dbz3_bisect_info(uint32_t fn, int *pos, int *count) __attribute__((weak));
/* Returns nonzero when the controller belongs to the debug menu. */
int switch_debug_input(uint64_t buttons);
int switch_debug_take_reboot(void);
void switch_debug_reboot_failed(int result);
int switch_debug_take_screenshot(void);
/* Nonzero once after RESET FPS STATS was chosen in the menu. */
int switch_debug_take_fps_reset(void);
int switch_debug_recording(void);
void switch_debug_stop_recording(void);
int switch_debug_take_draw_capture(int *start, int *step);

#endif
