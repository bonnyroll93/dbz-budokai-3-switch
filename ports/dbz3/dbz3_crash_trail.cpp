/*
 * Crash trail (PORT_DBZ3_CRASH_TRAIL): a light session log that goes with the
 * Atmosphere crash report, so a crash can be tied to what the game was doing.
 *
 * sdmc:/switch/dbz-budokai-3-switch/crash-trail-<unix seconds>.log, one per
 * session, the last kKeepFiles kept. A low-priority thread writes it once a
 * second at most; the game threads only copy a few words under a mutex:
 *   - events (boot stages, exit),
 *   - the set of scheduler task functions when it changes (tells menu, duel,
 *     Dragon Universe, cutscene apart),
 *   - every 5 s a heartbeat: images shown, FPS, current task, main thread lr,
 *     host heap in use,
 *   - when no image is shown for 10 s, where every guest thread is parked.
 * scripts/crash-report.ps1 matches it to the crash report by time.
 */
#include <switch.h>

#include <dirent.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <string>
#include <vector>

extern "C" uint32_t cellGcm_flip_request_count(void);
extern "C" uint32_t ppu_main_lr(void);
extern "C" void ppu_dump_all_thread_stacks(FILE* out);
extern "C" void spu_taskset_diag_snapshot(FILE* out);
extern "C" uint32_t g_dbz3_cur_task_fn;
/* dbz3_loop_prof.cpp with Logic60: passes, steps, gated, aborted, dropped_us. */
extern "C" void dbz3_logic60_counters(uint64_t out[5]) __attribute__((weak));

namespace {

constexpr const char* kDir = "sdmc:/switch/dbz-budokai-3-switch";
constexpr int kKeepFiles = 10;
constexpr int kEvents = 32;
constexpr int kEventLen = 192;
constexpr int kMaxFns = 64;
constexpr int kHeartbeatS = 5;
constexpr int kStallS = 10;

FILE* g_file = nullptr;
Mutex g_ring_lock;          /* events and task set: held for a copy only */
Mutex g_file_lock;          /* writer thread and exit handler */
char g_events[kEvents][kEventLen];
int g_event_head = 0, g_event_count = 0, g_event_lost = 0;
uint32_t g_fns[kMaxFns];
int g_fn_count = 0;
uint32_t g_fn_changes = 0;  /* task set changes since the last write */
bool g_fn_dirty = false;
Thread g_thread;
uint64_t g_t0 = 0;

double uptime_s(void)
{
    return (double)(armGetSystemTick() - g_t0) / 19200000.0;
}

void keep_last_files(void)
{
    DIR* d = opendir(kDir);
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
        if (strncmp(e->d_name, "crash-trail-", 12) == 0 && strstr(e->d_name, ".log"))
            names.emplace_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());   /* fixed-width seconds: sorts by time */
    for (size_t i = 0; i + (kKeepFiles - 1) < names.size(); ++i) {
        std::string path = std::string(kDir) + "/" + names[i];
        remove(path.c_str());
    }
}

void write_line(const char* text)
{
    mutexLock(&g_file_lock);
    if (g_file) fprintf(g_file, "[%9.1f] %s\n", uptime_s(), text);
    mutexUnlock(&g_file_lock);
}

