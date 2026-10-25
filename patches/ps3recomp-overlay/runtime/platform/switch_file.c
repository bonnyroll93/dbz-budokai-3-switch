#include "switch_file.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct SwitchFile {
    FILE *stream;
} SwitchFile;

typedef struct SwitchDirectory {
    DIR *native;
    char path[PS3_PLATFORM_MAX_PATH];
} SwitchDirectory;

static Ps3PlatformStatus file_error(int error)
{
    if (error == ENOMEM) return PS3_PLATFORM_OUT_OF_MEMORY;
    if (error == EEXIST || error == EBUSY) return PS3_PLATFORM_BUSY;
    return PS3_PLATFORM_IO_ERROR;
}

static void fill_info(const struct stat *source, Ps3PlatformFileInfo *out)
{
    out->size = source->st_size < 0 ? 0 : (uint64_t)source->st_size;
    out->modified_time_us = source->st_mtime < 0 ? 0 :
        (uint64_t)source->st_mtime * 1000000;
    out->type = S_ISDIR(source->st_mode) ? PS3_PLATFORM_FILE_TYPE_DIRECTORY :
        S_ISREG(source->st_mode) ? PS3_PLATFORM_FILE_TYPE_REGULAR :
        PS3_PLATFORM_FILE_TYPE_UNKNOWN;
}

