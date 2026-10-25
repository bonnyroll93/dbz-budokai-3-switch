#include <switch.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "../spu/spu_profile.h"

#define PROFILE_THREADS 16
#define PROFILE_BUCKETS 256
typedef struct {
    uint64_t stamp;
    uint64_t task;
    unsigned used;
    uint64_t calls[2];   /* owned by the slot's thread: entries, marked entries */
} ProfileSlot;
typedef struct { uint64_t key; unsigned samples; } ProfileBucket;
static ProfileSlot slots[PROFILE_THREADS];
static unsigned next_slot, overflow_threads;
static int enabled;
static FILE *profile_file;
_Thread_local uint64_t *ps3_spu_profile_current;
_Thread_local uint64_t *ps3_spu_profile_calls;
static _Thread_local uint64_t disabled_stamp;
static _Thread_local int slot_index = -1;

uint64_t *ps3_spu_profile_slot(void)
{
    if (enabled) {
        unsigned index = __atomic_fetch_add(&next_slot, 1, __ATOMIC_RELAXED);
        if (index < PROFILE_THREADS) {
            slot_index = (int)index;
            ps3_spu_profile_current = &slots[index].stamp;
            ps3_spu_profile_calls = slots[index].calls;
            __atomic_store_n(&slots[index].used, 1, __ATOMIC_RELEASE);
            return ps3_spu_profile_current;
        }
        __atomic_fetch_add(&overflow_threads, 1, __ATOMIC_RELAXED);
    }
    return ps3_spu_profile_current = &disabled_stamp;
}

void ps3_spu_profile_bind(uint32_t taskset, uint32_t task)
{
    if (!ps3_spu_profile_current) ps3_spu_profile_slot();
    if (slot_index >= 0)
        __atomic_store_n(&slots[slot_index].task,
            ((uint64_t)taskset << 32) | (task + 1ull), __ATOMIC_RELAXED);
}

static void *profile_sampler(void *unused)
{
    (void)unused;
    /* Only the sampler owns these histograms. No atomics in the accumulation. */
    static ProfileBucket buckets[PROFILE_THREADS][PROFILE_BUCKETS];
    unsigned idle[PROFILE_THREADS] = {0}, active[PROFILE_THREADS] = {0};
    unsigned dropped[PROFILE_THREADS] = {0};
    uint64_t calls_prev[PROFILE_THREADS][2] = {{0}};
    uint64_t started = armGetSystemTick(), window = started;
    for (;;) {
        svcSleepThread(2000000);
        for (unsigned t = 0; t < PROFILE_THREADS; ++t) {
            if (!__atomic_load_n(&slots[t].used, __ATOMIC_ACQUIRE)) continue;
            uint64_t key = __atomic_load_n(&slots[t].stamp, __ATOMIC_RELAXED);
            if (!key) { ++idle[t]; continue; }
            ++active[t];
            unsigned b = (unsigned)((key >> 2) ^ (key >> 32)) % PROFILE_BUCKETS;
            unsigned probe;
            for (probe = 0; probe < PROFILE_BUCKETS; ++probe) {
                ProfileBucket *bucket = &buckets[t][(b + probe) % PROFILE_BUCKETS];
                if (!bucket->key || bucket->key == key) {
                    bucket->key = key;
                    ++bucket->samples;
                    break;
                }
            }
            if (probe == PROFILE_BUCKETS) ++dropped[t];
        }
        uint64_t now = armGetSystemTick();
        uint64_t wall_ms = armTicksToNs(now - window) / 1000000;
        if (wall_ms < 5000) continue;
        fprintf(profile_file, "window_ms=%llu elapsed_ms=%llu overflow_threads=%u sampling=wall_function\n",
            (unsigned long long)wall_ms,
            (unsigned long long)(armTicksToNs(now - started) / 1000000),
            __atomic_load_n(&overflow_threads, __ATOMIC_RELAXED));
        for (unsigned t = 0; t < PROFILE_THREADS; ++t) {
            if (!idle[t] && !active[t]) continue;
            uint64_t task = __atomic_load_n(&slots[t].task, __ATOMIC_RELAXED);
            const uint64_t c0 = __atomic_load_n(&slots[t].calls[0], __ATOMIC_RELAXED);
            const uint64_t c1 = __atomic_load_n(&slots[t].calls[1], __ATOMIC_RELAXED);
            fprintf(profile_file, "thread=%u taskset=0x%08X task=%d active=%u outside_or_wait=%u dropped=%u "
                "calls_per_s=%llu marked_per_s=%llu\n",
                t, (unsigned)(task >> 32), (int)(uint32_t)task - 1,
                active[t], idle[t], dropped[t],
                (unsigned long long)((c0 - calls_prev[t][0]) * 1000u / wall_ms),
                (unsigned long long)((c1 - calls_prev[t][1]) * 1000u / wall_ms));
            calls_prev[t][0] = c0;
            calls_prev[t][1] = c1;
            for (unsigned rank = 0; rank < 10; ++rank) {
                unsigned best = 0;
                for (unsigned b = 1; b < PROFILE_BUCKETS; ++b)
                    if (buckets[t][b].samples > buckets[t][best].samples) best = b;
                ProfileBucket *bucket = &buckets[t][best];
                if (!bucket->samples) break;
                fprintf(profile_file, "hot thread=%u image=%u pc=0x%05X samples=%u\n",
                    t, (unsigned)(bucket->key >> 32), (unsigned)bucket->key, bucket->samples);
                bucket->samples = 0;
            }
        }
        fflush(profile_file);
        memset(buckets, 0, sizeof buckets);
        memset(idle, 0, sizeof idle);
        memset(active, 0, sizeof active);
        memset(dropped, 0, sizeof dropped);
        window = armGetSystemTick();
    }
    return NULL;
}

int ps3_spu_profile_start(void)
{
    profile_file = fopen("sdmc:/switch/dbz-budokai-3-switch/t21-spu-hot.log", "w");
    if (!profile_file) return 0;
    fprintf(profile_file, "SPU lifted-function wall sampler; 2ms requested, top10 per 5s; WAIT_SIGNAL excluded\n");
    fflush(profile_file);
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int rc = pthread_attr_setstacksize(&attr, 64 * 1024);
    if (!rc) rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (!rc) rc = pthread_create(&thread, &attr, profile_sampler, NULL);
    pthread_attr_destroy(&attr);
    if (rc) { fclose(profile_file); profile_file = NULL; return 0; }
    enabled = 1; /* Called before any guest/SPU threads are launched. */
    return 1;
}
