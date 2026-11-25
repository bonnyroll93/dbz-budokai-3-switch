/*
 * DBZ3 (LAUNCH/DBZ3.elf) -- profile of the title's main loop (T6.1).
 *
 * main() is func_00159BE8 (ppu_recomp_002.cpp). Its frame loop (0x15A198 ..
 * 0x15A2C0) does, in order:
 *   indirect call  (*(0x47C5E8))->vtable[2]        "scene" object
 *   0x4883C        exit check
 *   0x48088 0x2E958 0x1176C  [cellAudioOut get/configure]
 *   0x1C000 0x15B10 0x1573C 0x1F318 0x24588 0x15EA8 0x1745C 0x28FD8
 *   0x1F9DC0 0x287F4 0x15AB8(32) 0x15C54 0x2E9A4
 *   0x487FC        flip (cellRescSetConvertAndFlip) + sysutil callbacks
 * The game advances its logic one step per pass, as on PS3: the flip wrapper
 * holds the passes to the debug menu FRAME CAP (pace_before_flip). This also
 * measures what each step costs. (The "Logic60" extra logic steps per drawn
 * frame were removed on 2026-11-25: they ran tasks in half-built states,
 * run233 crash, and cost more CPU than they saved, run234.)
 *
 * The callees are in ppu_recomp_000.cpp, main in ppu_recomp_002.cpp, so the
 * linker's --wrap catches main's calls (CMakeLists.txt). The scene call is
 * timed as the gap between the end of the flip and the exit check.
 *
 * T26 log: sdmc:/switch/dbz-budokai-3-switch/t26-loop-prof.log, one block per ~5 s.
 */

#define PPU_FAST_VM 1
#include "ppu_recomp.h"

#include <cstdio>
#include <cstring>
#include <cstdint>

#ifdef __SWITCH__
#include <switch.h>
static inline uint64_t lp_ticks(void) { return armGetSystemTick(); }
static inline uint64_t lp_us(uint64_t t) { return (t * 5u) / 96u; } /* 19.2 MHz */
#else
#include <chrono>
static inline uint64_t lp_ticks(void)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static inline uint64_t lp_us(uint64_t t) { return t; }
#endif

/* Gameplay pacing uses lp_ticks(); diagnostic timing uses metric_ticks(). */
static inline uint64_t metric_ticks(void)
{
#ifdef PORT_DBZ3_LOOP_METRICS
    return lp_ticks();
#else
    return 0;
#endif
}

extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);

extern "C" int ps3_diag_enabled;   /* cellGcmSys.c, from diag.on at boot */
extern "C" uint64_t g_dbz3_adx_sur_calls;   /* dbz3_audio_trace.cpp */

/* Scheduler task running now, for the allocator trace (dbz3_heap_trace.cpp). */
extern "C" { uint32_t g_dbz3_cur_task_fn = 0; int g_dbz3_cur_pass = 0; }
#ifdef PORT_DBZ3_CRASH_TRAIL
extern "C" void dbz3_trail_tasks(const uint32_t* fns, int count);   /* dbz3_crash_trail.cpp */
extern "C" void dbz3_tlsf_walk(void);                                /* dbz3_tlsf_guard.cpp */
#endif
/* Debug menu FRAME CAP (switch_input.c): 60, 30 or 0 = off. */
extern "C" __attribute__((weak)) int switch_debug_frame_cap(void);
/* End-of-frame wait counters, filled by 0x002909C0 (dbz3_native_math.cpp). */
extern "C" { uint64_t g_fw_waits, g_fw_wait_ticks, g_fw_fifo_ticks, g_fw_deliver_ticks, g_fw_max_ticks; }
#ifdef PORT_DBZ3_HEAP_TRACE
extern "C" void dbz3_heap_trace_dump(FILE* f, uint32_t addr);
#else
static inline void dbz3_heap_trace_dump(FILE*, uint32_t) {}
#endif

#define DRAIN_TRAMPOLINE(ctx) do { \
    while (g_trampoline_fn) { \
        void (*_tf)(void*) = g_trampoline_fn; \
        g_trampoline_fn = 0; \
        _tf((void*)(ctx)); \
    } \
} while (0)

