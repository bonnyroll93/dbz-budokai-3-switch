/*
 * ps3recomp platform boundary.
 *
 * This header deliberately exposes no Win32, POSIX, SDL, D3D, or libnx types.
 * Backends implement these tables; the runtime owns PS3 semantics and passes
 * host resources through this boundary. Paths are UTF-8. Opaque handles are
 * created and destroyed by the backend that receives them.
 */
#ifndef PS3EMU_PLATFORM_H
#define PS3EMU_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS3_PLATFORM_ABI_VERSION 1u
#define PS3_PLATFORM_WAIT_FOREVER UINT64_MAX
#define PS3_PLATFORM_MAX_PATH 1024u
#define PS3_PLATFORM_MAX_DIRENT_NAME 256u
#define PS3_PLATFORM_PAD_COUNT 4u

typedef enum Ps3PlatformStatus {
    PS3_PLATFORM_OK = 0,
    PS3_PLATFORM_TIMEOUT = 1,
    PS3_PLATFORM_END_OF_DIRECTORY = 2,
    PS3_PLATFORM_UNSUPPORTED = -1,
    PS3_PLATFORM_INVALID_ARGUMENT = -2,
    PS3_PLATFORM_OUT_OF_MEMORY = -3,
    PS3_PLATFORM_IO_ERROR = -4,
    PS3_PLATFORM_BUSY = -5
} Ps3PlatformStatus;

typedef enum Ps3PlatformMemoryProtection {
    PS3_PLATFORM_MEM_READ = 1u << 0,
    PS3_PLATFORM_MEM_WRITE = 1u << 1,
    PS3_PLATFORM_MEM_EXECUTE = 1u << 2
} Ps3PlatformMemoryProtection;

typedef struct Ps3PlatformMemory {
    void *context;
    Ps3PlatformStatus (*reserve)(void *context, uint64_t size,
                                 uint64_t alignment, void **out_base);
    Ps3PlatformStatus (*commit)(void *context, void *base, uint64_t offset,
                                uint64_t size, uint32_t protection);
    Ps3PlatformStatus (*protect)(void *context, void *base, uint64_t offset,
                                 uint64_t size, uint32_t protection);
    void (*release)(void *context, void *base, uint64_t size);
} Ps3PlatformMemory;

typedef void (*Ps3PlatformThreadEntry)(void *argument);
typedef struct Ps3PlatformThreadOps {
    void *context;
    Ps3PlatformStatus (*create)(void *context, void **out_thread,
                                Ps3PlatformThreadEntry entry, void *argument,
                                uint64_t stack_size, int32_t priority);
    Ps3PlatformStatus (*join)(void *context, void *thread,
                              uint64_t timeout_us);
    void (*detach)(void *context, void *thread);
    void (*yield)(void *context);
    Ps3PlatformStatus (*mutex_create)(void *context, void **out_mutex);
    void (*mutex_destroy)(void *context, void *mutex);
    Ps3PlatformStatus (*mutex_lock)(void *context, void *mutex);
    void (*mutex_unlock)(void *context, void *mutex);
    Ps3PlatformStatus (*condition_create)(void *context, void **out_condition);
    void (*condition_destroy)(void *context, void *condition);
    Ps3PlatformStatus (*condition_wait)(void *context, void *condition,
                                        void *mutex, uint64_t timeout_us);
    void (*condition_signal)(void *context, void *condition);
    void (*condition_broadcast)(void *context, void *condition);
} Ps3PlatformThreadOps;

typedef struct Ps3PlatformTime {
    void *context;
    uint64_t (*monotonic_us)(void *context);
    void (*sleep_us)(void *context, uint64_t duration_us);
} Ps3PlatformTime;

typedef enum Ps3PlatformOpenFlags {
    PS3_PLATFORM_FILE_READ = 1u << 0,
    PS3_PLATFORM_FILE_WRITE = 1u << 1,
    PS3_PLATFORM_FILE_CREATE = 1u << 2,
    PS3_PLATFORM_FILE_TRUNCATE = 1u << 3,
    PS3_PLATFORM_FILE_APPEND = 1u << 4
} Ps3PlatformOpenFlags;

