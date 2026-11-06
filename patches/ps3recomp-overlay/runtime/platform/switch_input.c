#include "switch_input.h"
#include "switch_debug.h"

#include <switch.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PadState controllers[PS3_PLATFORM_PAD_COUNT];
static bool configured;
static bool sampler_running;
static Mutex sample_lock;
static Thread sample_thread;

/* Keep button edges until cellPadGetData can consume them. At 14 FPS the
 * guest can leave 70+ ms between polls, longer than a normal button tap. */
#define INPUT_QUEUE_CAP 128u
static Ps3PlatformPadState sample_queue[INPUT_QUEUE_CAP][PS3_PLATFORM_PAD_COUNT];
static Ps3PlatformPadState latest_sample[PS3_PLATFORM_PAD_COUNT];
static unsigned sample_head;
static unsigned sample_count;

#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
/* All counters and timestamps below are protected by sample_lock. */
static u64 sample_queue_tick[INPUT_QUEUE_CAP];
static FILE *input_queue_log;
static u64 input_queue_window_tick;
static u64 input_queue_last_poll_tick;
typedef struct InputQueueStats {
    u64 polls, empty, enqueued, delivered, overflow, menu_cleared;
    u64 age_total_us, age_max_us, age_ge_50ms, age_ge_100ms, age_ge_250ms;
    u64 poll_gap_max_us;
    unsigned depth_max;
} InputQueueStats;
static InputQueueStats input_queue_stats;

static u64 input_queue_elapsed_us(u64 from, u64 to)
{
    return (to - from) * 1000000ull / armGetSystemTickFreq();
}
#endif

#define DEBUG_SETTINGS_PATH "sdmc:/switch/dbz-budokai-3-switch/debug-settings.cfg"
static atomic_int debug_init_state;
static atomic_int debug_scale;
static atomic_int debug_fps;
static atomic_int debug_frame_ms;
static atomic_int debug_menu;
static atomic_int debug_selected;
static atomic_int debug_reboot;
static atomic_int debug_reboot_error;
static atomic_int debug_screenshot;
static atomic_int debug_recording;
static atomic_int debug_draw_capture;
static atomic_int debug_draw_start;
static atomic_int debug_draw_step = 8;
static atomic_int debug_gpu_aa;
static atomic_int debug_fps_stats;
static atomic_int debug_fps_reset;
static atomic_int debug_vsync = 1;
static atomic_int debug_cap = 60;   /* 60, 30 or 0 (off) */
static atomic_int debug_extra_repeat;   /* EXTRA REPEAT row, not saved */
static int debug_active_scale;
static u64 debug_old_buttons;
static int debug_suppress_until_release;

/* ---- OVERCLOCK (debug menu) -------------------------------------------------
 * Like sys-clk: CPU, GPU and RAM clocks set through clkrst (pcv before 8.0.0),
 * limited to the recommended official rates. 0 = the system's clock, left
 * alone. The system puts its own clocks back on dock changes and performance
 * mode changes, so the chosen rates are checked and set again about once a
 * second; the system rate seen last is put back when DEFAULT is chosen again
 * and at exit. Nothing is read or set while every row is DEFAULT and the
 * menu is closed. */
static const int oc_cpu_choices[] = { 0, 1224, 1428, 1581, 1785 };
static const int oc_gpu_choices[] = { 0, 460, 614, 768, 921 };
static const int oc_ram_choices[] = { 0, 1600 };
static const int* const oc_choices[3] = { oc_cpu_choices, oc_gpu_choices, oc_ram_choices };
static const int oc_choice_count[3] = { 5, 5, 2 };
static atomic_int debug_oc[3];       /* MHz, 0 = default */
static atomic_int debug_oc_now[3];   /* MHz read back */
static u32 oc_stock_hz[3];
static int oc_applied[3];
static int oc_service = -1;          /* -1 not tried, 0 unavailable, 1 clkrst, 2 pcv */
static Mutex oc_lock;
static u64 oc_last_tick;
static const PcvModuleId oc_module_id[3] = { PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC };
static const PcvModule oc_module[3] = { PcvModule_CpuBus, PcvModule_GPU, PcvModule_EMC };

