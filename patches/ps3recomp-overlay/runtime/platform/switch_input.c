#include "switch_input.h"

#include <switch.h>
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
        if (buttons_changed(sample, latest_sample)) {
            if (sample_count == INPUT_QUEUE_CAP) {
                sample_head = (sample_head + 1u) % INPUT_QUEUE_CAP;
                --sample_count;
            }
            unsigned tail = (sample_head + sample_count) % INPUT_QUEUE_CAP;
            memcpy(sample_queue[tail], sample, sizeof(sample));
            ++sample_count;
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
        configured = true;
    }
    if (sampler_running) {
        mutexLock(&sample_lock);
        if (sample_count) {
            memcpy(pads, sample_queue[sample_head], sizeof(sample_queue[0]));
            sample_head = (sample_head + 1u) % INPUT_QUEUE_CAP;
            --sample_count;
        } else {
            memcpy(pads, latest_sample, sizeof(latest_sample));
        }
        mutexUnlock(&sample_lock);
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