void writer(void*)
{
    uint32_t last_flips = cellGcm_flip_request_count();
    uint32_t window_flips = last_flips;
    int no_flip_s = 0, since_heartbeat = 0;
    bool stall_reported = false;
    for (;;) {
        svcSleepThread(1000000000ull);
        bool wrote = false;

        char events[kEvents][kEventLen];
        int count = 0, lost = 0;
        uint32_t fns[kMaxFns];
        int fn_count = 0;
        uint32_t fn_changes = 0;
        bool fn_dirty = false;
        mutexLock(&g_ring_lock);
        for (; count < g_event_count; ++count) {
            int slot = (g_event_head - g_event_count + count + kEvents) % kEvents;
            memcpy(events[count], g_events[slot], kEventLen);
        }
        lost = g_event_lost;
        g_event_count = 0;
        g_event_lost = 0;
        if (g_fn_dirty) {
            fn_dirty = true;
            fn_count = g_fn_count;
            memcpy(fns, g_fns, sizeof(uint32_t) * (size_t)fn_count);
            fn_changes = g_fn_changes;
            g_fn_dirty = false;
            g_fn_changes = 0;
        }
        mutexUnlock(&g_ring_lock);

        if (lost) {
            char line[64];
            snprintf(line, sizeof line, "(%d events lost)", lost);
            write_line(line);
        }
        if (count) {   /* events carry the time they happened */
            mutexLock(&g_file_lock);
            for (int i = 0; i < count && g_file; ++i) fprintf(g_file, "%s\n", events[i]);
            mutexUnlock(&g_file_lock);
        }
        wrote |= count > 0 || lost > 0;
        if (fn_dirty) {
            char line[16 + kMaxFns * 11];
            int n = snprintf(line, sizeof line, "tasks(%d changes=%u):", fn_count, fn_changes);
            for (int i = 0; i < fn_count && n < (int)sizeof line - 12; ++i)
                n += snprintf(line + n, sizeof line - (size_t)n, " %06X", fns[i]);
            write_line(line);
            wrote = true;
        }

        const uint32_t flips = cellGcm_flip_request_count();
        if (flips != last_flips) {
            last_flips = flips;
            no_flip_s = 0;
            stall_reported = false;
        } else if (++no_flip_s >= kStallS && flips > 30 && !stall_reported) {
            char line[96];
            snprintf(line, sizeof line, "STALL: no image for %d s (flips=%u); thread dump follows",
                     no_flip_s, flips);
            write_line(line);
            mutexLock(&g_file_lock);
            if (g_file) {
                ppu_dump_all_thread_stacks(g_file);
                spu_taskset_diag_snapshot(g_file);
            }
            mutexUnlock(&g_file_lock);
            stall_reported = true;
            wrote = true;
        }

        if (++since_heartbeat >= kHeartbeatS) {
            struct mallinfo mi = mallinfo();
            char line[320];
            int n = snprintf(line, sizeof line,
                     "hb flips=%u fps=%.1f task=%06X lr=%08X heap_used_mb=%.1f",
                     flips, (double)(flips - window_flips) / since_heartbeat,
                     g_dbz3_cur_task_fn, ppu_main_lr(), mi.uordblks / 1048576.0);
            /* Logic60 over the window: logic steps per second (60 = real
             * time), passes per second, passes whose extra steps a load held
             * back, extra steps cut short by a load, owed time thrown away. */
            static uint64_t l60_prev[5];
            if (dbz3_logic60_counters && n > 0 && n < (int)sizeof line) {
                uint64_t c[5];
                dbz3_logic60_counters(c);
                snprintf(line + n, sizeof line - (size_t)n,
                         " l60 steps_s=%.1f passes_s=%.1f gated=%llu aborted=%llu dropped_ms=%llu",
                         (double)(c[1] - l60_prev[1]) / since_heartbeat,
                         (double)(c[0] - l60_prev[0]) / since_heartbeat,
                         (unsigned long long)(c[2] - l60_prev[2]),
                         (unsigned long long)(c[3] - l60_prev[3]),
                         (unsigned long long)((c[4] - l60_prev[4]) / 1000u));
                memcpy(l60_prev, c, sizeof c);
            }
            write_line(line);
            window_flips = flips;
            since_heartbeat = 0;
            wrote = true;
        }

        if (wrote) {
            mutexLock(&g_file_lock);
            if (g_file) fflush(g_file);
            mutexUnlock(&g_file_lock);
        }
    }
}

void at_exit(void)
{
    mutexLock(&g_file_lock);
    if (g_file) {
        fprintf(g_file, "[%9.1f] exit (normal)\n", uptime_s());
        fclose(g_file);
        g_file = nullptr;
    }
    mutexUnlock(&g_file_lock);
}

} // namespace

extern "C" void dbz3_trail_event(const char* fmt, ...)
{
    if (!g_t0) return;
    char text[kEventLen];
    int n = snprintf(text, sizeof text, "[%9.1f] ", uptime_s());
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text + n, sizeof text - (size_t)n, fmt, ap);
    va_end(ap);
    mutexLock(&g_ring_lock);
    if (g_event_count == kEvents) { ++g_event_lost; --g_event_count; }
    memcpy(g_events[g_event_head], text, kEventLen);
    g_event_head = (g_event_head + 1) % kEvents;
    ++g_event_count;
    mutexUnlock(&g_ring_lock);
}

/* Called by the scheduler (dbz3_loop_prof.cpp) when its set of task functions
 * changed; fns sorted. Only the latest set is written, with the change count. */
extern "C" void dbz3_trail_tasks(const uint32_t* fns, int count)
{
    if (!g_t0) return;
    if (count > kMaxFns) count = kMaxFns;
    mutexLock(&g_ring_lock);
    memcpy(g_fns, fns, sizeof(uint32_t) * (size_t)count);
    g_fn_count = count;
    ++g_fn_changes;
    g_fn_dirty = true;
    mutexUnlock(&g_ring_lock);
}

extern "C" void dbz3_trail_start(const char* build)
{
    if (g_t0) return;
    mutexInit(&g_ring_lock);
    mutexInit(&g_file_lock);
    keep_last_files();
    const time_t now = time(nullptr);
    char path[160];
    snprintf(path, sizeof path, "%s/crash-trail-%010lld.log", kDir, (long long)now);
    g_file = fopen(path, "w");
    if (!g_file) return;
    char date[32] = "?";
    struct tm tm_now;
    if (localtime_r(&now, &tm_now)) strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm_now);
    fprintf(g_file, "crash trail: build=%s boot_unix=%lld boot_local=%s\n",
            build, (long long)now, date);
    fflush(g_file);
    g_t0 = armGetSystemTick();
    atexit(at_exit);
    /* Below the game threads (0x2C) and the renderer; any core. */
    if (R_SUCCEEDED(threadCreate(&g_thread, writer, nullptr, nullptr, 0x8000, 0x3B, -2)))
        threadStart(&g_thread);
}
