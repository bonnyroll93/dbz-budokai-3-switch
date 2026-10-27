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
 * The game advances its logic one step per pass, so at 30 FPS it runs at
 * half speed. This measures what each step costs, to find the update/draw
 * split for "2 logic steps per drawn frame".
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
static inline uint64_t lp_us(uint64_t t) { return t / 19u; } /* 19.2 MHz */
#else
#include <chrono>
static inline uint64_t lp_ticks(void)
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static inline uint64_t lp_us(uint64_t t) { return t; }
#endif

extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);

extern "C" int ps3_diag_enabled;   /* cellGcmSys.c, from diag.on at boot */
extern "C" uint64_t g_dbz3_adx_sur_calls;   /* dbz3_audio_trace.cpp */

/* Scheduler task running now, for the allocator trace (dbz3_heap_trace.cpp). */
extern "C" { uint32_t g_dbz3_cur_task_fn = 0; int g_dbz3_cur_pass = 0; }
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
    uint64_t steps_hist[5];        /* passes that ran 1/2/3/4 logic steps */
    uint64_t extra_us, extra_max_us, extra_tasks;
    uint64_t gated_load, gated_list, aborted;   /* extra steps not run */
    uint64_t rd_runs, rd_rolled, rd_kb, rd_noroom, rd_flushed, rd_wrapped;  /* T44 */
    uint64_t du_phase_runs[2][3], du_phase_us[2][3], du_phase_gcm[2][3];
    uint64_t du_render_skipped;
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
    g_w.sum[slot] += us;
    if (us > g_w.max[slot]) g_w.max[slot] = us;
}

void flush(uint64_t now)
{
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
        const uint64_t steps = g_w.steps_hist[1] + 2u * g_w.steps_hist[2] +
                               3u * g_w.steps_hist[3] + 4u * g_w.steps_hist[4];
        const uint64_t extra_steps = g_w.steps_hist[2] + 2u * g_w.steps_hist[3] +
                                     3u * g_w.steps_hist[4];
        fprintf(g_log, "  logic60 steps/s=%.1f passes_1/2/3/4=%llu/%llu/%llu/%llu"
                       " extra_avg_us=%.0f extra_max_us=%llu extra_tasks/step=%.1f\n",
                (double)steps * 1000000.0 / (double)lp_us(now - g_window_start),
                (unsigned long long)g_w.steps_hist[1], (unsigned long long)g_w.steps_hist[2],
                (unsigned long long)g_w.steps_hist[3], (unsigned long long)g_w.steps_hist[4],
                extra_steps ? (double)g_w.extra_us / (double)extra_steps : 0.0,
                (unsigned long long)g_w.extra_max_us,
                extra_steps ? (double)g_w.extra_tasks / (double)extra_steps : 0.0);
        fprintf(g_log, "  logic60 deferred: loading=%llu task_list_changed=%llu aborted=%llu\n",
                (unsigned long long)g_w.gated_load, (unsigned long long)g_w.gated_list,
                (unsigned long long)g_w.aborted);
        fprintf(g_log, "  repeat-draw: runs=%llu rolled_back=%llu kb=%llu no_room=%llu flushed=%llu wrapped=%llu\n",
                (unsigned long long)g_w.rd_runs, (unsigned long long)g_w.rd_rolled,
                (unsigned long long)g_w.rd_kb, (unsigned long long)g_w.rd_noroom,
                (unsigned long long)g_w.rd_flushed, (unsigned long long)g_w.rd_wrapped);
        for (int pass = 0; pass < 2; ++pass) {
            fprintf(g_log, "  du-phase pass=%s", pass ? "extra" : "regular");
            for (int phase = 0; phase < 3; ++phase) {
                const uint64_t runs = g_w.du_phase_runs[pass][phase];
                fprintf(g_log, " p%d=%llu/%.0fus/%.1fkb", phase,
                        (unsigned long long)runs,
                        runs ? (double)g_w.du_phase_us[pass][phase] / (double)runs : 0.0,
                        runs ? (double)g_w.du_phase_gcm[pass][phase] / 1024.0 / (double)runs : 0.0);
            }
            fputc('\n', g_log);
        }
        fprintf(g_log, "  du-render-skipped=%llu\n",
                (unsigned long long)g_w.du_render_skipped);
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
                    t.fn, t.task, t.order, (double)t.runs / tp,   /* regular pass only */
                    t.runs ? (double)t.us / (double)t.runs : 0.0,
                    (unsigned long long)t.max_us, (unsigned long long)t.gcm_runs,
                    (double)t.gcm_bytes / 1024.0 / tp);
        }
    }
    fflush(g_log);
    g_w = Window{};
    g_window_start = now;
}