namespace {

/* Slot 0 is the scene call (gap), then the wrapped callees in loop order. */
enum {
    S_SCENE, S_4883C, S_48088, S_2E958, S_1176C, S_1C000, S_15B10, S_1573C,
    S_1F318, S_24588, S_15EA8, S_1745C, S_28FD8, S_1F9DC0, S_287F4, S_15AB8,
    S_15C54, S_2E9A4, S_487FC, S_COUNT
};
const char* const kNames[S_COUNT] = {
    "scene", "4883C", "48088", "2E958", "1176C", "1C000", "15B10", "1573C",
    "1F318", "24588", "15EA8", "1745C", "28FD8", "1F9DC0", "287F4", "15AB8",
    "15C54", "2E9A4", "487FC",
};

constexpr uint32_t kSceneObjPtr = 0x0047C5E8u;   /* 0x47C5C0 + 0x28 */
constexpr int kMaxTargets = 8;

/* Per-task stats of the task scheduler func_00028FD8 (one entry per guest
 * function a task runs). "gcm" = runs that moved the title's GCM write
 * pointer, i.e. the task emits draw commands. */
constexpr int kMaxTasks = 64;
constexpr uint32_t kTitleGcmCtx = 0x0F800000u;   /* CellGcmContextData */

struct TaskStat {
    uint32_t fn;          /* guest code address (0 = free slot) */
    uint32_t task;        /* last task object seen with it */
    uint32_t order;       /* position in the list, last pass */
    uint64_t runs, us, max_us, gcm_runs, gcm_bytes;
};

struct Window {
    uint64_t sum[S_COUNT];
    uint64_t max[S_COUNT];
    uint64_t frames;
    uint64_t frame_sum, frame_max;
    uint32_t target[kMaxTargets];
    uint64_t target_frames[kMaxTargets];
    uint64_t target_us[kMaxTargets];
    TaskStat tasks[kMaxTasks];
    uint64_t task_passes;
    uint64_t du_phase_runs[2][3], du_phase_us[2][3], du_phase_gcm[2][3];
#ifdef PORT_DBZ3_LEVEL_FUNCTION_PROFILE
    uint64_t level30790_calls, level30790_ticks, level30790_max_ticks;
#endif
};

Window g_w;
FILE* g_log = nullptr;
uint64_t g_window_start = 0;
uint64_t g_flip_end = 0;        /* end of the previous pass's flip */
uint64_t g_pass_start = 0;      /* start of the previous pass's exit check */
uint32_t g_last_target = 0;

/* Guest function behind (*(0x47C5E8))->vtable[2]. */
uint32_t scene_target(void)
{
    const uint32_t obj = vm_read32(kSceneObjPtr);
    if (!obj) return 0;
    const uint32_t vt = vm_read32(obj);
    if (!vt) return 0;
    const uint32_t opd = vm_read32(vt + 8u);
    if (!opd) return 0;
    return vm_read32(opd);
}

void add(int slot, uint64_t us)
{
#ifdef PORT_DBZ3_LOOP_METRICS
    g_w.sum[slot] += us;
    if (us > g_w.max[slot]) g_w.max[slot] = us;
#else
    (void)slot; (void)us;
#endif
}


void flush(uint64_t now)
{
#ifndef PORT_DBZ3_LOOP_METRICS
    (void)now;
    return;
#else
    if (!g_log) g_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t26-loop-prof.log", "w");
    if (!g_log || g_w.frames == 0) return;
    const double n = (double)g_w.frames;
    fprintf(g_log, "adx_sur_calls_total=%llu\n", (unsigned long long)g_dbz3_adx_sur_calls);
    fprintf(g_log, "window_ms=%llu passes=%llu pass_avg_us=%.0f pass_max_us=%llu\n",
            (unsigned long long)lp_us(now - g_window_start),
            (unsigned long long)g_w.frames, (double)g_w.frame_sum / n,
            (unsigned long long)g_w.frame_max);
    for (int i = 0; i < S_COUNT; ++i)
        fprintf(g_log, "  %-7s avg_us=%7.0f max_us=%6llu\n", kNames[i],
                (double)g_w.sum[i] / n, (unsigned long long)g_w.max[i]);
    for (int i = 0; i < kMaxTargets && g_w.target[i]; ++i)
        fprintf(g_log, "  scene_fn=0x%08X passes=%llu avg_us=%.0f\n", g_w.target[i],
                (unsigned long long)g_w.target_frames[i],
                (double)g_w.target_us[i] / (double)g_w.target_frames[i]);
    if (g_w.task_passes) {
        const double tp = (double)g_w.task_passes;
        for (int pass = 0; pass < 1; ++pass) {
            fprintf(g_log, "  du-phase");
            for (int phase = 0; phase < 3; ++phase) {
                const uint64_t runs = g_w.du_phase_runs[pass][phase];
                fprintf(g_log, " p%d=%llu/%.0fus/%.1fkb", phase,
                        (unsigned long long)runs,
                        runs ? (double)g_w.du_phase_us[pass][phase] / (double)runs : 0.0,
                        runs ? (double)g_w.du_phase_gcm[pass][phase] / 1024.0 / (double)runs : 0.0);
            }
            fputc('\n', g_log);
        }
#ifdef PORT_DBZ3_LEVEL_FUNCTION_PROFILE
        fprintf(g_log, "  level30790 calls=%llu total_us=%llu avg_us=%.1f max_us=%llu\n",
                (unsigned long long)g_w.level30790_calls,
                (unsigned long long)lp_us(g_w.level30790_ticks),
                g_w.level30790_calls ?
                    (double)g_w.level30790_ticks / (double)g_w.level30790_calls / 19.2 : 0.0,
                (unsigned long long)lp_us(g_w.level30790_max_ticks));
#endif
        fprintf(g_log, "  tasks passes=%llu\n", (unsigned long long)g_w.task_passes);
        for (int i = 0; i < kMaxTasks && g_w.tasks[i].fn; ++i) {
            const TaskStat& t = g_w.tasks[i];
            fprintf(g_log,
                    "  task fn=0x%08X obj=0x%08X order=%2u runs/pass=%.2f avg_us=%6.0f"
                    " max_us=%6llu gcm_runs=%llu gcm_kb/pass=%.1f\n",
                    t.fn, t.task, t.order, (double)t.runs / tp,
                    t.runs ? (double)t.us / (double)t.runs : 0.0,
                    (unsigned long long)t.max_us, (unsigned long long)t.gcm_runs,
                    (double)t.gcm_bytes / 1024.0 / tp);
        }
    }
    fflush(g_log);
    g_w = Window{};
    g_window_start = now;
#endif
}

/* Start of the exit check: the scene call has just returned. */
void on_pass_start(void)
{
#ifdef PORT_DBZ3_LOOP_METRICS
    const uint64_t now = lp_ticks();
    if (!g_window_start) g_window_start = now;
    const uint32_t target = scene_target();
    if (target != g_last_target) {
        if (!g_log) g_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t26-loop-prof.log", "w");
        if (g_log) {
            fprintf(g_log, "scene_change 0x%08X -> 0x%08X\n", g_last_target, target);
            fflush(g_log);
        }
        g_last_target = target;
    }
    if (g_flip_end && g_pass_start) {
        const uint64_t scene_us = lp_us(now - g_flip_end);
        const uint64_t pass_us = lp_us(now - g_pass_start);
        add(S_SCENE, scene_us);
        g_w.frames++;
        g_w.frame_sum += pass_us;
        if (pass_us > g_w.frame_max) g_w.frame_max = pass_us;
        for (int i = 0; i < kMaxTargets; ++i) {
            if (g_w.target[i] == target || g_w.target[i] == 0) {
                g_w.target[i] = target;
                g_w.target_frames[i]++;
                g_w.target_us[i] += scene_us;
                break;
            }
        }
    }
    g_pass_start = now;
    if (lp_us(now - g_window_start) >= 5000000u) flush(now);
#endif
}

uint32_t gcm_current(void) { return vm_read32(kTitleGcmCtx + 8u); }

/* Bytes the title wrote to its command ring between two `current` values. */
uint32_t gcm_written(uint32_t before, uint32_t after)
{
    if (after >= before) return after - before;
    const uint32_t begin = vm_read32(kTitleGcmCtx + 0u);
    const uint32_t end = vm_read32(kTitleGcmCtx + 4u);
    return (end - before) + (after - begin);     /* wrapped once */
}

/* A diagnostic only: identify which part of the mixed DU task prepares GCM
 * commands. The compiler rewrites only func_001DBC58 to call this at entry,
 * the common tail, after its first tail block, and before return. No guest
 * registers or memory are changed. Phase 0 spans entry -> loc_001DE4F4,
 * phase 1 spans that label -> the call at 001DE598, phase 2 -> return. */
uint64_t g_du_phase_tick = 0;
uint32_t g_du_phase_gcm = 0;
int g_du_phase_expected = 0;

extern "C" void dbz3_du_phase_probe(uint32_t phase)
{
#ifndef PORT_DBZ3_LOOP_METRICS
    (void)phase;
    return;
#else
    if (g_dbz3_cur_task_fn != 0x001DBC58u ||
        g_dbz3_cur_pass != 1) return;
    const uint64_t tick = lp_ticks();
    const uint32_t gcm = gcm_current();
    if (phase == 0) {
        g_du_phase_tick = tick;
        g_du_phase_gcm = gcm;
        g_du_phase_expected = 1;
        return;
    }
    if ((int)phase != g_du_phase_expected || phase > 3) {
        g_du_phase_expected = 0;
        return;
    }
    const int pass = g_dbz3_cur_pass - 1;
    const int bucket = (int)phase - 1;
    g_w.du_phase_runs[pass][bucket]++;
    g_w.du_phase_us[pass][bucket] += lp_us(tick - g_du_phase_tick);
    g_w.du_phase_gcm[pass][bucket] += gcm_written(g_du_phase_gcm, gcm);
    g_du_phase_tick = tick;
    g_du_phase_gcm = gcm;
    g_du_phase_expected++;
#endif
}

/*
 * Frame limiter (debug menu FRAME CAP). The title advances its logic one step
 * per image, as on PS3, where the display held it to 60 images/s. Here nothing
 * else waits (run234: up to ~120 images/s, so the game ran fast), so sleep just
 * before the flip until the next slot: 60 -> 16.7 ms, 30 -> 33.3 ms (the game
 * then runs at half speed), 0 = off (no limit: tests only).
 */
uint64_t g_pace_target = 0;      /* ticks of the current slot */
double g_pace_work_ema = 0.0;    /* work since the previous flip, for the log */
uint64_t g_pace_sleep_us = 0, g_pace_late = 0;

void pace_before_flip(void)
{
    const uint64_t now = lp_ticks();
    if (g_flip_end) {
        const double work = (double)lp_us(now - g_flip_end);
        g_pace_work_ema = g_pace_work_ema == 0.0 ? work : g_pace_work_ema * 0.9 + work * 0.1;
    }
    const int cap = switch_debug_frame_cap ? switch_debug_frame_cap() : 60;
    if (cap == 0) {
        g_pace_target = 0;
    } else {
        const uint64_t interval_us = cap == 30 ? 33333u : 16667u;
        const uint64_t interval = interval_us * 19u + interval_us / 5u;   /* us -> 19.2 MHz ticks */
        const uint64_t target = g_pace_target + interval;
        if (!g_pace_target || now > target + interval) {   /* start, or far behind (load, stall) */
            if (g_pace_target) g_pace_late++;
            g_pace_target = now;
        } else {
            if (now < target) {
                const uint64_t us = lp_us(target - now);
                g_pace_sleep_us += us;
#ifdef __SWITCH__
                svcSleepThread((int64_t)us * 1000);
#endif
            } else {
                g_pace_late++;
            }
            g_pace_target = target;
        }
    }

    static uint32_t n = 0;
    if (++n % 150u == 0 && g_log) {
        fprintf(g_log, "  pace cap=%d work_ema_us=%.0f sleep_us/frame=%.0f late=%llu/150\n",
                cap, g_pace_work_ema, (double)g_pace_sleep_us / 150.0,
                (unsigned long long)g_pace_late);
        /* End-of-frame wait 0x002909C0 (dbz3_native_math.cpp): ms per image
         * in total, before the FIFO reached the user command, and after it. */
        fprintf(g_log, "  fwait waits=%llu ms/frame=%.2f fifo=%.2f deliver=%.2f max_us=%llu\n",
                (unsigned long long)g_fw_waits, (double)lp_us(g_fw_wait_ticks) / 150000.0,
                (double)lp_us(g_fw_fifo_ticks) / 150000.0, (double)lp_us(g_fw_deliver_ticks) / 150000.0,
                (unsigned long long)lp_us(g_fw_max_ticks));
        g_fw_waits = g_fw_wait_ticks = g_fw_fifo_ticks = g_fw_deliver_ticks = g_fw_max_ticks = 0;
        g_pace_sleep_us = 0;
        g_pace_late = 0;
    }
}

/* Task list: head at 0x47BC78 + 0x28, sentinel 0x47BCB8, next = task + 0x28. */
constexpr uint32_t kTaskListHead = 0x0047BCA0u;
constexpr uint32_t kTaskListEnd = 0x0047BCB8u;

#ifdef PORT_DBZ3_CRASH_TRAIL
/* Crash trail: hand the sorted set of task functions to dbz3_crash_trail.cpp
 * when it changes (it tells menu, duel, DU map and cutscenes apart). */
void trail_scan_tasks(void)
{
    static uint64_t last_sig = 0;
    uint32_t fns[64];
    int n = 0;
    uint32_t task = vm_read32(kTaskListHead);
    for (int guard = 0; task != kTaskListEnd && task != 0 && guard < 4096; ++guard) {
        const uint32_t opd = vm_read32(task + 0x1Cu);
        if (opd != 0 && opd != 0xFFFFFFFFu && n < 64) {
            const uint32_t fn = vm_read32(opd);
            int i = n;
            while (i > 0 && fns[i - 1] > fn) --i;
            if (i == 0 || fns[i - 1] != fn) {
                memmove(fns + i + 1, fns + i, sizeof(uint32_t) * (size_t)(n - i));
                fns[i] = fn;
                ++n;
            }
        }
        task = vm_read32(task + 0x28u);
    }
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; ++i) h = (h ^ fns[i]) * 1099511628211ull;
    if (h != last_sig) {
        last_sig = h;
        dbz3_trail_tasks(fns, n);
    }
}
#endif

