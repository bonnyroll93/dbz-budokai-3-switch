#include "switch_audio.h"

#include <switch.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_SLOTS 6   /* 6 x 256 frames = 32 ms of output queued */

typedef struct SwitchAudioSlot {
    AudioOutBuffer native;
    bool queued;
} SwitchAudioSlot;

typedef struct SwitchAudioStream {
    Ps3PlatformSampleFormat format;
    uint32_t frames_per_buffer;
    bool paused;
    SwitchAudioSlot slots[AUDIO_SLOTS];
} SwitchAudioStream;

static bool audio_open;

static uint64_t timeout_ns(uint64_t timeout_us)
{
    return timeout_us == PS3_PLATFORM_WAIT_FOREVER ? UINT64_MAX :
        timeout_us > UINT64_MAX / 1000 ? UINT64_MAX - 1 : timeout_us * 1000;
}

static Ps3PlatformStatus audio_reap_one(SwitchAudioStream *stream,
                                        uint64_t timeout_us)
{
    AudioOutBuffer *released = NULL;
    u32 released_count = 0;
    Result rc = audoutWaitPlayFinish(&released, &released_count,
                                     timeout_ns(timeout_us));
    if (R_FAILED(rc))
        return R_VALUE(rc) == KERNELRESULT(TimedOut) ? PS3_PLATFORM_TIMEOUT :
            PS3_PLATFORM_IO_ERROR;
    if (!released_count || !released) return PS3_PLATFORM_TIMEOUT;
    for (unsigned i = 0; i < AUDIO_SLOTS; ++i) {
        if (&stream->slots[i].native == released) {
            stream->slots[i].queued = false;
            return PS3_PLATFORM_OK;
        }
    }
    return PS3_PLATFORM_IO_ERROR;
}