/* Start of the exit check: the scene call has just returned. */
void on_pass_start(void)
{
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
    if (g_dbz3_cur_task_fn != 0x001DBC58u ||
        (g_dbz3_cur_pass != 1 && g_dbz3_cur_pass != 2)) return;
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
}

/* Task classes for the 60 Hz logic step: a task function that has ever moved
 * the title's GCM write pointer draws; one seen only without GCM writes is
 * logic. Persistent (not reset per log window). */
enum { CLS_UNKNOWN = 0, CLS_LOGIC = 1, CLS_DRAW = 2 };
constexpr int kClassSlots = 512;                  /* power of two */
uint32_t g_cls_fn[kClassSlots];
uint8_t g_cls[kClassSlots];

int class_slot(uint32_t fn)
{
    uint32_t h = (fn >> 2) * 2654435761u;
    for (int i = 0; i < kClassSlots; ++i) {
        const int s = (int)((h + (uint32_t)i) & (kClassSlots - 1));
        if (g_cls_fn[s] == fn || g_cls_fn[s] == 0) return s;
    }
    return -1;
}

void classify(uint32_t fn, int drew)
{
    const int s = class_slot(fn);
    if (s < 0) return;
    g_cls_fn[s] = fn;
    if (drew) g_cls[s] = CLS_DRAW;
    else if (g_cls[s] == CLS_UNKNOWN) g_cls[s] = CLS_LOGIC;
}

int task_class(uint32_t fn)
{
    const int s = class_slot(fn);
    return (s < 0 || g_cls_fn[s] != fn) ? CLS_UNKNOWN : g_cls[s];
}

/* Real-time accumulator: logic steps of 1/60 s owed since the last pass. */
constexpr uint64_t kStepUs = 16667u;
constexpr int kMaxSteps = 4;   /* G1: frames up to ~67 ms still keep real time */
/* T43: time owed after a stall is kept up to 0.5 s and paid back at up to
 * kMaxSteps steps per frame. With the old 3-step cap every 100-400 ms hitch
 * (cutscene shot changes load models and textures) threw away 3-20 steps for
 * good, so cutscene animation fell further behind the real-time voice at each
 * cut. Longer stalls (loading screens) still drop the excess. */
constexpr uint64_t kCatchUpUs = 30u * kStepUs;
uint64_t g_step_last = 0;
uint64_t g_step_acc = 0;

/*
 * Frame pacing for logic60 (run29: frames of 25-45 ms gave 1, 2 or 3 logic
 * steps per image -> right average speed, uneven motion). Present at a fixed
 * rate instead: 30 Hz when the frame's own work is above ~18 ms (then every
 * image is 2 logic steps), 60 Hz when it is below ~14 ms. Called just before
 * the flip; sleeps until the next slot.
 */
uint64_t g_pace_target = 0;     /* ticks of the current slot */
uint64_t g_pace_interval_us = 16667;
double g_pace_work_ema = 0.0;
uint64_t g_pace_sleep_us = 0, g_pace_late = 0;