void record_task(uint32_t fn, uint32_t task, uint32_t order, uint64_t us,
                 uint32_t gcm_bytes)
{
#ifndef PORT_DBZ3_LOOP_METRICS
    (void)task; (void)order; (void)us;
    return;
#else
    for (int i = 0; i < kMaxTasks; ++i) {
        TaskStat& t = g_w.tasks[i];
        if (t.fn != fn && t.fn != 0) continue;
        t.fn = fn;
        t.task = task;
        t.order = order;
        t.runs++;
        t.us += us;
        if (us > t.max_us) t.max_us = us;
        if (gcm_bytes) { t.gcm_runs++; t.gcm_bytes += gcm_bytes; }
        return;
    }
#endif
}

} // namespace

#ifdef PORT_DBZ3_LEVEL_FUNCTION_PROFILE
extern "C" uint64_t dbz3_level30790_begin(void) { return lp_ticks(); }
extern "C" void dbz3_level30790_end(uint64_t start)
{
    if (g_dbz3_cur_task_fn != 0x001D1604u) return;
    const uint64_t ticks = lp_ticks() - start;
    g_w.level30790_calls++;
    g_w.level30790_ticks += ticks;
    if (ticks > g_w.level30790_max_ticks) g_w.level30790_max_ticks = ticks;
}
#endif