static int oc_valid(int domain, int mhz)
{
    for (int i = 0; i < oc_choice_count[domain]; i++)
        if (oc_choices[domain][i] == mhz) return 1;
    return 0;
}

static u32 oc_hz(int domain, int mhz)
{
    if (domain == 1)   /* GPU rates are multiples of 76.8 MHz */
        return mhz == 460 ? 460800000u : mhz == 614 ? 614400000u :
               mhz == 768 ? 768000000u : 921600000u;
    return (u32)mhz * 1000000u;
}

static void oc_service_init(void)
{
    if (oc_service >= 0) return;
    if (hosversionAtLeast(8, 0, 0)) oc_service = R_SUCCEEDED(clkrstInitialize()) ? 1 : 0;
    else oc_service = R_SUCCEEDED(pcvInitialize()) ? 2 : 0;
}

static int oc_get(int d, u32 *hz)
{
    if (oc_service == 1) {
        ClkrstSession session;
        if (R_FAILED(clkrstOpenSession(&session, oc_module_id[d], 3))) return 0;
        const Result rc = clkrstGetClockRate(&session, hz);
        clkrstCloseSession(&session);
        return R_SUCCEEDED(rc);
    }
    if (oc_service == 2) return R_SUCCEEDED(pcvGetClockRate(oc_module[d], hz));
    return 0;
}

static void oc_set(int d, u32 hz)
{
    if (oc_service == 1) {
        ClkrstSession session;
        if (R_FAILED(clkrstOpenSession(&session, oc_module_id[d], 3))) return;
        clkrstSetClockRate(&session, hz);
        clkrstCloseSession(&session);
    } else if (oc_service == 2) {
        pcvSetClockRate(oc_module[d], hz);
    }
}

static void oc_maintain(int force)
{
    int wanted = 0;
    for (int d = 0; d < 3; d++) wanted |= atomic_load(&debug_oc[d]) != 0 || oc_applied[d];
    if (!force && !wanted && !atomic_load(&debug_menu)) return;
    const u64 now_tick = armGetSystemTick();
    if (!force && now_tick - oc_last_tick < armGetSystemTickFreq()) return;
    oc_last_tick = now_tick;
    mutexLock(&oc_lock);
    oc_service_init();
    for (int d = 0; d < 3 && oc_service > 0; d++) {
        const int mhz = atomic_load(&debug_oc[d]);
        u32 now = 0;
        const int have = oc_get(d, &now);
        if (!mhz) {
            if (oc_applied[d]) {
                oc_set(d, oc_stock_hz[d]);
                oc_applied[d] = 0;
            }
        } else if (have) {
            const u32 want = oc_hz(d, mhz);
            if (now != want) {
                /* Not ours: the system's rate for the current mode. */
                oc_stock_hz[d] = now;
                oc_applied[d] = 1;
                oc_set(d, want);
            }
        }
        if (oc_get(d, &now)) atomic_store(&debug_oc_now[d], (int)((now + 500000u) / 1000000u));
    }
    mutexUnlock(&oc_lock);
}

static void oc_restore_at_exit(void)
{
    for (int d = 0; d < 3; d++) atomic_store(&debug_oc[d], 0);
    oc_maintain(1);
}