void pace_before_flip(void)
{
    const uint64_t now = lp_ticks();
    if (g_flip_end) {
        const double work = (double)lp_us(now - g_flip_end);
        g_pace_work_ema = g_pace_work_ema == 0.0 ? work : g_pace_work_ema * 0.9 + work * 0.1;
        if (g_pace_work_ema > 18000.0) g_pace_interval_us = 33333;
        else if (g_pace_work_ema < 14000.0) g_pace_interval_us = 16667;
    }
    const uint64_t interval = g_pace_interval_us * 19u + g_pace_interval_us / 5u;   /* us -> 19.2 MHz ticks */
    if (!g_pace_target) { g_pace_target = now; return; }
    uint64_t target = g_pace_target + interval;
    if (now > target + interval) {          /* far behind (load, stall): restart */
        g_pace_late++;
        g_pace_target = now;
        return;
    }
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

    static uint32_t n = 0;
    if (++n % 150u == 0 && g_log) {
        fprintf(g_log, "  pace interval_us=%llu work_ema_us=%.0f sleep_us/frame=%.0f late=%llu/150\n",
                (unsigned long long)g_pace_interval_us, g_pace_work_ema,
                (double)g_pace_sleep_us / 150.0, (unsigned long long)g_pace_late);
        g_pace_sleep_us = 0;
        g_pace_late = 0;
    }
}

int logic_steps_due(void)
{
    const uint64_t now = lp_ticks();
    if (!g_step_last) { g_step_last = now; return 1; }
    g_step_acc += lp_us(now - g_step_last);
    g_step_last = now;
    int steps = (int)(g_step_acc / kStepUs);
    if (steps < 1) steps = 1;
    if (steps > kMaxSteps) steps = kMaxSteps;
    const uint64_t used = (uint64_t)steps * kStepUs;
    g_step_acc = g_step_acc > used ? g_step_acc - used : 0;
    if (g_step_acc > kCatchUpUs) g_step_acc = kCatchUpUs;   /* no spiral after a long stall */
    return steps;
}

/* Steps that were due but not run: owed to the next passes. */
void logic_steps_defer(int steps)
{
    g_step_acc += (uint64_t)steps * kStepUs;
    if (g_step_acc > kCatchUpUs) g_step_acc = kCatchUpUs;
}

/* Gate for the extra logic steps (run25 crash: end of a match, the async
 * loader func_00014AF8 read a freed object). Logic that starts a scene change
 * or a load must not run twice in one frame, so no extra step while
 *   - the loader has requests pending (count at 0x4765E4), or
 *   - the task list changed during the regular pass (tasks created/killed). */
constexpr uint32_t kLoaderPending = 0x004765E4u;
constexpr uint32_t kTaskListHead = 0x0047BCA0u;   /* 0x47BC78 + 0x28 */
constexpr uint32_t kTaskListEnd = 0x0047BCB8u;

uint32_t loader_pending(void) { return vm_read32(kLoaderPending); }

/*
 * Heap check (run25/run26: logic60 crashed in the title allocator
 * func_0001D294 walking its free list into 0x3F800000, a float 1.0 written
 * over a link). The allocator (init at 0x1D090):
 *   *(0x40C820) first block, *(0x40C824) last block (tail sentinel)
 *   block: +0 size, +0x14 / +0x18 link records, +0x1C 0xD2ACD2AC
 *   list A (alloc walk): b = first; b = *(*(b + 0x18) + 0xC) until 0
 *   list B:              b = *(last + 8); b = *(*(b + 0x14) + 8) until 0
 * Every link must stay inside [first, last]. Checked after every task (both
 * passes) and around the loop helpers; the first failure is logged with the
 * task that just ran.
 */
constexpr uint32_t kHeapFirst = 0x0040C820u;
constexpr uint32_t kHeapLast = 0x0040C824u;
constexpr int kHeapWalkMax = 200000;

int g_heap_bad_logged = 0;
uint64_t g_heap_checks = 0;
const char* g_heap_last_ok = "start";
uint32_t g_heap_last_ok_fn = 0;

inline int heap_in(uint32_t p, uint32_t lo, uint32_t hi) { return p >= lo && p <= hi && !(p & 3u); }

