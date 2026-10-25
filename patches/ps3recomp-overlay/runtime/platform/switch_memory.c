#include "switch_memory.h"

#include <switch.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GUEST_SIZE 0x100000000ull
#define MAP_UNIT 0x10000ull
#define GUARD_SIZE 0x10000ull
#define MAP_UNITS (GUEST_SIZE / MAP_UNIT)

static uint8_t s_mapped[MAP_UNITS / 8];
static uint8_t s_added[MAP_UNITS / 8];
static VirtmemReservation *s_reservation;
static uintptr_t s_base;
static Mutex s_mutex;

static bool bit_get(const uint8_t *bits, uint32_t index) {
    return (bits[index >> 3] & (1u << (index & 7))) != 0;
}

static void bit_set(uint8_t *bits, uint32_t index, bool value) {
    const uint8_t mask = (uint8_t)(1u << (index & 7));
    if (value) bits[index >> 3] |= mask;
    else bits[index >> 3] &= (uint8_t)~mask;
}

static Ps3PlatformStatus switch_reserve(void *context, uint64_t size,
                                         uint64_t alignment, void **out_base) {
    (void)context;
    if (!out_base || size != GUEST_SIZE || alignment != GUEST_SIZE)
        return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_base = NULL;
    if (s_reservation) return PS3_PLATFORM_BUSY;

    uint64_t alias_start = 0, alias_size = 0, alias_extra = 0;
    if (R_FAILED(svcGetInfo(&alias_start, InfoType_AliasRegionAddress,
                            CUR_PROCESS_HANDLE, 0)) ||
        R_FAILED(svcGetInfo(&alias_size, InfoType_AliasRegionSize,
                            CUR_PROCESS_HANDLE, 0)) ||
        alias_size > UINT64_MAX - alias_start)
        return PS3_PLATFORM_UNSUPPORTED;
    if (R_SUCCEEDED(svcGetInfo(&alias_extra, InfoType_AliasRegionExtraSize,
                               CUR_PROCESS_HANDLE, 0)) && alias_extra <= alias_size)
        alias_size -= alias_extra;

    const uint64_t end = alias_start + alias_size;
    uint64_t cursor = alias_start;
    virtmemLock();
    for (unsigned queries = 0; cursor < end && queries < 100000; ++queries) {
        MemoryInfo info;
        uint32_t page_info = 0;
        if (R_FAILED(svcQueryMemory(&info, &page_info, cursor)) ||
            info.size > UINT64_MAX - info.addr || info.addr + info.size <= cursor)
            break;
        uint64_t next = info.addr + info.size;
        if (next > end) next = end;
        if ((info.type & MemState_Type) == MemType_Unmapped &&
            next - cursor >= GUEST_SIZE + GUARD_SIZE) {
            uint64_t base = (cursor + GUARD_SIZE + GUEST_SIZE - 1) &
                            ~(GUEST_SIZE - 1);
            if (base >= cursor + GUARD_SIZE && base <= next &&
                GUEST_SIZE + GUARD_SIZE <= next - base) {
                VirtmemReservation *reservation = virtmemAddReservation(
                    (void *)(uintptr_t)base, (size_t)GUEST_SIZE);
                if (reservation) {
                    s_reservation = reservation;
                    s_base = (uintptr_t)base;
                    memset(s_mapped, 0, sizeof(s_mapped));
                    mutexInit(&s_mutex);
                    *out_base = (void *)s_base;
                    virtmemUnlock();
                    return PS3_PLATFORM_OK;
                }
            }
        }
        cursor = next;
    }
    virtmemUnlock();
    return PS3_PLATFORM_OUT_OF_MEMORY;
}