void switch_debug_init(void)
{
    int expected = 0;
    if (atomic_compare_exchange_strong(&debug_init_state, &expected, 1)) {
        int scale = 10, fps = 1, frame_ms = 0, gpu_aa = 0, fps_stats = 0, vsync = 1, cap = 60;
        int oc[3] = { 0, 0, 0 };
        FILE *f = fopen(DEBUG_SETTINGS_PATH, "r");
        if (f) {
            char line[80];
            while (fgets(line, sizeof line, f)) {
                int value;
                if (sscanf(line, "render_scale_tenths=%d", &value) == 1 && value >= 0 && value <= 10)
                    scale = value;
                else if (sscanf(line, "show_fps=%d", &value) == 1) fps = !!value;
                else if (sscanf(line, "show_frame_ms=%d", &value) == 1) frame_ms = !!value;
                else if (sscanf(line, "gpu_fxaa=%d", &value) == 1) gpu_aa = !!value;
                else if (sscanf(line, "show_fps_stats=%d", &value) == 1) fps_stats = !!value;
                else if (sscanf(line, "vsync=%d", &value) == 1) vsync = !!value;
                else if (sscanf(line, "frame_cap=%d", &value) == 1)
                    cap = (value == 30 || value == 0) ? value : 60;
                else if (sscanf(line, "oc_cpu_mhz=%d", &value) == 1 && oc_valid(0, value)) oc[0] = value;
                else if (sscanf(line, "oc_gpu_mhz=%d", &value) == 1 && oc_valid(1, value)) oc[1] = value;
                else if (sscanf(line, "oc_ram_mhz=%d", &value) == 1 && oc_valid(2, value)) oc[2] = value;
            }
            fclose(f);
        } else {
            /* Preserve the older SD-card switch until settings are first saved. */
            f = fopen("sdmc:/switch/dbz-budokai-3-switch/fps-off", "r");
            if (f) { fps = 0; fclose(f); }
        }
        debug_active_scale = scale;
        atomic_store(&debug_scale, scale);
        atomic_store(&debug_fps, fps);
        atomic_store(&debug_frame_ms, frame_ms);
        atomic_store(&debug_gpu_aa, gpu_aa);
        atomic_store(&debug_fps_stats, fps_stats);
        atomic_store(&debug_vsync, vsync);
        atomic_store(&debug_cap, cap);
        for (int d = 0; d < 3; d++) atomic_store(&debug_oc[d], oc[d]);
        atomic_store(&debug_init_state, 2);
        atexit(oc_restore_at_exit);
        if (oc[0] || oc[1] || oc[2]) oc_maintain(1);
    } else {
        while (atomic_load(&debug_init_state) != 2) svcSleepThread(1000000);
    }
}

static void switch_debug_save(void)
{
    FILE *f = fopen(DEBUG_SETTINGS_PATH ".tmp", "w");
    if (!f) return;
    fprintf(f, "render_scale_tenths=%d\nshow_fps=%d\nshow_frame_ms=%d\ngpu_fxaa=%d\nshow_fps_stats=%d\n"
               "vsync=%d\nframe_cap=%d\noc_cpu_mhz=%d\noc_gpu_mhz=%d\noc_ram_mhz=%d\n",
            atomic_load(&debug_scale), atomic_load(&debug_fps), atomic_load(&debug_frame_ms),
            atomic_load(&debug_gpu_aa), atomic_load(&debug_fps_stats),
            atomic_load(&debug_vsync), atomic_load(&debug_cap),
            atomic_load(&debug_oc[0]), atomic_load(&debug_oc[1]), atomic_load(&debug_oc[2]));
    if (fclose(f) != 0) return;
    remove(DEBUG_SETTINGS_PATH);
    rename(DEBUG_SETTINGS_PATH ".tmp", DEBUG_SETTINGS_PATH);
}

SwitchDebugSettings switch_debug_snapshot(void)
{
    switch_debug_init();
    SwitchDebugSettings s = {
        atomic_load(&debug_scale), debug_active_scale,
        atomic_load(&debug_fps), atomic_load(&debug_frame_ms),
        atomic_load(&debug_menu), atomic_load(&debug_selected),
        atomic_load(&debug_reboot_error), atomic_load(&debug_recording),
        atomic_load(&debug_draw_start), atomic_load(&debug_draw_step),
        atomic_load(&debug_gpu_aa), atomic_load(&debug_fps_stats),
        atomic_load(&debug_vsync), atomic_load(&debug_cap),
        { atomic_load(&debug_oc[0]), atomic_load(&debug_oc[1]), atomic_load(&debug_oc[2]) },
        { atomic_load(&debug_oc_now[0]), atomic_load(&debug_oc_now[1]), atomic_load(&debug_oc_now[2]) },
        atomic_load(&debug_extra_repeat)
    };
    return s;
}

int switch_debug_frame_cap(void)
{
    switch_debug_init();
    return atomic_load(&debug_cap);
}

int switch_debug_extra_repeat(void)
{
    return atomic_load(&debug_extra_repeat);
}