/* 0 = fine; otherwise fills the failing link. */
int heap_walk(uint32_t* bad_node, uint32_t* bad_val, int* which)
{
    const uint32_t lo = vm_read32(kHeapFirst), hi = vm_read32(kHeapLast);
    if (!lo || !hi || hi < lo) return 0;          /* not set up yet */
    uint32_t b = lo;
    for (int n = 0; b != 0 && n < kHeapWalkMax; ++n) {
        if (!heap_in(b, lo, hi)) { *which = 1; *bad_node = 0; *bad_val = b; return 1; }
        const uint32_t link = vm_read32(b + 0x18u);
        if (!heap_in(link, lo, hi + 0x20u)) { *which = 2; *bad_node = b; *bad_val = link; return 1; }
        const uint32_t next = vm_read32(link + 0xCu);
        if (next != 0 && !heap_in(next, lo, hi)) { *which = 3; *bad_node = link; *bad_val = next; return 1; }
        b = next;
    }
    b = vm_read32(hi + 8u);
    for (int n = 0; b != 0 && n < kHeapWalkMax; ++n) {
        if (!heap_in(b, lo, hi)) { *which = 4; *bad_node = 0; *bad_val = b; return 1; }
        const uint32_t link = vm_read32(b + 0x14u);
        if (link == 0) break;
        if (!heap_in(link, lo, hi + 0x20u)) { *which = 5; *bad_node = b; *bad_val = link; return 1; }
        const uint32_t next = vm_read32(link + 8u);
        if (next != 0 && !heap_in(next, lo, hi)) { *which = 6; *bad_node = link; *bad_val = next; return 1; }
        b = next;
    }
    return 0;
}

void heap_check(const char* where, uint32_t fn, uint32_t task, int step)
{
    if (!ps3_diag_enabled) return;     /* full heap walk: diagnostics only */
    g_heap_checks++;
    if (g_heap_bad_logged >= 3) return;
    uint32_t node = 0, val = 0;
    int which = 0;
    if (!heap_walk(&node, &val, &which)) {
        g_heap_last_ok = where;
        g_heap_last_ok_fn = fn;
        return;
    }
    if (!g_log) g_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t26-loop-prof.log", "w");
    if (!g_log) return;
    fprintf(g_log, "HEAP_BAD #%d after %s fn=0x%08X task=0x%08X step=%d (last ok: %s fn=0x%08X)"
                   " link_kind=%d node=0x%08X value=0x%08X heap=0x%08X..0x%08X checks=%llu\n",
            g_heap_bad_logged + 1, where, fn, task, step, g_heap_last_ok, g_heap_last_ok_fn,
            which, node, val, vm_read32(kHeapFirst), vm_read32(kHeapLast),
            (unsigned long long)g_heap_checks);
    if (node) {
        const uint32_t base = (node & ~15u) - 0x40u;
        for (uint32_t a = base; a < base + 0xA0u; a += 16u)
            fprintf(g_log, "  %08X: %08X %08X %08X %08X\n", a, vm_read32(a), vm_read32(a + 4u),
                    vm_read32(a + 8u), vm_read32(a + 12u));
    }
    if (val >= 0x00010000u) dbz3_heap_trace_dump(g_log, node ? node : val);
    fflush(g_log);
    g_heap_bad_logged++;
}

uint64_t task_list_sig(void)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t task = vm_read32(kTaskListHead);
    for (int guard = 0; task != kTaskListEnd && task != 0 && guard < 4096; ++guard) {
        h = (h ^ task) * 1099511628211ull;
        h = (h ^ vm_read32(task + 0x1Cu)) * 1099511628211ull;
        task = vm_read32(task + 0x28u);
    }
    return h;
}

void record_task(uint32_t fn, uint32_t task, uint32_t order, uint64_t us,
                 uint32_t gcm_bytes)
{
    classify(fn, gcm_bytes != 0);
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
 * by func_00024588/func_00023CD0) after every pad poll, regular pass (1) and
 * extra logic step (2). One line per poll that changed something, with the
 * changed words, to t30-padgame.log (up to 6000 lines). Matched against the
 * host presses in t21-input.log it shows where a press stops. */