static Ps3PlatformStatus switch_commit(void *context, void *base, uint64_t offset,
                                        uint64_t size, uint32_t protection) {
    (void)context;
    if (!s_reservation || base != (void *)s_base || !size ||
        offset >= GUEST_SIZE || size > GUEST_SIZE - offset ||
        (protection & PS3_PLATFORM_MEM_EXECUTE))
        return PS3_PLATFORM_INVALID_ARGUMENT;
    uint64_t start = offset & ~(MAP_UNIT - 1);
    uint64_t end = (offset + size + MAP_UNIT - 1) & ~(MAP_UNIT - 1);
    if (end > GUEST_SIZE) return PS3_PLATFORM_INVALID_ARGUMENT;

    mutexLock(&s_mutex);
    memset(s_added, 0, sizeof(s_added));
    Ps3PlatformStatus status = PS3_PLATFORM_OK;
    for (uint64_t at = start; at < end; at += MAP_UNIT) {
        uint32_t index = (uint32_t)(at / MAP_UNIT);
        if (bit_get(s_mapped, index)) continue;
        Result rc = svcMapPhysicalMemory((void *)(s_base + at), MAP_UNIT);
        if (R_FAILED(rc)) {
            status = PS3_PLATFORM_OUT_OF_MEMORY;
            break;
        }
        bit_set(s_mapped, index, true);
        bit_set(s_added, index, true);
    }
    if (status == PS3_PLATFORM_OK &&
        (protection & (PS3_PLATFORM_MEM_READ | PS3_PLATFORM_MEM_WRITE)) !=
            (PS3_PLATFORM_MEM_READ | PS3_PLATFORM_MEM_WRITE)) {
        u32 perm = (protection & PS3_PLATFORM_MEM_READ) ? Perm_R : Perm_None;
        if (R_FAILED(svcSetMemoryPermission((void *)(s_base + start), end - start, perm)))
            status = PS3_PLATFORM_UNSUPPORTED;
    }
    if (status != PS3_PLATFORM_OK) {
        for (uint64_t at = start; at < end; at += MAP_UNIT) {
            uint32_t index = (uint32_t)(at / MAP_UNIT);
            if (!bit_get(s_added, index)) continue;
            if (R_SUCCEEDED(svcUnmapPhysicalMemory((void *)(s_base + at), MAP_UNIT)))
                bit_set(s_mapped, index, false);
        }
    }
    mutexUnlock(&s_mutex);
    return status;
}

static Ps3PlatformStatus switch_protect(void *context, void *base, uint64_t offset,
                                         uint64_t size, uint32_t protection) {
    (void)context;
    if (!s_reservation || base != (void *)s_base || !size ||
        offset >= GUEST_SIZE || size > GUEST_SIZE - offset ||
        (offset & 0xfff) || (size & 0xfff) ||
        (protection & PS3_PLATFORM_MEM_EXECUTE))
        return PS3_PLATFORM_INVALID_ARGUMENT;
    const uint64_t first = offset / MAP_UNIT;
    const uint64_t last = (offset + size - 1) / MAP_UNIT;
    mutexLock(&s_mutex);
    for (uint64_t i = first; i <= last; ++i) {
        if (!bit_get(s_mapped, (uint32_t)i)) {
            mutexUnlock(&s_mutex);
            return PS3_PLATFORM_INVALID_ARGUMENT;
        }
    }
    u32 perm = (protection & PS3_PLATFORM_MEM_WRITE) ? Perm_Rw :
                (protection & PS3_PLATFORM_MEM_READ) ? Perm_R : Perm_None;
    Result rc = svcSetMemoryPermission((void *)(s_base + offset), size, perm);
    mutexUnlock(&s_mutex);
    return R_SUCCEEDED(rc) ? PS3_PLATFORM_OK : PS3_PLATFORM_UNSUPPORTED;
}

static void switch_release(void *context, void *base, uint64_t size) {
    (void)context;
    if (!s_reservation || base != (void *)s_base || size != GUEST_SIZE) return;
    mutexLock(&s_mutex);
    bool all_unmapped = true;
    for (uint32_t i = 0; i < MAP_UNITS; ++i) {
        if (!bit_get(s_mapped, i)) continue;
        Result rc = svcUnmapPhysicalMemory((void *)(s_base + (uint64_t)i * MAP_UNIT), MAP_UNIT);
        if (R_SUCCEEDED(rc)) bit_set(s_mapped, i, false);
        else {
            all_unmapped = false;
            fprintf(stderr, "Switch guest unmap failed at %08x: %08x\n", i * (uint32_t)MAP_UNIT, rc);
        }
    }
    mutexUnlock(&s_mutex);
    if (all_unmapped) {
        virtmemLock();
        virtmemRemoveReservation(s_reservation);
        virtmemUnlock();
        s_reservation = NULL;
        s_base = 0;
    }
}