static Ps3PlatformStatus switch_file_open(void *context, const char *path,
                                           uint32_t flags, void **out_file)
{
    (void)context;
    const uint32_t allowed = PS3_PLATFORM_FILE_READ | PS3_PLATFORM_FILE_WRITE |
        PS3_PLATFORM_FILE_CREATE | PS3_PLATFORM_FILE_TRUNCATE |
        PS3_PLATFORM_FILE_APPEND;
    if (!out_file || !path || !(flags & (PS3_PLATFORM_FILE_READ | PS3_PLATFORM_FILE_WRITE)) ||
        (flags & ~allowed) ||
        ((flags & (PS3_PLATFORM_FILE_CREATE | PS3_PLATFORM_FILE_TRUNCATE |
                   PS3_PLATFORM_FILE_APPEND)) && !(flags & PS3_PLATFORM_FILE_WRITE)))
        return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_file = NULL;
    int mode = (flags & PS3_PLATFORM_FILE_READ) ?
        ((flags & PS3_PLATFORM_FILE_WRITE) ? O_RDWR : O_RDONLY) : O_WRONLY;
    if (flags & PS3_PLATFORM_FILE_CREATE) mode |= O_CREAT;
    if (flags & PS3_PLATFORM_FILE_TRUNCATE) mode |= O_TRUNC;
    if (flags & PS3_PLATFORM_FILE_APPEND) mode |= O_APPEND;
    int fd = open(path, mode, 0666);
    if (fd < 0) return file_error(errno);
    const char *stdio_mode = (flags & PS3_PLATFORM_FILE_READ) ?
        ((flags & PS3_PLATFORM_FILE_WRITE) ? "r+b" : "rb") :
        ((flags & PS3_PLATFORM_FILE_APPEND) ? "ab" : "wb");
    FILE *stream = fdopen(fd, stdio_mode);
    if (!stream) {
        int error = errno;
        close(fd);
        return file_error(error);
    }
    SwitchFile *file = (SwitchFile *)malloc(sizeof(*file));
    if (!file) { fclose(stream); return PS3_PLATFORM_OUT_OF_MEMORY; }
    file->stream = stream;
    *out_file = file;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_file_read(void *context, void *handle,
    void *buffer, uint64_t bytes, uint64_t *out_read)
{
    (void)context;
    if (!handle || !out_read || (bytes && !buffer)) return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_read = 0;
    size_t count = bytes > SIZE_MAX ? SIZE_MAX : (size_t)bytes;
    if (!count) return PS3_PLATFORM_OK;
    FILE *stream = ((SwitchFile *)handle)->stream;
    *out_read = fread(buffer, 1, count, stream);
    return ferror(stream) ? file_error(errno) : PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_file_write(void *context, void *handle,
    const void *buffer, uint64_t bytes, uint64_t *out_written)
{
    (void)context;
    if (!handle || !out_written || (bytes && !buffer)) return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_written = 0;
    size_t count = bytes > SIZE_MAX ? SIZE_MAX : (size_t)bytes;
    if (!count) return PS3_PLATFORM_OK;
    FILE *stream = ((SwitchFile *)handle)->stream;
    *out_written = fwrite(buffer, 1, count, stream);
    return *out_written == count ? PS3_PLATFORM_OK : file_error(errno);
}

static Ps3PlatformStatus switch_file_seek(void *context, void *handle,
    int64_t offset, int32_t origin, uint64_t *out_position)
{
    (void)context;
    if (!handle || !out_position || origin < PS3_PLATFORM_SEEK_BEGIN ||
        origin > PS3_PLATFORM_SEEK_END) return PS3_PLATFORM_INVALID_ARGUMENT;
    int native_origin = origin == PS3_PLATFORM_SEEK_BEGIN ? SEEK_SET :
        origin == PS3_PLATFORM_SEEK_CURRENT ? SEEK_CUR : SEEK_END;
    FILE *stream = ((SwitchFile *)handle)->stream;
    if (fseeko(stream, (off_t)offset, native_origin) != 0) return file_error(errno);
    off_t position = ftello(stream);
    if (position < 0) return file_error(errno);
    *out_position = (uint64_t)position;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_file_stat(void *context, const char *path,
                                           Ps3PlatformFileInfo *out_info)
{
    (void)context;
    if (!path || !out_info) return PS3_PLATFORM_INVALID_ARGUMENT;
    struct stat data;
    if (stat(path, &data) != 0) return file_error(errno);
    fill_info(&data, out_info);
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_file_mkdir(void *context, const char *path)
{
    (void)context;
    if (!path) return PS3_PLATFORM_INVALID_ARGUMENT;
    return mkdir(path, 0777) == 0 ? PS3_PLATFORM_OK : file_error(errno);
}

static Ps3PlatformStatus switch_file_close(void *context, void *handle)
{
    (void)context;
    if (!handle) return PS3_PLATFORM_INVALID_ARGUMENT;
    SwitchFile *file = (SwitchFile *)handle;
    int rc = fclose(file->stream);
    free(file);
    return rc == 0 ? PS3_PLATFORM_OK : file_error(errno);
}

static Ps3PlatformStatus switch_dir_open(void *context, const char *path,
                                          void **out_dir)
{
    (void)context;
    if (!path || !out_dir || strlen(path) >= PS3_PLATFORM_MAX_PATH)
        return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_dir = NULL;
    SwitchDirectory *directory = (SwitchDirectory *)calloc(1, sizeof(*directory));
    if (!directory) return PS3_PLATFORM_OUT_OF_MEMORY;
    memcpy(directory->path, path, strlen(path) + 1);
    directory->native = opendir(path);
    if (!directory->native) {
        int error = errno;
        free(directory);
        return file_error(error);
    }
    *out_dir = directory;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_dir_read(void *context, void *handle,
                                          Ps3PlatformDirEntry *out_entry)
{
    (void)context;
    if (!handle || !out_entry) return PS3_PLATFORM_INVALID_ARGUMENT;
    SwitchDirectory *directory = (SwitchDirectory *)handle;
    struct dirent *entry;
    do {
        errno = 0;
        entry = readdir(directory->native);
        if (!entry) return errno ? file_error(errno) : PS3_PLATFORM_END_OF_DIRECTORY;
    } while (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."));
    size_t dir_length = strlen(directory->path);
    size_t name_length = strlen(entry->d_name);
    if (name_length >= sizeof(out_entry->name) ||
        dir_length + name_length + 2 > PS3_PLATFORM_MAX_PATH)
        return PS3_PLATFORM_IO_ERROR;
    memcpy(out_entry->name, entry->d_name, name_length + 1);
    char path[PS3_PLATFORM_MAX_PATH];
    memcpy(path, directory->path, dir_length);
    if (dir_length && path[dir_length - 1] != '/') path[dir_length++] = '/';
    memcpy(path + dir_length, entry->d_name, name_length + 1);
    struct stat data;
    if (stat(path, &data) != 0) return file_error(errno);
    fill_info(&data, &out_entry->info);
    return PS3_PLATFORM_OK;
}

static void switch_dir_close(void *context, void *handle)
{
    (void)context;
    if (!handle) return;
    SwitchDirectory *directory = (SwitchDirectory *)handle;
    closedir(directory->native);
    free(directory);
}

const Ps3PlatformFileOps *ps3_platform_switch_file(void)
{
    static const Ps3PlatformFileOps ops = {
        .context = NULL,
        .open = switch_file_open, .read = switch_file_read,
        .write = switch_file_write, .seek = switch_file_seek,
        .stat = switch_file_stat, .mkdir = switch_file_mkdir,
        .close = switch_file_close, .dir_open = switch_dir_open,
        .dir_read = switch_dir_read, .dir_close = switch_dir_close,
    };
    return &ops;
}