constexpr uint32_t kPadRecBase = 0x0047A400u, kPadRecWords = 0x500u / 4u;
uint32_t g_padrec[kPadRecWords];
int g_padrec_init = 0;
FILE* g_padrec_log = nullptr;
uint32_t g_padrec_lines = 0;

void pad_rec_trace(int pass)
{
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
        const uint64_t t0 = lp_ticks();                                         \
        real_func_##ADDR(ctx);                                                  \
        DRAIN_TRAMPOLINE(ctx);                                                  \
        const uint64_t t1 = lp_ticks();                                         \
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
#ifdef PORT_DBZ3_LOGIC60
#define PACE_PRE pace_before_flip()
#else
#define PACE_PRE NOTHING
#endif
LOOP_WRAP(000487FC, S_487FC, 0x0015A2BCu, PACE_PRE, g_flip_end = lp_ticks())

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

/*
 * T6.2 logic at 60 Hz: the title steps its logic once per scheduler pass and
 * was made for 60 passes/s, so at ~30 FPS it ran at half speed. After the
 * regular pass, for every further 1/60 s of real time owed (logic_steps_due),
 * re-read the pad (func_00024588, the same per-frame poll main does, so a
 * press is not seen twice) and run the logic tasks again: those whose function
 * has never written GCM commands. Draw tasks run once per drawn frame.
 */
/* Logic tasks that must not run twice per drawn frame.
 *   0x000D1490  run27/run28: in the extra pass the heap was always fine right
 *               after it and broken at the next check, whichever task came
 *               next (0x946E8, then 0x97F58), at the same block 0x41120520
 *               (1.0f vectors over a free-list link -> crash in the allocator
 *               func_0001D294). It calls no import: it most likely queues
 *               work another processor writes back asynchronously, into a
 *               per-frame buffer already freed by the time of the extra pass.
 *               (0x946E8, blamed first, was only the next task checked.) */
/* run29: excluding 0x000D1490 did not help (corruption then showed after
 * 0x946E8 again) and made animations stutter, so it runs twice again; the
 * allocator trace (dbz3_heap_trace.cpp) is to find the buffer's owner. */
constexpr uint32_t kNoRepeat[] = { 0u /* none */ };

int no_repeat(uint32_t fn)
{
    for (uint32_t f : kNoRepeat)
        if (f == fn) return 1;
    return 0;
}

constexpr uint32_t kTaskListBase = 0x0047BC78u;   /* head at +0x28 */
constexpr uint32_t kTaskSentinel = 0x0047BCB8u;

/* Returns 0 when it stopped early or changed the task list (no more steps
 * this frame). */
extern "C" uint32_t ppu_hle_inject_base;   /* vm.h: GCM control at +0x2000 */

/* T44: draw tasks that also hold game logic, run again in the extra step with
 * their GCM output thrown away (the title's write pointer is put back).
 *   0x001DBC58  Dragon Universe (map, menus, its character select): one task
 *               that updates and draws, ~17 ms; once per drawn frame it ran
 *               at half speed at 30 FPS, menus included.
 * The repeat is skipped when the ring has less than kRdRoom bytes left
 * before `end`: the run must not wrap. If the run moves `put` (it flushes),
 * its commands are already with the RSX and cannot be discarded; disable
 * further repeats for this process. The same applies if the context moves
 * or wraps. sdmc:/switch/dbz-budokai-3-switch/repeat-draw-off disables it. */
constexpr uint32_t kRepeatDraw[] = { 0x001DBC58u };
constexpr uint32_t kRdRoom = 0x60000u;
bool g_repeat_draw_unsafe = false;

/* run08: entry -> loc_001DE4F4 takes about 0.45 ms and writes zero GCM bytes.
 * The rest takes 15-22 ms and emits all observed GCM output. Keep the update
 * part on extra 60 Hz steps; render the common tail on regular passes only.
 * The SD marker restores the original full extra pass for A/B comparisons. */
int du_update_only_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        FILE* f = fopen("sdmc:/switch/dbz-budokai-3-switch/du-full-extra", "r");
        enabled = f ? 0 : 1;
        if (f) fclose(f);
    }
    return enabled;
}