const Ps3PlatformMemory *ps3_platform_switch_memory(void) {
    static const Ps3PlatformMemory memory = {
        NULL, switch_reserve, switch_commit, switch_protect, switch_release
    };
    return &memory;
}

uint64_t ps3_switch_used_memory(void) {
    u64 used = 0;
    return R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize,
                                   CUR_PROCESS_HANDLE, 0)) ? used : 0;
}

int ps3_switch_is_no_access(const void *address) {
    MemoryInfo info;
    u32 page_info = 0;
    return R_SUCCEEDED(svcQueryMemory(&info, &page_info, (u64)(uintptr_t)address)) &&
           info.perm == Perm_None;
}

int ps3_switch_is_readable_range(const void *address, uint64_t size) {
    uintptr_t at = (uintptr_t)address;
    if (!size) return 1;
    if (size > UINTPTR_MAX - at) return 0;
    uintptr_t end = at + (uintptr_t)size;
    while (at < end) {
        MemoryInfo info;
        u32 page_info = 0;
        if (R_FAILED(svcQueryMemory(&info, &page_info, (u64)at)) ||
            (info.perm != Perm_R && info.perm != Perm_Rw) ||
            info.size > UINTPTR_MAX - info.addr || info.addr + info.size <= at)
            return 0;
        at = (uintptr_t)(info.addr + info.size);
    }
    return 1;
}

#include <malloc.h>
#include <stdlib.h>

static void probe_entry(void *arg) { (void)arg; }

void ps3_switch_resource_report(const char *tag) {
    static const struct { u32 id; const char *name; } k[] = {
        { InfoType_TotalMemorySize, "mem_total" },
        { InfoType_UsedMemorySize, "mem_used" },
        { InfoType_SystemResourceSizeTotal, "sysres_total" },
        { InfoType_SystemResourceSizeUsed, "sysres_used" },
        { InfoType_HeapRegionSize, "heap_region" },
        { InfoType_StackRegionSize, "stack_region" },
        { InfoType_FreeThreadCount, "free_threads" },
    };
    char line[768];
    int n = snprintf(line, sizeof line, "[res] %s:", tag ? tag : "");
    for (size_t i = 0; i < sizeof k / sizeof k[0] && n < (int)sizeof line; i++) {
        u64 v = 0;
        Result rc = svcGetInfo(&v, k[i].id, CUR_PROCESS_HANDLE, 0);
        if (R_FAILED(rc))
            n += snprintf(line + n, sizeof line - n, " %s=rc%x", k[i].name, rc);
        else if (k[i].id == InfoType_FreeThreadCount)
            n += snprintf(line + n, sizeof line - n, " %s=%llu", k[i].name,
                          (unsigned long long)v);
        else
            n += snprintf(line + n, sizeof line - n, " %s=%lluMiB", k[i].name,
                          (unsigned long long)(v >> 20));
    }
    struct mallinfo mi = mallinfo();
    if (n < (int)sizeof line)
        n += snprintf(line + n, sizeof line - n, " malloc_arena=%zuMiB malloc_free=%zuMiB",
                      (size_t)mi.arena >> 20, (size_t)mi.fordblks >> 20);
    /* Can the heap still give a thread stack, and can the kernel still give a
     * thread? Each probe is released right away. */
    void *m1 = malloc(1u << 20);
    void *m32 = malloc(32u << 20);
    if (n < (int)sizeof line)
        n += snprintf(line + n, sizeof line - n, " malloc1M=%s malloc32M=%s",
                      m1 ? "ok" : "FAIL", m32 ? "ok" : "FAIL");
    free(m32);
    free(m1);
    Thread t;
    Result trc = threadCreate(&t, probe_entry, NULL, NULL, 0x10000, 0x2C, -2);
    if (R_SUCCEEDED(trc)) threadClose(&t);
    if (n < (int)sizeof line)
        snprintf(line + n, sizeof line - n, " probe_thread=%s(0x%x)\n",
                 R_SUCCEEDED(trc) ? "ok" : "FAIL", trc);
    fputs(line, stderr);
    FILE *f = fopen("sdmc:/switch/dbz-budokai-3-switch/t22-gpu.log", "a");
    if (f) { fputs(line, f); fclose(f); }
}