int switch_debug_input(uint64_t buttons)
{
    switch_debug_init();
    const u64 combo = HidNpadButton_L | HidNpadButton_StickL | HidNpadButton_StickR;
    const int combo_down = (buttons & combo) == combo;
    const int combo_old = (debug_old_buttons & combo) == combo;
    const u64 pressed = buttons & ~debug_old_buttons;
    debug_old_buttons = buttons;
    oc_maintain(0);
    if (combo_down && !combo_old) {
        atomic_store(&debug_menu, !atomic_load(&debug_menu));
        debug_suppress_until_release = 1;
    }
    if (atomic_load(&debug_menu)) {
        int row = atomic_load(&debug_selected);
        if (pressed & HidNpadButton_Up) row = (row + DBG_ROW_COUNT - 1) % DBG_ROW_COUNT;
        if (pressed & HidNpadButton_Down) row = (row + 1) % DBG_ROW_COUNT;
        atomic_store(&debug_selected, row);
        if (pressed & HidNpadButton_B) {
            atomic_store(&debug_menu, 0);
            debug_suppress_until_release = 1;
        } else if (pressed & (HidNpadButton_Left | HidNpadButton_Right |
                              HidNpadButton_A | HidNpadButton_ZL | HidNpadButton_ZR)) {
            int delta = (pressed & HidNpadButton_Left) ? -1 : 1;
            if (row == DBG_ROW_SCALE) {
                int scale = atomic_load(&debug_scale) + delta;
                if (scale >= 0 && scale <= 10) atomic_store(&debug_scale, scale);
                switch_debug_save();
            } else if (row == DBG_ROW_FXAA) {
                atomic_store(&debug_gpu_aa, !atomic_load(&debug_gpu_aa));
                switch_debug_save();
            } else if (row == DBG_ROW_VSYNC) {
                atomic_store(&debug_vsync, !atomic_load(&debug_vsync));
                switch_debug_save();
            } else if (row == DBG_ROW_CAP) {
                static const int caps[3] = { 60, 30, 0 };
                int i = 0;
                while (i < 2 && caps[i] != atomic_load(&debug_cap)) i++;
                atomic_store(&debug_cap, caps[(i + 3 + delta) % 3]);
                switch_debug_save();
            } else if (row == DBG_ROW_FPS) {
                atomic_store(&debug_fps, !atomic_load(&debug_fps));
                switch_debug_save();
            } else if (row == DBG_ROW_FRAME_MS) {
                atomic_store(&debug_frame_ms, !atomic_load(&debug_frame_ms));
                switch_debug_save();
            } else if (row == DBG_ROW_FPS_STATS) {
                atomic_store(&debug_fps_stats, !atomic_load(&debug_fps_stats));
                switch_debug_save();
            } else if (row == DBG_ROW_FPS_RESET && (pressed & HidNpadButton_A)) {
                atomic_store(&debug_fps_reset, 1);
            } else if (row == DBG_ROW_EXTRA) {
                /* OFF, ALL, then each draw task active in the scene. */
                const uint32_t cur = (uint32_t)atomic_load(&debug_extra_repeat);
                const uint32_t next = dbz3_bisect_next ? dbz3_bisect_next(cur, delta)
                                                       : (cur ? 0u : 1u);
                atomic_store(&debug_extra_repeat, (int)next);
            } else if (row >= DBG_ROW_OC_CPU && row <= DBG_ROW_OC_RAM) {
                const int d = row - DBG_ROW_OC_CPU, n = oc_choice_count[d];
                int i = 0;
                while (i < n - 1 && oc_choices[d][i] != atomic_load(&debug_oc[d])) i++;
                atomic_store(&debug_oc[d], oc_choices[d][(i + n + delta) % n]);
                switch_debug_save();
                oc_maintain(1);
            } else if (row == DBG_ROW_SCREENSHOT && (pressed & HidNpadButton_A)) {
                atomic_store(&debug_screenshot, 1);
                atomic_store(&debug_menu, 0);
                debug_suppress_until_release = 1;
            } else if (row == DBG_ROW_RECORD && (pressed & HidNpadButton_A)) {
                atomic_store(&debug_recording, !atomic_load(&debug_recording));
                atomic_store(&debug_menu, 0);
                debug_suppress_until_release = 1;
            } else if (row == DBG_ROW_DRAW_START) {
                if (pressed & HidNpadButton_ZL) delta = -1;
                int amount = (pressed & (HidNpadButton_ZL | HidNpadButton_ZR))
                    ? 128 : atomic_load(&debug_draw_step);
                int start = atomic_load(&debug_draw_start) + delta * amount;
                if (start >= 0 && start <= 8192) atomic_store(&debug_draw_start, start);
            } else if (row == DBG_ROW_DRAW_STEP) {
                atomic_store(&debug_draw_step, atomic_load(&debug_draw_step) == 8 ? 1 : 8);
            } else if (row == DBG_ROW_DRAW_CAPTURE && (pressed & HidNpadButton_A)) {
                atomic_store(&debug_draw_capture, 1);
                atomic_store(&debug_menu, 0);
                debug_suppress_until_release = 1;
            } else if (row == DBG_ROW_REBOOT && (pressed & HidNpadButton_A)) {
                atomic_store(&debug_reboot, 1);
            } else if (row == DBG_ROW_CLOSE && (pressed & HidNpadButton_A)) {
                atomic_store(&debug_menu, 0);
                debug_suppress_until_release = 1;
            }
        }
        return 1;
    }
    if (debug_suppress_until_release) {
        if (!buttons) debug_suppress_until_release = 0;
        return 1;
    }
    return 0;
}

