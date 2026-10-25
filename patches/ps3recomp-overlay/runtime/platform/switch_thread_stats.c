/*
 * T25: where the Switch's CPU time goes, per thread and per core.
 *
 * Threads register themselves on entry (ps3_thread_stats_register: guest PPU
 * threads under their guest name, SPU / SPURS / audio / timer / frame clock
 * under a kind). Every ~5 s a sampler writes to
 * sdmc:/switch/dbz-budokai-3-switch/t25-threads.log:
 *   - each application core's busy share, from the kernel idle tick count
 *     (a pinned helper per core, since a core's idle count can only be read
 *     from that core);
 *   - CPU ms per kind, and the busiest threads with the cores they may run on
 *     (ideal core, affinity mask).
 * Diagnostic only: nothing here changes scheduling.
 */
#include <switch.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define TS_MAX_THREADS 192
#define TS_CORES 3
#define TS_TOP 16

typedef struct {
    Handle handle;
    char kind[12];
    char name[28];
    uint64_t last_ns;
} TsThread;

static TsThread s_threads[TS_MAX_THREADS];
static atomic_uint s_count;
static atomic_uint s_ready[TS_MAX_THREADS];

static uint64_t s_core_busy_pm[TS_CORES];      /* per-mille, last window */
static atomic_int s_core_valid[TS_CORES];

static uint64_t ts_cpu_ns(Handle h)
{
    u64 ticks = 0;
    Result rc = svcGetInfo(&ticks, InfoType_ThreadTickCount, h, TickCountInfo_Total);
    if (R_FAILED(rc))
        rc = svcGetInfo(&ticks, InfoType_ThreadTickCountDeprecated, h, TickCountInfo_Total);
    return R_SUCCEEDED(rc) ? armTicksToNs(ticks) : UINT64_MAX;
}

void ps3_thread_stats_register(const char* kind, const char* name)
{
    unsigned i = atomic_fetch_add(&s_count, 1u);
    if (i >= TS_MAX_THREADS) return;
    TsThread* t = &s_threads[i];
    t->handle = threadGetCurHandle();
    snprintf(t->kind, sizeof t->kind, "%s", kind ? kind : "?");
    snprintf(t->name, sizeof t->name, "%s", (name && name[0]) ? name : "-");
    uint64_t ns = ts_cpu_ns(t->handle);
    t->last_ns = ns == UINT64_MAX ? 0 : ns;
    atomic_store(&s_ready[i], 1u);
}

/* One per application core: reads that core's idle ticks every 5 s. */
static void core_probe(void* arg)
{
    const int core = (int)(intptr_t)arg;
    u64 idle_prev = 0, tick_prev = 0;
    for (;;) {
        u64 idle = 0;
        Result rc = svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, (u64)-1);
        const u64 now = armGetSystemTick();
        if (R_SUCCEEDED(rc) && tick_prev && now > tick_prev) {
            u64 di = idle - idle_prev, dt = now - tick_prev;
            if (di > dt) di = dt;
            s_core_busy_pm[core] = 1000u - (di * 1000u) / dt;
            atomic_store(&s_core_valid[core], 1);
        } else if (R_FAILED(rc)) {
            atomic_store(&s_core_valid[core], -1);
        }
        idle_prev = idle;
        tick_prev = now;
        svcSleepThread(5000000000ull);
    }
}

typedef struct { const char* kind; uint64_t ns; } KindSum;

static void ts_sampler(void* arg)
{
    (void)arg;
    FILE* f = fopen("sdmc:/switch/dbz-budokai-3-switch/t25-threads.log", "w");
    if (!f) return;
    fprintf(f, "T25 per-thread CPU and per-core busy, every ~5 s\n");
    fflush(f);
    uint64_t prev = armGetSystemTick();
    static uint64_t delta[TS_MAX_THREADS];
    for (;;) {
        svcSleepThread(5000000000ull);
        const uint64_t now = armGetSystemTick();
        const uint64_t wall_ms = armTicksToNs(now - prev) / 1000000u;
        prev = now;

        fprintf(f, "window_ms=%llu cores_busy_pct=", (unsigned long long)wall_ms);
        for (int c = 0; c < TS_CORES; c++) {
            int v = atomic_load(&s_core_valid[c]);
            if (v > 0) fprintf(f, "%s%llu.%llu", c ? "/" : "",
                               (unsigned long long)(s_core_busy_pm[c] / 10u),
                               (unsigned long long)(s_core_busy_pm[c] % 10u));
            else fprintf(f, "%s%s", c ? "/" : "", v < 0 ? "err" : "?");
        }
        fprintf(f, "\n");

        unsigned n = atomic_load(&s_count);
        if (n > TS_MAX_THREADS) n = TS_MAX_THREADS;
        KindSum kinds[16]; int nk = 0;
        uint64_t total = 0;
        for (unsigned i = 0; i < n; i++) {
            delta[i] = 0;
            if (!atomic_load(&s_ready[i])) continue;
            uint64_t ns = ts_cpu_ns(s_threads[i].handle);
            if (ns == UINT64_MAX) continue;              /* exited */
            delta[i] = ns >= s_threads[i].last_ns ? ns - s_threads[i].last_ns : 0;
            s_threads[i].last_ns = ns;
            total += delta[i];
            int k = 0;
            while (k < nk && strcmp(kinds[k].kind, s_threads[i].kind)) k++;
            if (k == nk && nk < 16) { kinds[nk].kind = s_threads[i].kind; kinds[nk].ns = 0; nk++; }
            if (k < nk) kinds[k].ns += delta[i];
        }
        fprintf(f, "  total_cpu_ms=%llu by_kind:", (unsigned long long)(total / 1000000u));
        for (int k = 0; k < nk; k++)
            fprintf(f, " %s=%llu", kinds[k].kind, (unsigned long long)(kinds[k].ns / 1000000u));
        fprintf(f, "\n");

        for (int r = 0; r < TS_TOP; r++) {
            unsigned best = TS_MAX_THREADS; uint64_t bv = 0;
            for (unsigned i = 0; i < n; i++)
                if (delta[i] > bv) { bv = delta[i]; best = i; }
            if (best == TS_MAX_THREADS || bv < 1000000u) break;   /* < 1 ms */
            s32 ideal = -1; u64 mask = 0;
            svcGetThreadCoreMask(&ideal, &mask, s_threads[best].handle);
            fprintf(f, "  %-6s %-27s cpu_ms=%-6llu pct=%-5llu ideal=%d mask=0x%llx\n",
                    s_threads[best].kind, s_threads[best].name,
                    (unsigned long long)(bv / 1000000u),
                    (unsigned long long)(wall_ms ? bv / 10000u / wall_ms : 0),
                    (int)ideal, (unsigned long long)mask);
            delta[best] = 0;
        }
        fflush(f);
    }
}

int ps3_thread_stats_start(void)
{
    static Thread probes[TS_CORES], sampler;
    for (int c = 0; c < TS_CORES; c++) {
        if (R_SUCCEEDED(threadCreate(&probes[c], core_probe, (void*)(intptr_t)c,
                                     NULL, 0x4000, 0x20, c)))
            threadStart(&probes[c]);
    }
    if (R_FAILED(threadCreate(&sampler, ts_sampler, NULL, NULL, 0x8000, 0x2C, -2)))
        return 0;
    return R_SUCCEEDED(threadStart(&sampler));
}