typedef enum Ps3PlatformSeekOrigin {
    PS3_PLATFORM_SEEK_BEGIN = 0,
    PS3_PLATFORM_SEEK_CURRENT = 1,
    PS3_PLATFORM_SEEK_END = 2
} Ps3PlatformSeekOrigin;

typedef enum Ps3PlatformFileType {
    PS3_PLATFORM_FILE_TYPE_UNKNOWN = 0,
    PS3_PLATFORM_FILE_TYPE_REGULAR = 1,
    PS3_PLATFORM_FILE_TYPE_DIRECTORY = 2
} Ps3PlatformFileType;

typedef struct Ps3PlatformFileInfo {
    uint64_t size;
    uint64_t modified_time_us;
    Ps3PlatformFileType type;
} Ps3PlatformFileInfo;

typedef struct Ps3PlatformDirEntry {
    char name[PS3_PLATFORM_MAX_DIRENT_NAME];
    Ps3PlatformFileInfo info;
} Ps3PlatformDirEntry;

typedef struct Ps3PlatformFileOps {
    void *context;
    Ps3PlatformStatus (*open)(void *context, const char *utf8_path,
                              uint32_t flags, void **out_file);
    Ps3PlatformStatus (*read)(void *context, void *file, void *buffer,
                              uint64_t bytes, uint64_t *out_bytes_read);
    Ps3PlatformStatus (*write)(void *context, void *file, const void *buffer,
                               uint64_t bytes, uint64_t *out_bytes_written);
    Ps3PlatformStatus (*seek)(void *context, void *file, int64_t offset,
                              int32_t origin, uint64_t *out_position);
    Ps3PlatformStatus (*stat)(void *context, const char *utf8_path,
                              Ps3PlatformFileInfo *out_info);
    Ps3PlatformStatus (*mkdir)(void *context, const char *utf8_path);
    Ps3PlatformStatus (*close)(void *context, void *file);
    Ps3PlatformStatus (*dir_open)(void *context, const char *utf8_path,
                                  void **out_directory);
    Ps3PlatformStatus (*dir_read)(void *context, void *directory,
                                  Ps3PlatformDirEntry *out_entry);
    void (*dir_close)(void *context, void *directory);
} Ps3PlatformFileOps;

typedef enum Ps3PlatformLogLevel {
    PS3_PLATFORM_LOG_TRACE = 0,
    PS3_PLATFORM_LOG_DEBUG = 1,
    PS3_PLATFORM_LOG_INFO = 2,
    PS3_PLATFORM_LOG_WARN = 3,
    PS3_PLATFORM_LOG_ERROR = 4,
    PS3_PLATFORM_LOG_FATAL = 5
} Ps3PlatformLogLevel;

typedef struct Ps3PlatformLog {
    void *context;
    void (*write)(void *context, Ps3PlatformLogLevel level,
                  const char *category, const char *message, size_t length);
} Ps3PlatformLog;

typedef enum Ps3PlatformPixelFormat {
    PS3_PLATFORM_PIXEL_RGBA8 = 1,
    PS3_PLATFORM_PIXEL_BGRA8 = 2
} Ps3PlatformPixelFormat;

typedef struct Ps3PlatformFrame {
    void *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes;
    Ps3PlatformPixelFormat format;
    void *backend_token;
} Ps3PlatformFrame;

typedef struct Ps3PlatformDisplay {
    void *context;
    Ps3PlatformStatus (*open)(void *context, uint32_t width, uint32_t height,
                              const char *title);
    Ps3PlatformStatus (*acquire_frame)(void *context,
                                       Ps3PlatformFrame *out_frame);
    Ps3PlatformStatus (*present)(void *context, Ps3PlatformFrame *frame,
                                 uint32_t wait_vblank);
    void (*pump_events)(void *context);
    void (*close)(void *context);
} Ps3PlatformDisplay;

