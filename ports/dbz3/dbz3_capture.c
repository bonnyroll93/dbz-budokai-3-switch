/* Debug captures from the final RSX colour surface. The renderer calls this
 * only when the menu requests a still or a low-rate recording frame. */
#include "switch_debug.h"

#include <stdio.h>
#include <stddef.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#define CAPTURE_DIR "sdmc:/switch/dbz-budokai-3-switch/captures"
#define VIDEO_INTERVAL_US 200000ull
#define VIDEO_MAX_FRAMES 150u

typedef struct CaptureJpegError {
    struct jpeg_error_mgr base;
    jmp_buf jump;
} CaptureJpegError;

static FILE *video_file;
static FILE *video_times;
static uint64_t video_start_us;
static uint64_t last_video_us;
static unsigned video_frames;
static unsigned file_serial;
static int pending_still;
static int pending_video;

static void capture_jpeg_error(j_common_ptr info)
{
    CaptureJpegError *err = (CaptureJpegError *)info->err;
    longjmp(err->jump, 1);
}

static int capture_write_jpeg(FILE *file, const uint8_t *rgba,
                              unsigned source_w, unsigned source_h,
                              unsigned pitch, unsigned out_w, unsigned out_h,
                              int quality)
{
    struct jpeg_compress_struct jpg;
    CaptureJpegError err;
    uint8_t *row = malloc((size_t)out_w * 3u);
    if (!row) return 0;
    jpg.err = jpeg_std_error(&err.base);
    err.base.error_exit = capture_jpeg_error;
    if (setjmp(err.jump)) {
        jpeg_destroy_compress(&jpg);
        free(row);
        return 0;
    }
    jpeg_create_compress(&jpg);
    jpeg_stdio_dest(&jpg, file);
    jpg.image_width = out_w;
    jpg.image_height = out_h;
    jpg.input_components = 3;
    jpg.in_color_space = JCS_RGB;
    jpeg_set_defaults(&jpg);
    jpeg_set_quality(&jpg, quality, TRUE);
    jpeg_start_compress(&jpg, TRUE);
    while (jpg.next_scanline < out_h) {
        const unsigned sy = (unsigned)((uint64_t)jpg.next_scanline * source_h / out_h);
        const uint8_t *src = rgba + (size_t)sy * pitch;
        for (unsigned x = 0; x < out_w; ++x) {
            const unsigned sx = (unsigned)((uint64_t)x * source_w / out_w);
            row[x * 3u + 0u] = src[sx * 4u + 0u];
            row[x * 3u + 1u] = src[sx * 4u + 1u];
            row[x * 3u + 2u] = src[sx * 4u + 2u];
        }
        JSAMPROW scanline = row;
        jpeg_write_scanlines(&jpg, &scanline, 1);
    }
    jpeg_finish_compress(&jpg);
    jpeg_destroy_compress(&jpg);
    free(row);
    return fflush(file) == 0;
}

static void capture_stop_video(void)
{
    if (video_file) {
        fclose(video_file);
        video_file = NULL;
        if (video_times) { fclose(video_times); video_times = NULL; }
        fprintf(stderr, "[capture] recording stopped: %u frames\n", video_frames);
    }
    switch_debug_stop_recording();
}

static void capture_open_video(uint64_t now_us)
{
    if (mkdir(CAPTURE_DIR, 0777) != 0) {
        /* EEXIST is harmless; fopen below reports an actual directory error. */
    }
    const long stamp = (long)time(NULL);
    char path[192];
    const unsigned serial = ++file_serial;
    snprintf(path, sizeof path, CAPTURE_DIR "/clip_%ld_%03u.mjpg", stamp, serial);
    video_file = fopen(path, "wb");
    if (!video_file) {
        fprintf(stderr, "[capture] cannot create %s\n", path);
        switch_debug_stop_recording();
        return;
    }
    snprintf(path, sizeof path, CAPTURE_DIR "/clip_%ld_%03u.txt", stamp, serial);
    video_times = fopen(path, "w");
    if (video_times) fprintf(video_times, "MJPEG 640x360, no audio; frame\tseconds\n");
    video_start_us = now_us;
    last_video_us = 0;
    video_frames = 0;
    fprintf(stderr, "[capture] recording started: clip_%ld_%03u.mjpg\n", stamp, serial);
}

int dbz3_capture_wants_frame(uint64_t now_us)
{
    if (switch_debug_take_screenshot()) pending_still = 1;
    if (!switch_debug_recording() && video_file) capture_stop_video();
    if (switch_debug_recording() && !video_file) capture_open_video(now_us);
    if (video_file && (video_frames >= VIDEO_MAX_FRAMES ||
                       now_us - video_start_us >= 30000000ull))
        capture_stop_video();
    pending_video = video_file &&
        (!last_video_us || now_us - last_video_us >= VIDEO_INTERVAL_US);
    return pending_still || pending_video;
}

void dbz3_capture_frame(const uint8_t *rgba, unsigned width, unsigned height,
                        unsigned pitch, uint64_t now_us)
{
    if (!rgba || !width || !height || pitch < width * 4u) return;
    if (pending_still) {
        char path[192];
        const long stamp = (long)time(NULL);
        snprintf(path, sizeof path, CAPTURE_DIR "/shot_%ld_%03u.jpg",
                 stamp, ++file_serial);
        if (mkdir(CAPTURE_DIR, 0777) != 0) { /* Existing directory is fine. */ }
        FILE *file = fopen(path, "wb");
        if (file) {
            const int ok = capture_write_jpeg(file, rgba, width, height,
                                              pitch, width, height, 88);
            fclose(file);
            fprintf(stderr, "[capture] screenshot %s: %s\n", ok ? "saved" : "failed", path);
        } else fprintf(stderr, "[capture] cannot create %s\n", path);
        pending_still = 0;
    }
    if (pending_video && video_file) {
        if (!capture_write_jpeg(video_file, rgba, width, height,
                                pitch, 640u, 360u, 72)) {
            fprintf(stderr, "[capture] recording write failed\n");
            capture_stop_video();
        } else {
            if (video_times) {
                fprintf(video_times, "%u\t%.3f\n", video_frames,
                        (now_us - video_start_us) / 1000000.0);
                fflush(video_times);
            }
            ++video_frames;
            last_video_us = now_us;
        }
    }
    pending_video = 0;
}