/* One wrapper per callee; the lifted original is __real__Z13func_XXXXXXXX.
 * Only calls made by the main loop count (main sets ctx->lr to the return
 * address before each call); the same functions called from elsewhere -- for
 * example from inside the scene call -- run untouched. */
/* T43 input diagnostic: the title's pad records (0x47A400..0x47A900, written
 * by func_00024588/func_00023CD0) after every pad poll. One line per poll that changed something, with the
 * changed words, to t30-padgame.log (up to 6000 lines). Matched against the
 * host presses in t21-input.log it shows where a press stops. */
constexpr uint32_t kPadRecBase = 0x0047A400u, kPadRecWords = 0x500u / 4u;
uint32_t g_padrec[kPadRecWords];
int g_padrec_init = 0;
FILE* g_padrec_log = nullptr;
uint32_t g_padrec_lines = 0;

void pad_rec_trace(int pass)
{
#ifndef PORT_DBZ3_LOOP_METRICS
    (void)pass;
    return;
#else
    if (g_padrec_lines >= 6000u) return;
    uint32_t cur[kPadRecWords];
    for (uint32_t i = 0; i < kPadRecWords; ++i) cur[i] = vm_read32(kPadRecBase + i * 4u);
    if (!g_padrec_init) {
        memcpy(g_padrec, cur, sizeof cur);
        g_padrec_init = 1;
        return;
    }
    char buf[1024];
    int n = 0, changed = 0;
    for (uint32_t i = 0; i < kPadRecWords && n < (int)sizeof buf - 40; ++i) {
        if (cur[i] == g_padrec[i]) continue;
        if (changed++ < 24)
            n += snprintf(buf + n, sizeof buf - n, " %03X:%08X>%08X", i * 4u, g_padrec[i], cur[i]);
    }
    memcpy(g_padrec, cur, sizeof cur);
    if (!changed) return;
    if (!g_padrec_log) g_padrec_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t30-padgame.log", "w");
    if (!g_padrec_log) return;
    const uint64_t ms = lp_us(lp_ticks()) / 1000u;
    fprintf(g_padrec_log, "ms=%llu pass=%d frame=%llu n=%d%s\n", (unsigned long long)ms, pass,
            (unsigned long long)g_w.task_passes, changed, buf);
    if (++g_padrec_lines % 16u == 0) fflush(g_padrec_log);
#endif
}