int switch_debug_take_reboot(void) { return atomic_exchange(&debug_reboot, 0); }
void switch_debug_reboot_failed(int result) { atomic_store(&debug_reboot_error, result); }
int switch_debug_take_screenshot(void) { return atomic_exchange(&debug_screenshot, 0); }
int switch_debug_take_fps_reset(void) { return atomic_exchange(&debug_fps_reset, 0); }
int switch_debug_recording(void) { return atomic_load(&debug_recording); }
void switch_debug_stop_recording(void) { atomic_store(&debug_recording, 0); }
int switch_debug_take_draw_capture(int *start, int *step)
{
    if (!atomic_exchange(&debug_draw_capture, 0)) return 0;
    if (start) *start = atomic_load(&debug_draw_start);
    if (step) *step = atomic_load(&debug_draw_step);
    return 1;
}

static void switch_input_read_native(Ps3PlatformPadState pads[PS3_PLATFORM_PAD_COUNT])
{
    memset(pads, 0, sizeof(Ps3PlatformPadState) * PS3_PLATFORM_PAD_COUNT);
    for (unsigned i = 0; i < PS3_PLATFORM_PAD_COUNT; ++i) {
        PadState *native = &controllers[i];
        padUpdate(native);
        if (!padIsConnected(native)) continue;
        Ps3PlatformPadState *out = &pads[i];
        out->connected = 1;
        HidAnalogStickState left = padGetStickPos(native, 0);
        HidAnalogStickState right = padGetStickPos(native, 1);
        out->left_x = left.x == INT16_MIN ? -32767 : left.x;
        out->left_y = left.y == INT16_MIN ? -32767 : left.y;
        out->right_x = right.x == INT16_MIN ? -32767 : right.x;
        out->right_y = right.y == INT16_MIN ? -32767 : right.y;
        u64 buttons = padGetButtons(native);
        if ((i == 0 && switch_debug_input(buttons)) ||
            (i != 0 && atomic_load(&debug_menu))) continue;
        out->left_trigger = (buttons & HidNpadButton_ZL) ? 255 : 0;
        out->right_trigger = (buttons & HidNpadButton_ZR) ? 255 : 0;
        if (buttons & HidNpadButton_Minus) out->buttons |= PS3_PLATFORM_PAD_SELECT;
        if (buttons & HidNpadButton_Plus) out->buttons |= PS3_PLATFORM_PAD_START;
        if (buttons & HidNpadButton_Up) out->buttons |= PS3_PLATFORM_PAD_DPAD_UP;
        if (buttons & HidNpadButton_Right) out->buttons |= PS3_PLATFORM_PAD_DPAD_RIGHT;
        if (buttons & HidNpadButton_Down) out->buttons |= PS3_PLATFORM_PAD_DPAD_DOWN;
        if (buttons & HidNpadButton_Left) out->buttons |= PS3_PLATFORM_PAD_DPAD_LEFT;
        /* Positional, like the PS3 pad: B (bottom) is Cross, A (right) is
         * Circle, Y (left) is Square, X (top) is Triangle. */
        if (buttons & HidNpadButton_B) out->buttons |= PS3_PLATFORM_PAD_FACE_SOUTH;
        if (buttons & HidNpadButton_A) out->buttons |= PS3_PLATFORM_PAD_FACE_EAST;
        if (buttons & HidNpadButton_Y) out->buttons |= PS3_PLATFORM_PAD_FACE_WEST;
        if (buttons & HidNpadButton_X) out->buttons |= PS3_PLATFORM_PAD_FACE_NORTH;
        if (buttons & HidNpadButton_L) out->buttons |= PS3_PLATFORM_PAD_L1;
        if (buttons & HidNpadButton_R) out->buttons |= PS3_PLATFORM_PAD_R1;
        if (buttons & HidNpadButton_StickL) out->buttons |= PS3_PLATFORM_PAD_L3;
        if (buttons & HidNpadButton_StickR) out->buttons |= PS3_PLATFORM_PAD_R3;
    }
}

