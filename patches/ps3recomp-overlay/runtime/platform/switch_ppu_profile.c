/* T23: wall-clock sampler of the main PPU thread's guest position (Switch).
 *
 * Lifted code stores the return address in ctx->lr before every call and
 * restores it on return, so the main context's lr is "the last guest call or
 * return point executed". Sampling it every millisecond gives a statistical
 * profile of where the main thread spends its time without touching the
 * lifted code. Each 5 s window writes the 40 hottest addresses to
 * sdmc:/switch/dbz-budokai-3-switch/t23-ppu-hot.log; map them to functions offline
 * with the analysis function list. */
#ifdef __SWITCH__
#include <switch.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
uint32_t ppu_main_lr(void);   /* sys_ppu_thread.c */
/* T38: the main thread's innermost lifted function, in a lift built with
 * scripts/ppu_instrument.py (0 otherwise; ppu_loader.cpp). */
extern volatile uint32_t g_ppu_fn_stamp;
static int s_fn_mode;

/* Task attribution (dbz3_loop_prof.cpp): guest fn of the scheduler task the
 * main thread is running, 0 outside tasks. Weak so other ports link without it. */
extern uint32_t g_dbz3_cur_task_fn __attribute__((weak));

#define HOT_BUCKETS 16384u
typedef struct { uint32_t addr, task, samples; } HotBucket;

static FILE* s_hot_file;

static unsigned hot_add(HotBucket* tab, uint32_t a, uint32_t t)
{
    unsigned h = ((a >> 2) ^ (t * 40503u)) * 2654435761u % HOT_BUCKETS, p;
    for (p = 0; p < 64; p++) {
        HotBucket* b = &tab[(h + p) % HOT_BUCKETS];
        if (!b->samples || (b->addr == a && b->task == t)) {
            b->addr = a; b->task = t; b->samples++; return 1;
        }
    }
    return 0;
}

/* Prints and clears the top `n` entries of `tab`. */
static void hot_dump(HotBucket* tab, unsigned n, const char* fmt_kind)
{
    for (unsigned rank = 0; rank < n; rank++) {
        unsigned best = 0;
        for (unsigned i = 1; i < HOT_BUCKETS; i++)
            if (tab[i].samples > tab[best].samples) best = i;
        if (!tab[best].samples) break;
        if (fmt_kind[0] == 't')
            fprintf(s_hot_file, "  task=0x%08X %u\n", tab[best].task, tab[best].samples);
        else if (fmt_kind[0] == 'p')
            fprintf(s_hot_file, "  %s=0x%08X task=0x%08X %u\n", s_fn_mode ? "fnt" : "lrt",
                    tab[best].addr, tab[best].task, tab[best].samples);
        else
            fprintf(s_hot_file, "  %s=0x%08X %u\n", s_fn_mode ? "fn" : "lr",
                    tab[best].addr, tab[best].samples);
        tab[best].samples = 0;
    }
}

static void* ppu_hot_sampler(void* unused)
{
    (void)unused;
    static HotBucket by_fn[HOT_BUCKETS], by_pair[HOT_BUCKETS], by_task[HOT_BUCKETS];
    unsigned total = 0, lost = 0;
    uint64_t window = armGetSystemTick();
    for (;;) {
        svcSleepThread(1000000);
        const uint32_t stamp = g_ppu_fn_stamp;
        if (stamp) s_fn_mode = 1;
        const uint32_t a = s_fn_mode ? stamp : ppu_main_lr();
        const uint32_t t = &g_dbz3_cur_task_fn ? g_dbz3_cur_task_fn : 0;
        if (!a) continue;
        total++;
        if (!hot_add(by_fn, a, 0)) lost++;
        hot_add(by_pair, a, t);
        hot_add(by_task, 0, t);
        const uint64_t now = armGetSystemTick();
        if (armTicksToNs(now - window) < 5000000000ull) continue;
        fprintf(s_hot_file, "window samples=%u lost=%u\n", total, lost);
        hot_dump(by_fn, 60, "f");
        hot_dump(by_task, 30, "t");
        hot_dump(by_pair, 120, "p");
        fflush(s_hot_file);
        memset(by_fn, 0, sizeof by_fn);
        memset(by_pair, 0, sizeof by_pair);
        memset(by_task, 0, sizeof by_task);
        total = lost = 0;
        window = armGetSystemTick();
    }
    return NULL;
}

int ps3_ppu_hot_profile_start(void)
{
    s_hot_file = fopen("sdmc:/switch/dbz-budokai-3-switch/t23-ppu-hot.log", "w");
    if (!s_hot_file) return 0;
    fprintf(s_hot_file, "main PPU thread ctx->lr wall sampler, 1 ms, top 40 per 5 s\n");
    fflush(s_hot_file);
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int rc = pthread_attr_setstacksize(&attr, 64 * 1024);
    if (!rc) rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (!rc) rc = pthread_create(&thread, &attr, ppu_hot_sampler, NULL);
    pthread_attr_destroy(&attr);
    if (rc) { fclose(s_hot_file); s_hot_file = NULL; return 0; }
    return 1;
}
#endif