#define LOOP_WRAP(ADDR, SLOT, LR, PRE, POST)                                    \
    extern "C" void real_func_##ADDR(ppu_context* ctx)                          \
        __asm__("__real__Z13func_" #ADDR "P11ppu_context");                     \
    extern "C" void wrap_func_##ADDR(ppu_context* ctx)                          \
        __asm__("__wrap__Z13func_" #ADDR "P11ppu_context");                     \
    extern "C" void wrap_func_##ADDR(ppu_context* ctx)                          \
    {                                                                           \
        if ((uint32_t)ctx->lr != (LR)) { real_func_##ADDR(ctx); return; }       \
        PRE;                                                                    \
        const uint64_t t0 = metric_ticks();                                     \
        real_func_##ADDR(ctx);                                                  \
        DRAIN_TRAMPOLINE(ctx);                                                  \
        const uint64_t t1 = metric_ticks();                                     \
        add(SLOT, lp_us(t1 - t0));                                              \
        POST;                                                                   \
    }

#define NOTHING (void)0

LOOP_WRAP(0004883C, S_4883C, 0x0015A1C4u, on_pass_start(), NOTHING)
LOOP_WRAP(00048088, S_48088, 0x0015A1D8u, NOTHING, NOTHING)
LOOP_WRAP(0002E958, S_2E958, 0x0015A1E4u, NOTHING, NOTHING)
LOOP_WRAP(0001176C, S_1176C, 0x0015A1ECu, NOTHING, NOTHING)
LOOP_WRAP(0001C000, S_1C000, 0x0015A23Cu, NOTHING, NOTHING)
LOOP_WRAP(00015B10, S_15B10, 0x0015A244u, NOTHING, NOTHING)
LOOP_WRAP(0001573C, S_1573C, 0x0015A24Cu, NOTHING, NOTHING)
LOOP_WRAP(0001F318, S_1F318, 0x0015A254u, NOTHING, NOTHING)
LOOP_WRAP(00024588, S_24588, 0x0015A25Cu, NOTHING, pad_rec_trace(1))
LOOP_WRAP(00015EA8, S_15EA8, 0x0015A264u, NOTHING, NOTHING)
LOOP_WRAP(0001745C, S_1745C, 0x0015A26Cu, NOTHING, NOTHING)
LOOP_WRAP(001F9DC0, S_1F9DC0, 0x0015A27Cu, NOTHING, NOTHING)
LOOP_WRAP(000287F4, S_287F4, 0x0015A284u, NOTHING, NOTHING)
LOOP_WRAP(00015AB8, S_15AB8, 0x0015A290u, NOTHING, NOTHING)
LOOP_WRAP(00015C54, S_15C54, 0x0015A298u, NOTHING, NOTHING)
LOOP_WRAP(0002E9A4, S_2E9A4, 0x0015A2A4u, NOTHING, NOTHING)
LOOP_WRAP(000487FC, S_487FC, 0x0015A2BCu, pace_before_flip(), g_flip_end = lp_ticks())

/*
 * func_00028FD8: the title's task scheduler, ~30 ms of every fight frame.
 *   list head  *(0x47BC78 + 0x28), sentinel 0x47BCB8, next = task + 0x28
 *   per task   t0 = func_002988B0() (system time); if task+0x1C (OPD) is not
 *              0 / -1, call it with r3 = task; then stats at
 *              task + *(0x47BCF8) + 0x40: +8 last time, +0xC max time
 *   then       tasks whose OPD is -1 are unlinked and put on the free ring.
 * Below is the lifted body, unchanged, with timing and a GCM-write check
 * around the task call.
 */
extern "C" void ps3_indirect_call(ppu_context* ctx);

extern "C" void real_func_00028FD8(ppu_context* ctx)
    __asm__("__real__Z13func_00028FD8P11ppu_context");
extern "C" void wrap_func_00028FD8(ppu_context* ctx)
    __asm__("__wrap__Z13func_00028FD8P11ppu_context");
extern "C" void wrap_func_00028FD8(ppu_context* ctx)
{
    if ((uint32_t)ctx->lr != 0x0015A274u) { real_func_00028FD8(ctx); return; }
    const uint64_t sched_t0 = metric_ticks();
    uint32_t order = 0;
    g_w.task_passes++;

        uint64_t _cs_28 = ctx->gpr[28];
        uint64_t _cs_29 = ctx->gpr[29];
        uint64_t _cs_30 = ctx->gpr[30];
        uint64_t _cs_31 = ctx->gpr[31];
        vm_write64(ctx->gpr[1] + -0xA0, ctx->gpr[1]); ctx->gpr[1] += -0xA0;
        ctx->gpr[0] = ctx->lr;
        vm_write64(ctx->gpr[1] + 0xB0, ctx->gpr[0]);
        vm_write64(ctx->gpr[1] + 0x88, ctx->gpr[29]);
        vm_write64(ctx->gpr[1] + 0x98, ctx->gpr[31]);
        ctx->gpr[29] = (int64_t)(int32_t)((uint32_t)0x48 << 16);
        ctx->gpr[31] = (int64_t)(int32_t)((uint32_t)0x48 << 16);
        { uint64_t _a = ctx->gpr[29]; uint64_t _r = _a + (uint64_t)(int64_t)(-17288); ctx->gpr[29] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        { uint64_t _a = ctx->gpr[31]; uint64_t _r = _a + (uint64_t)(int64_t)(-17224); ctx->gpr[31] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        vm_write64(ctx->gpr[1] + 0x90, ctx->gpr[30]);
        ctx->gpr[3] = vm_read32(ctx->gpr[29] + 0x28);
        ctx->gpr[30] = ctx->gpr[3] | 0x0;
        vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[28]);
        { int64_t a = (int32_t)ctx->gpr[30]; int64_t b = (int32_t)ctx->gpr[31]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 2)) goto loc_000290A0;
loc_00029014:
        ctx->lr = 0x00029018; func_002988B0(ctx); DRAIN_TRAMPOLINE(ctx);
        vm_write32(ctx->gpr[1] + 0x70, ctx->gpr[3]);
        ctx->gpr[3] = vm_read32(ctx->gpr[30] + 0x1C);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 2)) goto loc_00029054;
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)-1; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 2)) goto loc_00029054;
        {
            const uint32_t task = (uint32_t)ctx->gpr[30];
            const uint32_t fn = vm_read32((uint32_t)ctx->gpr[3]);
            const uint32_t g0 = gcm_current();
            const uint64_t t0 = metric_ticks();
        ctx->gpr[4] = ctx->gpr[3] | 0x0;
        ctx->gpr[3] = ctx->gpr[30] | 0x0;
        ctx->gpr[5] = vm_read32(ctx->gpr[4] + 0x0);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ctx->ctr = (uint32_t)ctx->gpr[5];
        ctx->gpr[2] = vm_read32(ctx->gpr[4] + 0x4);
        g_dbz3_cur_task_fn = fn; g_dbz3_cur_pass = 1;
        { ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx); }
        g_dbz3_cur_task_fn = 0; g_dbz3_cur_pass = 0;
        ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
            const uint64_t t1 = metric_ticks();
            record_task(fn, task, order, lp_us(t1 - t0), gcm_written(g0, gcm_current()));
        }
