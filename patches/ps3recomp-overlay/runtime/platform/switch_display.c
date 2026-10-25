#include "switch_display.h"

#include <switch.h>
#include <string.h>

#include "switch_time.h"

typedef struct SwitchDisplay {
    Framebuffer native;
    uint32_t width, height;
    uint32_t acquired;
    uint32_t opened;
    uint64_t next_present_us;
} SwitchDisplay;

static SwitchDisplay display;

static Ps3PlatformStatus switch_display_open(void *context, uint32_t width,
                                              uint32_t height, const char *title)
{
    (void)context;
    (void)title;
    if (!width || !height || width > 4096 || height > 4096)
        return PS3_PLATFORM_INVALID_ARGUMENT;
    if (display.opened) return PS3_PLATFORM_BUSY;
    memset(&display, 0, sizeof(display));
    Result rc = framebufferCreate(&display.native, nwindowGetDefault(),
                                   width, height, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) return PS3_PLATFORM_IO_ERROR;
    rc = framebufferMakeLinear(&display.native);
    if (R_FAILED(rc)) {
        framebufferClose(&display.native);
        memset(&display, 0, sizeof(display));
        return PS3_PLATFORM_OUT_OF_MEMORY;
    }
    display.width = width;
    display.height = height;
    display.opened = 1;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_display_acquire(void *context,
                                                Ps3PlatformFrame *out_frame)
{
    (void)context;
    if (!out_frame) return PS3_PLATFORM_INVALID_ARGUMENT;
    if (!display.opened) return PS3_PLATFORM_IO_ERROR;
    if (display.acquired) return PS3_PLATFORM_BUSY;
    u32 stride = 0;
    void *pixels = framebufferBegin(&display.native, &stride);
    if (!pixels || stride < display.width * 4) return PS3_PLATFORM_IO_ERROR;
    display.acquired = 1;
    *out_frame = (Ps3PlatformFrame) {
        .pixels = pixels, .width = display.width, .height = display.height,
        .pitch_bytes = stride, .format = PS3_PLATFORM_PIXEL_RGBA8,
        .backend_token = &display,
    };
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_display_present(void *context,
    Ps3PlatformFrame *frame, uint32_t wait_vblank)
{
    (void)context;
    if (!frame || frame->backend_token != &display || !display.acquired ||
        !frame->pixels) return PS3_PLATFORM_INVALID_ARGUMENT;
    framebufferEnd(&display.native);
    display.acquired = 0;
    frame->pixels = NULL;
    frame->backend_token = NULL;
    if (wait_vblank) {
        const Ps3PlatformTime *time = ps3_platform_switch_time();
        uint64_t now = time->monotonic_us(time->context);
        if (display.next_present_us > now &&
            display.next_present_us - now < 50000)
            time->sleep_us(time->context, display.next_present_us - now);
        now = time->monotonic_us(time->context);
        display.next_present_us = now + 16667;
    }
    return PS3_PLATFORM_OK;
}

static void switch_display_pump(void *context)
{ (void)context; }

static void switch_display_close(void *context)
{
    (void)context;
    if (!display.opened) return;
    if (display.acquired) framebufferEnd(&display.native);
    framebufferClose(&display.native);
    memset(&display, 0, sizeof(display));
}

const Ps3PlatformDisplay *ps3_platform_switch_display(void)
{
    static const Ps3PlatformDisplay ops = {
        .context = NULL,
        .open = switch_display_open,
        .acquire_frame = switch_display_acquire,
        .present = switch_display_present,
        .pump_events = switch_display_pump,
        .close = switch_display_close,
    };
    return &ops;
}