static bool buttons_changed(const Ps3PlatformPadState *a,
                            const Ps3PlatformPadState *b)
{
    for (unsigned i = 0; i < PS3_PLATFORM_PAD_COUNT; ++i) {
        if (a[i].connected != b[i].connected || a[i].buttons != b[i].buttons ||
            a[i].left_trigger != b[i].left_trigger ||
            a[i].right_trigger != b[i].right_trigger)
            return true;
    }
    return false;
}

static void switch_input_sampler(void *arg)
{
    (void)arg;
    for (;;) {
        Ps3PlatformPadState sample[PS3_PLATFORM_PAD_COUNT];
        switch_input_read_native(sample);
        mutexLock(&sample_lock);
        if (atomic_load(&debug_menu)) {
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
            input_queue_stats.menu_cleared += sample_count;
#endif
            sample_count = 0; sample_head = 0;
        }
        if (buttons_changed(sample, latest_sample)) {
            if (sample_count == INPUT_QUEUE_CAP) {
                sample_head = (sample_head + 1u) % INPUT_QUEUE_CAP;
                --sample_count;
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
                ++input_queue_stats.overflow;
#endif
            }
            unsigned tail = (sample_head + sample_count) % INPUT_QUEUE_CAP;
            memcpy(sample_queue[tail], sample, sizeof(sample));
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
            sample_queue_tick[tail] = armGetSystemTick();
            ++input_queue_stats.enqueued;
#endif
            ++sample_count;
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
            if (sample_count > input_queue_stats.depth_max)
                input_queue_stats.depth_max = sample_count;
#endif
        }
        memcpy(latest_sample, sample, sizeof(sample));
        mutexUnlock(&sample_lock);
        svcSleepThread(4000000);
    }
}