typedef struct Ps3PlatformPadState {
    uint32_t connected;
    uint32_t buttons;
    int16_t left_x, left_y;
    int16_t right_x, right_y;
    uint8_t left_trigger, right_trigger;
    uint8_t reserved[2];
} Ps3PlatformPadState;

typedef enum Ps3PlatformPadButton {
    PS3_PLATFORM_PAD_SELECT = 1u << 0,
    PS3_PLATFORM_PAD_START = 1u << 1,
    PS3_PLATFORM_PAD_DPAD_UP = 1u << 2,
    PS3_PLATFORM_PAD_DPAD_RIGHT = 1u << 3,
    PS3_PLATFORM_PAD_DPAD_DOWN = 1u << 4,
    PS3_PLATFORM_PAD_DPAD_LEFT = 1u << 5,
    PS3_PLATFORM_PAD_FACE_SOUTH = 1u << 6,
    PS3_PLATFORM_PAD_FACE_EAST = 1u << 7,
    PS3_PLATFORM_PAD_FACE_WEST = 1u << 8,
    PS3_PLATFORM_PAD_FACE_NORTH = 1u << 9,
    PS3_PLATFORM_PAD_L1 = 1u << 10,
    PS3_PLATFORM_PAD_R1 = 1u << 11,
    PS3_PLATFORM_PAD_L3 = 1u << 12,
    PS3_PLATFORM_PAD_R3 = 1u << 13
} Ps3PlatformPadButton;

typedef struct Ps3PlatformInput {
    void *context;
    Ps3PlatformStatus (*poll)(void *context,
                              Ps3PlatformPadState pads[PS3_PLATFORM_PAD_COUNT]);
} Ps3PlatformInput;

typedef enum Ps3PlatformSampleFormat {
    PS3_PLATFORM_SAMPLE_S16 = 1,
    PS3_PLATFORM_SAMPLE_F32 = 2
} Ps3PlatformSampleFormat;

typedef struct Ps3PlatformAudioConfig {
    uint32_t sample_rate;
    uint16_t channels;
    Ps3PlatformSampleFormat format;
    uint32_t frames_per_buffer;
} Ps3PlatformAudioConfig;

typedef struct Ps3PlatformAudio {
    void *context;
    Ps3PlatformStatus (*open)(void *context,
                              const Ps3PlatformAudioConfig *config,
                              void **out_stream);
    Ps3PlatformStatus (*submit)(void *context, void *stream,
                                const void *interleaved_frames,
                                uint32_t frame_count,
                                uint64_t timeout_us);
    Ps3PlatformStatus (*pause)(void *context, void *stream, uint32_t paused);
    Ps3PlatformStatus (*drain)(void *context, void *stream,
                               uint64_t timeout_us);
    void (*close)(void *context, void *stream);
} Ps3PlatformAudio;

typedef struct Ps3Platform {
    uint32_t abi_version;
    uint32_t struct_size;
    Ps3PlatformMemory memory;
    Ps3PlatformThreadOps thread;
    Ps3PlatformTime time;
    Ps3PlatformFileOps file;
    Ps3PlatformLog log;
    Ps3PlatformDisplay display;
    Ps3PlatformInput input;
    Ps3PlatformAudio audio;
} Ps3Platform;

/* Checks only ABI compatibility. Service completeness is backend-dependent;
 * callers must handle PS3_PLATFORM_UNSUPPORTED for optional operations. */
static inline Ps3PlatformStatus ps3_platform_validate(const Ps3Platform *platform)
{
    if (!platform || platform->abi_version != PS3_PLATFORM_ABI_VERSION ||
        platform->struct_size < offsetof(Ps3Platform, audio) +
                                sizeof(platform->audio)) {
        return PS3_PLATFORM_INVALID_ARGUMENT;
    }
    return PS3_PLATFORM_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* PS3EMU_PLATFORM_H */