extern "C" int dbz3_du_skip_render(void)
{
    if (g_dbz3_cur_pass != 2 || !du_update_only_enabled()) return 0;
    g_w.du_render_skipped++;
    return 1;
}

int repeat_draw(uint32_t fn)
{
    static int off = -1;
    if (off < 0) {
        FILE* f = fopen("sdmc:/switch/dbz-budokai-3-switch/repeat-draw-off", "r");
        off = f ? 1 : 0;
        if (f) fclose(f);
    }
    if (off || g_repeat_draw_unsafe) return 0;
    for (uint32_t f : kRepeatDraw)
        if (f == fn) return 1;
    return 0;
}

uint32_t gcm_put(void) { return vm_read32(ppu_hle_inject_base + 0x2000u); }

int extra_logic_step(ppu_context* ctx)
{
    const uint64_t t0 = lp_ticks();
    const uint64_t sig0 = task_list_sig();
    int ok = 1;
    const uint64_t save_r1 = ctx->gpr[1], save_r2 = ctx->gpr[2];
    const uint64_t save_r28 = ctx->gpr[28], save_r29 = ctx->gpr[29];
    const uint64_t save_r30 = ctx->gpr[30], save_r31 = ctx->gpr[31];
    const uint64_t save_lr = ctx->lr;

    /* Same frame layout as func_00028FD8's: back chain, TOC save at +0x28. */
    vm_write64(ctx->gpr[1] + -0xA0, ctx->gpr[1]); ctx->gpr[1] += -0xA0;
    vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);

    /* Read live input on every logic step. Replaying a frozen snapshot caused
     * missed/delayed controls on Switch; the pad-repoll trial resolved them.
     * This is now unconditional and needs no SD marker. The logic60 scheduler
     * still controls timing, while cellPad retains its normal report policy. */
    ctx->lr = 0x0015A25Cu;            /* as from main; the wrapper times it */
    real_func_00024588(ctx); DRAIN_TRAMPOLINE(ctx);
    pad_rec_trace(2);
    heap_check("extra pad poll", 0x00024588u, 0, 2);
    ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);

    uint64_t ran = 0;
    ctx->gpr[29] = kTaskListBase;
    ctx->gpr[31] = kTaskSentinel;
    uint32_t task = vm_read32(kTaskListBase + 0x28u);
    for (int guard = 0; task != kTaskSentinel && task != 0 && guard < 4096; ++guard) {
        const uint32_t opd = vm_read32(task + 0x1Cu);
        if (opd != 0 && opd != 0xFFFFFFFFu) {
            const uint32_t fn = vm_read32(opd);
            const int cls = task_class(fn);
            int rd = cls == CLS_DRAW && repeat_draw(fn);
            uint32_t rd_begin0 = 0, rd_end0 = 0;
            uint32_t rd_cur0 = 0, rd_put0 = 0;
            if (rd) {
                rd_begin0 = vm_read32(kTitleGcmCtx);
                rd_end0 = vm_read32(kTitleGcmCtx + 4u);
                rd_cur0 = gcm_current();
                if (rd_begin0 > rd_cur0 || rd_end0 < rd_cur0 ||
                    (!du_update_only_enabled() && rd_end0 - rd_cur0 < kRdRoom)) {
                    g_w.rd_noroom++;
                    task = vm_read32(task + 0x28u);
                    continue;
                }
                rd_put0 = gcm_put();
            }
            if ((cls == CLS_LOGIC && !no_repeat(fn)) || rd) {
                ctx->gpr[30] = task;
                ctx->gpr[3] = task;
                ctx->gpr[4] = opd;
                ctx->gpr[5] = fn;
                ctx->ctr = fn;
                ctx->gpr[2] = vm_read32(opd + 4u);
                ctx->lr = 0x00029050u;
                g_dbz3_cur_task_fn = fn; g_dbz3_cur_pass = 2;
                ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
                g_dbz3_cur_task_fn = 0; g_dbz3_cur_pass = 0;
                ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
                ++ran;
                if (rd) {
                    g_w.rd_runs++;
                    const uint32_t cur1 = gcm_current();
                    if (gcm_put() != rd_put0) {
                        g_w.rd_flushed++;
                        g_repeat_draw_unsafe = true;
                    } else if (vm_read32(kTitleGcmCtx) != rd_begin0 ||
                               vm_read32(kTitleGcmCtx + 4u) != rd_end0 ||
                               cur1 < rd_cur0 || cur1 > rd_end0) {
                        g_w.rd_wrapped++;
                        g_repeat_draw_unsafe = true;
                    } else {
                        vm_write32(kTitleGcmCtx + 8u, rd_cur0);
                        g_w.rd_rolled++;
                        g_w.rd_kb += (cur1 - rd_cur0) / 1024u;
                    }
                }
                heap_check("extra task", fn, task, 2);
                if (loader_pending() != 0) {       /* this step started a load */
                    g_w.aborted++;
                    ok = 0;
                    break;
                }
            }
        }
        task = vm_read32(task + 0x28u);
    }
    (void)sig0;   /* G1: a changed task list no longer ends the frame's extra steps */

    ctx->gpr[1] = save_r1; ctx->gpr[2] = save_r2;
    ctx->gpr[28] = save_r28; ctx->gpr[29] = save_r29;
    ctx->gpr[30] = save_r30; ctx->gpr[31] = save_r31;
    ctx->lr = save_lr;

    const uint64_t us = lp_us(lp_ticks() - t0);
    g_w.extra_us += us;
    if (us > g_w.extra_max_us) g_w.extra_max_us = us;
    g_w.extra_tasks += ran;
    return ok;
}