static Ps3PlatformStatus switch_audio_open(void *context,
    const Ps3PlatformAudioConfig *config, void **out_stream)
{
    (void)context;
    if (!config || !out_stream || config->sample_rate != 48000 ||
        config->channels != 2 || !config->frames_per_buffer ||
        config->frames_per_buffer > 8192 ||
        (config->format != PS3_PLATFORM_SAMPLE_F32 &&
         config->format != PS3_PLATFORM_SAMPLE_S16))
        return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_stream = NULL;
    if (audio_open) return PS3_PLATFORM_BUSY;
    SwitchAudioStream *stream = (SwitchAudioStream *)calloc(1, sizeof(*stream));
    if (!stream) return PS3_PLATFORM_OUT_OF_MEMORY;
    stream->format = config->format;
    stream->frames_per_buffer = config->frames_per_buffer;
    size_t bytes = (size_t)config->frames_per_buffer * 2 * sizeof(int16_t);
    size_t aligned = (bytes + 0xfff) & ~(size_t)0xfff;
    for (unsigned i = 0; i < AUDIO_SLOTS; ++i) {
        stream->slots[i].native.buffer = memalign(0x1000, aligned);
        if (!stream->slots[i].native.buffer) {
            for (unsigned j = 0; j < i; ++j) free(stream->slots[j].native.buffer);
            free(stream);
            return PS3_PLATFORM_OUT_OF_MEMORY;
        }
        stream->slots[i].native.buffer_size = aligned;
    }
    Result rc = audoutInitialize();
    if (R_SUCCEEDED(rc) && (audoutGetSampleRate() != 48000 ||
        audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16)) {
        audoutExit();
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    if (R_SUCCEEDED(rc)) {
        rc = audoutStartAudioOut();
        if (R_FAILED(rc)) audoutExit();
    }
    if (R_FAILED(rc)) {
        for (unsigned i = 0; i < AUDIO_SLOTS; ++i) free(stream->slots[i].native.buffer);
        free(stream);
        return PS3_PLATFORM_IO_ERROR;
    }
    audio_open = true;
    *out_stream = stream;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_audio_submit(void *context, void *handle,
    const void *interleaved_frames, uint32_t frame_count, uint64_t timeout_us)
{
    (void)context;
    if (!handle || (frame_count && !interleaved_frames))
        return PS3_PLATFORM_INVALID_ARGUMENT;
    SwitchAudioStream *stream = (SwitchAudioStream *)handle;
    if (frame_count > stream->frames_per_buffer) return PS3_PLATFORM_INVALID_ARGUMENT;
    if (!frame_count) return PS3_PLATFORM_OK;
    if (stream->paused) return PS3_PLATFORM_BUSY;
    SwitchAudioSlot *slot = NULL;
    for (unsigned i = 0; i < AUDIO_SLOTS; ++i)
        if (!stream->slots[i].queued) { slot = &stream->slots[i]; break; }
    if (!slot) {
        Ps3PlatformStatus status = audio_reap_one(stream, timeout_us);
        if (status != PS3_PLATFORM_OK) return status;
        for (unsigned i = 0; i < AUDIO_SLOTS; ++i)
            if (!stream->slots[i].queued) { slot = &stream->slots[i]; break; }
    }
    if (!slot) return PS3_PLATFORM_BUSY;
    size_t samples = (size_t)frame_count * 2;
    int16_t *out = (int16_t *)slot->native.buffer;
    if (stream->format == PS3_PLATFORM_SAMPLE_S16) {
        memcpy(out, interleaved_frames, samples * sizeof(int16_t));
    } else {
        const float *input = (const float *)interleaved_frames;
        for (size_t i = 0; i < samples; ++i) {
            float value = input[i];
            if (value > 1.0f) value = 1.0f;
            else if (value < -1.0f) value = -1.0f;
            else if (value != value) value = 0.0f;
            out[i] = (int16_t)(value * 32767.0f);
        }
    }
    slot->native.data_size = samples * sizeof(int16_t);
    slot->native.data_offset = 0;
    Result rc = audoutAppendAudioOutBuffer(&slot->native);
    if (R_FAILED(rc)) return PS3_PLATFORM_IO_ERROR;
    slot->queued = true;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_audio_pause(void *context, void *handle,
                                             uint32_t paused)
{
    (void)context;
    if (!handle) return PS3_PLATFORM_INVALID_ARGUMENT;
    SwitchAudioStream *stream = (SwitchAudioStream *)handle;
    if (!!paused == stream->paused) return PS3_PLATFORM_OK;
    Result rc = paused ? audoutStopAudioOut() : audoutStartAudioOut();
    if (R_FAILED(rc)) return PS3_PLATFORM_IO_ERROR;
    stream->paused = !!paused;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_audio_drain(void *context, void *handle,
                                             uint64_t timeout_us)
{
    (void)context;
    if (!handle) return PS3_PLATFORM_INVALID_ARGUMENT;
    SwitchAudioStream *stream = (SwitchAudioStream *)handle;
    if (stream->paused) return PS3_PLATFORM_BUSY;
    for (;;) {
        unsigned pending = 0;
        for (unsigned i = 0; i < AUDIO_SLOTS; ++i)
            pending += stream->slots[i].queued;
        if (!pending) return PS3_PLATFORM_OK;
        Ps3PlatformStatus status = audio_reap_one(stream, timeout_us);
        if (status != PS3_PLATFORM_OK) return status;
    }
}

static void switch_audio_close(void *context, void *handle)
{
    (void)context;
    if (!handle) return;
    SwitchAudioStream *stream = (SwitchAudioStream *)handle;
    audoutStopAudioOut();
    audoutExit();
    for (unsigned i = 0; i < AUDIO_SLOTS; ++i)
        free(stream->slots[i].native.buffer);
    free(stream);
    audio_open = false;
}

const Ps3PlatformAudio *ps3_platform_switch_audio(void)
{
    static const Ps3PlatformAudio ops = {
        .context = NULL,
        .open = switch_audio_open,
        .submit = switch_audio_submit,
        .pause = switch_audio_pause,
        .drain = switch_audio_drain,
        .close = switch_audio_close,
    };
    return &ops;
}