loc_00029054:
        order++;
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0x48 << 16);
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + -0x4308);
        { uint64_t result = ctx->gpr[30] + ctx->gpr[3]; ctx->xer = (ctx->xer & ~(1u << 29)) | ((result < ctx->gpr[30]) ? (1u << 29) : 0); ctx->gpr[3] = result; }
        { uint64_t _a = ctx->gpr[3]; uint64_t _r = _a + (uint64_t)(int64_t)(0x40); ctx->gpr[28] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->gpr[28] = ctx->gpr[28] & 0xFFFFFFFFull;               /* rldicl 0,32 */
        ctx->lr = 0x0002906C; func_002988B0(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[4] = vm_read32(ctx->gpr[1] + 0x70);
        { uint64_t result = ~ctx->gpr[4] + ctx->gpr[3] + 1; ctx->xer = (ctx->xer & ~(1u << 29)) | ((ctx->gpr[3] >= ctx->gpr[4]) ? (1u << 29) : 0); ctx->gpr[4] = result; }
        ctx->gpr[3] = vm_read32(ctx->gpr[28] + 0xC);
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int32_t)ctx->gpr[4]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        vm_write32(ctx->gpr[28] + 0x8, ctx->gpr[4]);
        if ((!((ctx->cr >> 28) & 8))) goto loc_0002908C;
        ctx->gpr[3] = ctx->gpr[4] | 0x0;
loc_0002908C:
        ctx->gpr[30] = vm_read32(ctx->gpr[30] + 0x28);
        vm_write32(ctx->gpr[28] + 0xC, ctx->gpr[3]);
        { int64_t a = (int32_t)ctx->gpr[30]; int64_t b = (int32_t)ctx->gpr[31]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if ((!((ctx->cr >> 28) & 2))) goto loc_00029014;
        ctx->gpr[3] = vm_read32(ctx->gpr[29] + 0x28);
loc_000290A0:
        ctx->gpr[30] = ctx->gpr[3] | 0x0;
        { int64_t a = (int32_t)ctx->gpr[30]; int64_t b = (int32_t)ctx->gpr[31]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if (((ctx->cr >> 28) & 2)) goto loc_0002911C;
        ctx->gpr[6] = (int64_t)(int32_t)((uint32_t)0x41 << 16);
        ctx->gpr[5] = (int64_t)(int32_t)((uint32_t)0x48 << 16);
        ctx->gpr[4] = (int64_t)(int32_t)((uint32_t)0x48 << 16);
        ctx->gpr[3] = (int64_t)(int32_t)((uint32_t)0x48 << 16);
loc_000290BC:
        ctx->gpr[7] = vm_read32(ctx->gpr[30] + 0x1C);
        { int64_t a = (int32_t)ctx->gpr[7]; int64_t b = (int64_t)-1; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if ((!((ctx->cr >> 28) & 2))) goto loc_00029110;
        ctx->gpr[7] = vm_read32(ctx->gpr[30] + 0x24);
        ctx->gpr[8] = vm_read32(ctx->gpr[30] + 0x28);
        ctx->gpr[9] = vm_read32(ctx->gpr[5] + -0x438C);
        ctx->gpr[10] = vm_read32(ctx->gpr[4] + -0x4304);
        vm_write32(ctx->gpr[7] + 0x28, ctx->gpr[8]);
        { uint64_t _a = ctx->gpr[10]; uint64_t _r = _a + (uint64_t)(int64_t)(-1); ctx->gpr[10] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->gpr[8] = vm_read32(ctx->gpr[30] + 0x28);
        ctx->gpr[7] = (uint64_t)(((uint32_t)ctx->gpr[9] << 2) & 0xFFFFFFFCu);   /* rlwinm 2,0,29 */
        { uint64_t _a = ctx->gpr[9]; uint64_t _r = _a + (uint64_t)(int64_t)(1); ctx->gpr[9] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->gpr[11] = vm_read32(ctx->gpr[30] + 0x24);
        ctx->gpr[12] = vm_read32(ctx->gpr[3] + -0x4394);
        ctx->gpr[9] = ctx->gpr[9] & ctx->gpr[10];
        vm_write32(ctx->gpr[8] + 0x24, ctx->gpr[11]);
        { uint64_t _a = ctx->gpr[12]; uint64_t _r = _a + (uint64_t)(int64_t)(-1); ctx->gpr[8] = _r; ctx->xer = (ctx->xer & ~(1u << 29)) | ((_r < _a) ? (1u << 29) : 0u); }
        ctx->gpr[10] = vm_read32(ctx->gpr[6] + -0x3478);
        vm_write32(ctx->gpr[5] + -0x438C, ctx->gpr[9]);
        vm_write32(ctx->gpr[3] + -0x4394, ctx->gpr[8]);
        vm_write32((ctx->gpr[10] + ctx->gpr[7]), ctx->gpr[30]);
loc_00029110:
        ctx->gpr[30] = vm_read32(ctx->gpr[30] + 0x28);
        { int64_t a = (int32_t)ctx->gpr[30]; int64_t b = (int32_t)ctx->gpr[31]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }
        if ((!((ctx->cr >> 28) & 2))) goto loc_000290BC;
loc_0002911C:
        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xB0);
        ctx->lr = ctx->gpr[0];
        ctx->gpr[28] = _cs_28;
        ctx->gpr[29] = _cs_29;
        ctx->gpr[30] = _cs_30;
        ctx->gpr[31] = _cs_31;
        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0xA0);

#ifdef PORT_DBZ3_CRASH_TRAIL
    trail_scan_tasks();
    dbz3_tlsf_walk();   /* dbz3_tlsf_guard.cpp: damaged heap headers */
#endif
    add(S_28FD8, lp_us(metric_ticks() - sched_t0));
}