extern "C" void real_func_00028FD8(ppu_context* ctx)
    __asm__("__real__Z13func_00028FD8P11ppu_context");
extern "C" void wrap_func_00028FD8(ppu_context* ctx)
    __asm__("__wrap__Z13func_00028FD8P11ppu_context");
extern "C" void wrap_func_00028FD8(ppu_context* ctx)
{
    if ((uint32_t)ctx->lr != 0x0015A274u) { real_func_00028FD8(ctx); return; }
    const uint64_t sched_t0 = lp_ticks();
    uint32_t order = 0;
    g_w.task_passes++;
#ifdef PORT_DBZ3_LOGIC60
    const uint64_t list_sig0 = task_list_sig();
    const uint32_t load0 = loader_pending();
#endif

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
            const uint64_t t0 = lp_ticks();
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
            const uint64_t t1 = lp_ticks();
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

#ifdef PORT_DBZ3_LOGIC60
    {
        const int steps = logic_steps_due();
        int ran = 1;
        if (steps > 1) {
            /* G1: no longer deferred when the task list changed this pass.
             * Cutscenes create and kill tasks almost every frame, so that gate
             * cost them up to ~10% of their logic steps and the animation fell
             * behind the (real-time) voice. It was added for the run25/26
             * crash, whose real cause was the a2d stack underflow, now guarded
             * (dbz3_heap_trace.cpp). The count is still logged. */
            if (task_list_sig() != list_sig0) g_w.gated_list++;
            if (load0 != 0 || loader_pending() != 0) {
                g_w.gated_load++;
            } else {
                while (ran < steps) {
                    const int ok = extra_logic_step(ctx);
                    ++ran;
                    if (!ok) break;
                }
            }
            if (ran < steps) logic_steps_defer(steps - ran);
        }
        g_w.steps_hist[ran]++;
    }
#else
    g_w.steps_hist[1]++;
#endif
    add(S_28FD8, lp_us(lp_ticks() - sched_t0));
}