static Ps3PlatformStatus switch_input_poll(void *context,
    Ps3PlatformPadState pads[PS3_PLATFORM_PAD_COUNT])
{
    (void)context;
    if (!pads) return PS3_PLATFORM_INVALID_ARGUMENT;
    if (!configured) {
        padConfigureInput(PS3_PLATFORM_PAD_COUNT, HidNpadStyleSet_NpadStandard);
        padInitialize(&controllers[0], HidNpadIdType_No1,
                      HidNpadIdType_Handheld);
        padInitialize(&controllers[1], HidNpadIdType_No2);
        padInitialize(&controllers[2], HidNpadIdType_No3);
        padInitialize(&controllers[3], HidNpadIdType_No4);
        mutexInit(&sample_lock);
        switch_input_read_native(latest_sample);
        Result rc = threadCreate(&sample_thread, switch_input_sampler, NULL,
                                 NULL, 0x8000, 0x2C, -2);
        if (R_SUCCEEDED(rc)) {
            rc = threadStart(&sample_thread);
            if (R_SUCCEEDED(rc)) sampler_running = true;
            else threadClose(&sample_thread);
        }
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
        input_queue_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t21-input-queue.log", "w");
        if (input_queue_log) {
            fprintf(input_queue_log,
                    "profile=input_queue window=5s sampler=%s age=sample_to_host_poll "
                    "polls=host_backend_polls thresholds_ms=50/100/250\n",
                    sampler_running ? "running" : "failed");
            fflush(input_queue_log);
        }
        input_queue_window_tick = armGetSystemTick();
#endif
        configured = true;
    }
    if (switch_debug_take_reboot()) {
        Result rc = appletRequestLaunchApplication(0, NULL);
        if (R_FAILED(rc)) switch_debug_reboot_failed((int)rc);
    }
    if (sampler_running) {
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
        u64 now;
        u64 window_us = 0, oldest_us = 0;
        unsigned depth = 0;
        InputQueueStats report;
        int report_ready = 0;
#endif
        mutexLock(&sample_lock);
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
        now = armGetSystemTick();
        ++input_queue_stats.polls;
        if (input_queue_last_poll_tick) {
            u64 gap_us = input_queue_elapsed_us(input_queue_last_poll_tick, now);
            if (gap_us > input_queue_stats.poll_gap_max_us)
                input_queue_stats.poll_gap_max_us = gap_us;
        }
        input_queue_last_poll_tick = now;
#endif
        if (sample_count) {
            memcpy(pads, sample_queue[sample_head], sizeof(sample_queue[0]));
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
            u64 age_us = input_queue_elapsed_us(sample_queue_tick[sample_head], now);
            ++input_queue_stats.delivered;
            input_queue_stats.age_total_us += age_us;
            if (age_us > input_queue_stats.age_max_us) input_queue_stats.age_max_us = age_us;
            if (age_us >= 50000) ++input_queue_stats.age_ge_50ms;
            if (age_us >= 100000) ++input_queue_stats.age_ge_100ms;
            if (age_us >= 250000) ++input_queue_stats.age_ge_250ms;
#endif
            sample_head = (sample_head + 1u) % INPUT_QUEUE_CAP;
            --sample_count;
        } else {
            memcpy(pads, latest_sample, sizeof(latest_sample));
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
            ++input_queue_stats.empty;
#endif
        }
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
        if (input_queue_elapsed_us(input_queue_window_tick, now) >= 5000000) {
            window_us = input_queue_elapsed_us(input_queue_window_tick, now);
            report = input_queue_stats;
            depth = sample_count;
            if (depth) oldest_us = input_queue_elapsed_us(sample_queue_tick[sample_head], now);
            memset(&input_queue_stats, 0, sizeof(input_queue_stats));
            input_queue_stats.depth_max = depth;
            input_queue_window_tick = now;
            report_ready = 1;
        }
#endif
        mutexUnlock(&sample_lock);
#ifdef PORT_SWITCH_INPUT_QUEUE_PROFILE
        if (report_ready && input_queue_log) {
            fprintf(input_queue_log,
                    "window_ms=%llu polls=%llu poll_gap_max_ms=%.1f enqueued=%llu "
                    "delivered=%llu empty=%llu depth_now=%u depth_max=%u oldest_now_ms=%.1f "
                    "overflow=%llu menu_cleared=%llu age_avg_ms=%.1f age_max_ms=%.1f "
                    "age_ge_50ms=%llu age_ge_100ms=%llu age_ge_250ms=%llu\n",
                    (unsigned long long)(window_us / 1000),
                    (unsigned long long)report.polls, report.poll_gap_max_us / 1000.0,
                    (unsigned long long)report.enqueued, (unsigned long long)report.delivered,
                    (unsigned long long)report.empty, depth, report.depth_max, oldest_us / 1000.0,
                    (unsigned long long)report.overflow,
                    (unsigned long long)report.menu_cleared,
                    report.delivered ? (double)report.age_total_us / report.delivered / 1000.0 : 0.0,
                    report.age_max_us / 1000.0,
                    (unsigned long long)report.age_ge_50ms,
                    (unsigned long long)report.age_ge_100ms,
                    (unsigned long long)report.age_ge_250ms);
            fflush(input_queue_log);
        }
#endif
    } else {
        /* Keep the old synchronous path if the sampling thread cannot start. */
        switch_input_read_native(pads);
    }
    return PS3_PLATFORM_OK;
}

const Ps3PlatformInput *ps3_platform_switch_input(void)
{
    static const Ps3PlatformInput ops = {
        .context = NULL,
        .poll = switch_input_poll,
    };
    return &ops;
}
