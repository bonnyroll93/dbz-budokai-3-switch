/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"
#ifdef __SWITCH__
#include "vm.h"
#endif
#ifdef _WIN32
#include "win32_platform.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The PPU ELF the Switch runner boots. */
#ifndef PORT_GAME_ELF
#define PORT_GAME_ELF "sdmc:/switch/dbz-budokai-3-switch/game/PS3_GAME/USRDIR/LAUNCH/DBZ3.elf"
#endif
#ifdef __SWITCH__
extern "C" int ps3_switch_app_init(void);
extern "C" const char* ps3_switch_game_elf_path(void);
extern "C" void ps3_switch_app_stage(const char *stage);
extern "C" void ps3_switch_app_hold(const char *reason);
extern "C" void ps3_switch_app_prepare_display(void);
extern "C" uint64_t ps3_switch_thread_cpu_ns(void);
extern "C" uint64_t ps3_switch_thread_cpu_ns_for(uint32_t handle);
extern "C" int ps3_thread_stats_start(void);
extern "C" void ps3_thread_stats_register(const char* kind, const char* name);
extern "C" uint32_t ps3_switch_current_thread_handle(void);
extern "C" void spu_taskset_diag_snapshot(FILE* out);
extern "C" void ppu_dump_all_thread_stacks(FILE* out);
static uint32_t g_main_thread_handle = 0;
#endif

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
unsigned spu_workload_count(void);            /* registered SPU image fingerprints */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ps3_blus_register_hle_overrides(void);
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
uint32_t cellGcm_fifo_get_offset(void);
#ifdef __SWITCH__
void     cellGcm_fifo_diag_snapshot(uint64_t out[8]);
#endif
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  if defined(__SWITCH__) && defined(PORT_RSX_DEKO3D)
/* Use deko3d by default on Switch. An rsx-gpu.off marker on the SD card or
 * PS3RECOMP_RSX_GPU=0 selects software; initialization failure also falls back. */
#    include "rsx_deko3d_backend.h"
static int s_rsx_gpu = 0;
static int switch_rsx_backend_init(uint32_t w, uint32_t h, const char* title)
{
    if (rsx_deko3d_backend_requested()) {
        if (rsx_deko3d_backend_init(w, h, title) == 0) {
            s_rsx_gpu = 1;
            return 0;
        }
        fprintf(stderr, "[rsx] deko3d backend failed, using the software backend\n");
    }
    return rsx_null_backend_init(w, h, title);
}
static void switch_rsx_backend_present(void)
{
    if (s_rsx_gpu) rsx_deko3d_backend_present();
    else           rsx_null_backend_present();
}
static int switch_rsx_backend_pump(void)
{
    return s_rsx_gpu ? rsx_deko3d_backend_pump_messages()
                     : rsx_null_backend_pump_messages();
}
#    define rsx_backend_init    switch_rsx_backend_init
#    define rsx_backend_present switch_rsx_backend_present
#    define rsx_backend_pump    switch_rsx_backend_pump
#    define RSX_BACKEND_NAME    (s_rsx_gpu ? "deko3d GPU" : "software -> libnx RGBA8")
#  else
#    define rsx_backend_init    rsx_null_backend_init
#    define rsx_backend_present rsx_null_backend_present
#    define rsx_backend_pump    rsx_null_backend_pump_messages
#    ifdef __SWITCH__
#      define RSX_BACKEND_NAME  "software -> libnx RGBA8"
#    else
#      define RSX_BACKEND_NAME  "null (headless software)"
#    endif
#  endif
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

static uint64_t frame_now_us(void)
{
#ifdef _WIN32
    const Ps3Platform *platform = ps3_platform_win32();
    return platform->time.monotonic_us(platform->time.context);
#else
    return GetTickCount64() * 1000ull;
#endif
}

static uint64_t frame_now_ms(void) { return frame_now_us() / 1000ull; }

static void frame_sleep_ms(uint32_t milliseconds)
{
#ifdef _WIN32
    const Ps3Platform *platform = ps3_platform_win32();
    platform->time.sleep_us(platform->time.context, (uint64_t)milliseconds * 1000);
#else
    Sleep(milliseconds);
#endif
}

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

static void present_guest_frame(void)
{
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    LONG frames = InterlockedIncrement(&g_frames_presented);
    if (frames == 1 || frames == 60)
        fprintf(stderr, "[rsx] guest frame presented #%ld\n", (long)frames);
}

static DWORD WINAPI frame_clock(LPVOID)
{
#ifdef __SWITCH__
    ps3_thread_stats_register("frame", "frame-clock+fifo");
#endif
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK" : "FAILED");
#ifdef __SWITCH__
    ps3_switch_app_stage(rsx_ok ? "Game display ready" : "Game display init failed");
#endif

    unsigned  last_flip = 0;
    uint64_t next_tick_us = frame_now_us();
    unsigned tick_remainder = 0;
#ifdef __SWITCH__
    const uint64_t frame_start_ms = next_tick_us / 1000ull;
    FILE* clock_diag = fopen("sdmc:/switch/dbz-budokai-3-switch/t21-clock.log", "w");
    FILE* heartbeat_diag = fopen("sdmc:/switch/dbz-budokai-3-switch/t21-heartbeat.log", "w");
    FILE* stall_diag = fopen("sdmc:/switch/dbz-budokai-3-switch/t21-stall.log", "w");
    uint64_t clock_window_start = frame_start_ms;
    uint64_t heartbeat_previous_ms = frame_start_ms;
    uint64_t last_progress_ms = frame_start_ms, last_stall_report_ms = 0;
    unsigned last_progress_frames = 0;
    uint64_t tick_ms = 0, fifo_ms = 0, present_ms = 0, sleep_ms = 0;
    uint64_t fifo_cpu_ns = 0, fifo_max_ms = 0, sleep_max_ms = 0;
    uint64_t present_cpu_ns = 0, fifo_diag_prev[8] = {};
    unsigned fifo_calls = 0, sleep_calls = 0, fifo_cpu_valid = 0;
    cellGcm_fifo_diag_snapshot(fifo_diag_prev);
    unsigned last_report_frames = 0;
    uint64_t ppu_cpu_previous_ns = g_main_thread_handle
        ? ps3_switch_thread_cpu_ns_for(g_main_thread_handle) : 0;
    auto timed_fifo_drain = [&]() {
        const uint64_t start_wall = frame_now_ms();
        const uint64_t start_cpu = ps3_switch_thread_cpu_ns();
        cellGcm_rsx_process_fifo();
        const uint64_t end_cpu = ps3_switch_thread_cpu_ns();
        const uint64_t elapsed = frame_now_ms() - start_wall;
        fifo_ms += elapsed;
        if (elapsed > fifo_max_ms) fifo_max_ms = elapsed;
        if (start_cpu && end_cpu >= start_cpu) {
            fifo_cpu_ns += end_cpu - start_cpu;
            fifo_cpu_valid = 1;
        }
        ++fifo_calls;
    };
    auto timed_present = [&]() {
        const uint64_t start_wall = frame_now_ms();
        const uint64_t start_cpu = ps3_switch_thread_cpu_ns();
        present_guest_frame();
        const uint64_t end_cpu = ps3_switch_thread_cpu_ns();
        present_ms += frame_now_ms() - start_wall;
        if (start_cpu && end_cpu >= start_cpu)
            present_cpu_ns += end_cpu - start_cpu;
    };
#endif

    /* T23: 1 ms between drains while the FIFO is moving, 4 ms when idle. A
     * fixed 4 ms left the PPU waiting on frames the drain had not reached. */
    uint32_t idle_get = 0xFFFFFFFFu;
    for (;;) {
#ifdef __SWITCH__
        uint64_t section_start = frame_now_ms();
#endif
        { const uint32_t g = cellGcm_fifo_get_offset();
          frame_sleep_ms(g != idle_get ? 1 : 4);
          idle_get = g; }
#ifdef __SWITCH__
        const uint64_t sleep_elapsed = frame_now_ms() - section_start;
        sleep_ms += sleep_elapsed;
        if (sleep_elapsed > sleep_max_ms) sleep_max_ms = sleep_elapsed;
        ++sleep_calls;
#endif
        uint64_t now_us = frame_now_us();
        uint64_t now = now_us / 1000ull;

        int fired = 0;
        while ((int64_t)(now_us - next_tick_us) >= 0 && fired < 240) {
#ifdef __SWITCH__
            section_start = frame_now_ms();
#endif
            cellGcmTickVBlank();
            cellGcmTickFlip();
#ifdef __SWITCH__
            tick_ms += frame_now_ms() - section_start;
#endif
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
#ifdef __SWITCH__
                timed_present();
#else
                present_guest_frame();
#endif
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) {
#ifdef __SWITCH__
                timed_fifo_drain();
#else
                cellGcm_rsx_process_fifo();
#endif
            }
            next_tick_us += 1000000u / 60u;
            tick_remainder += 1000000u % 60u;
            if (tick_remainder >= 60u) { ++next_tick_us; tick_remainder -= 60u; }
            fired++;
        }
        if (fired >= 240) { next_tick_us = now_us; tick_remainder = 0; }

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
#ifdef __SWITCH__
                timed_present();
#else
                present_guest_frame();
#endif
                last_flip = cellGcm_flip_request_count();
            }
#ifdef __SWITCH__
            timed_fifo_drain();
#else
            cellGcm_rsx_process_fifo();
#endif

            if (rsx_backend_pump() != 0) {
                rsx_ok = 0;              /* window closed */
                continue;
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            if (fc != last_flip) {
#ifdef __SWITCH__
                timed_present();
#else
                present_guest_frame();
#endif
                last_flip = fc;
            } else if (fc == 0) {
#ifndef __SWITCH__
                rsx_backend_present();
#endif
            }
        }
#ifdef __SWITCH__
        unsigned frames_now = (unsigned)g_frames_presented;
        if (frames_now != last_progress_frames) {
            last_progress_frames = frames_now;
            last_progress_ms = now;
        }
        if (stall_diag && frames_now >= 30 && now - last_progress_ms >= 10000 &&
            (!last_stall_report_ms || now - last_stall_report_ms >= 30000)) {
            fprintf(stall_diag, "uptime_ms=%llu stalled_ms=%llu frames=%u flips=%u "
                    "fifo_calls=%u\n",
                    (unsigned long long)(now - frame_start_ms),
                    (unsigned long long)(now - last_progress_ms), frames_now,
                    cellGcm_flip_request_count(), fifo_calls);
            /* BLUS30966's async queue descriptor used by func_00027D08.
             * The PPU waits on descriptor+0x2C; inspect the entire header to
             * distinguish an unsubmitted job from a missing completion. */
            if (vm_base) {
                const uint32_t base = 0x0027F83Cu;
                for (unsigned offset = 0; offset <= 0x2C; offset += 4) {
                    const uint8_t* p = vm_base + base + offset;
                    const uint32_t value = ((uint32_t)p[0] << 24) |
                        ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
                    fprintf(stall_diag, "async+0x%02X=0x%08X\n", offset, value);
                }
            }
            /* T23: where every PPU thread is parked (frame chain + regs). */
            ppu_dump_all_thread_stacks(stall_diag);
            spu_taskset_diag_snapshot(stall_diag);
            fflush(stall_diag);
            last_stall_report_ms = now;
        }
        if (heartbeat_diag && now - heartbeat_previous_ms >= 5000) {
            fflush(stdout);   /* stdout/stderr are fully buffered (switch_app.c) */
            fflush(stderr);
            fprintf(heartbeat_diag,
                    "uptime_ms=%llu frames=%u flip_requests=%u "
                    "since_frame_report_ms=%llu fifo_calls=%u fifo_cpu_ms=%llu\n",
                    (unsigned long long)(now - frame_start_ms), frames_now,
                    cellGcm_flip_request_count(),
                    (unsigned long long)(now - clock_window_start), fifo_calls,
                    (unsigned long long)(fifo_cpu_ns / 1000000ull));
            fflush(heartbeat_diag);
            heartbeat_previous_ms = now;
        }
        if (clock_diag && frames_now - last_report_frames >= 30) {
            uint64_t report_now = frame_now_ms();
            uint64_t ppu_cpu_now_ns = g_main_thread_handle
                ? ps3_switch_thread_cpu_ns_for(g_main_thread_handle) : 0;
            uint64_t ppu_cpu_ms = ppu_cpu_previous_ns &&
                ppu_cpu_now_ns >= ppu_cpu_previous_ns
                ? (ppu_cpu_now_ns - ppu_cpu_previous_ns) / 1000000ull : 0;
            unsigned ppu_cpu_valid = ppu_cpu_previous_ns && ppu_cpu_now_ns;
            ppu_cpu_previous_ns = ppu_cpu_now_ns;
            uint64_t fifo_diag_now[8], fifo_diag_delta[8];
            cellGcm_fifo_diag_snapshot(fifo_diag_now);
            for (unsigned i = 0; i < 8; ++i) {
                fifo_diag_delta[i] = fifo_diag_now[i] - fifo_diag_prev[i];
                fifo_diag_prev[i] = fifo_diag_now[i];
            }
            fprintf(clock_diag,
                    "frames=%u delta=%u wall_ms=%llu tick_ms=%llu fifo_ms=%llu "
                    "present_ms=%llu sleep_ms=%llu flip_requests=%u "
                    "fifo_cpu_ms=%llu fifo_cpu_valid=%u fifo_calls=%u "
                    "fifo_max_ms=%llu sleep_calls=%u sleep_max_ms=%llu "
                    "present_cpu_ms=%llu fifo_headers=%llu fifo_payload=%llu "
                    "fifo_3d_methods=%llu fifo_method_sample_cpu_us=%llu "
                    "fifo_method_samples=%llu fifo_budget_hits=%llu "
                    "fifo_draw_methods=%llu fifo_draw_cpu_us=%llu "
                    "ppu_main_cpu_ms=%llu ppu_main_cpu_valid=%u\n",
                    frames_now, frames_now - last_report_frames,
                    (unsigned long long)(report_now - clock_window_start),
                    (unsigned long long)tick_ms, (unsigned long long)fifo_ms,
                    (unsigned long long)present_ms, (unsigned long long)sleep_ms,
                    cellGcm_flip_request_count(),
                    (unsigned long long)(fifo_cpu_ns / 1000000ull), fifo_cpu_valid,
                    fifo_calls, (unsigned long long)fifo_max_ms,
                    sleep_calls, (unsigned long long)sleep_max_ms,
                    (unsigned long long)(present_cpu_ns / 1000000ull),
                    (unsigned long long)fifo_diag_delta[0],
                    (unsigned long long)fifo_diag_delta[1],
                    (unsigned long long)fifo_diag_delta[2],
                    (unsigned long long)(fifo_diag_delta[3] / 1000ull),
                    (unsigned long long)fifo_diag_delta[4],
                    (unsigned long long)fifo_diag_delta[5],
                    (unsigned long long)fifo_diag_delta[6],
                    (unsigned long long)(fifo_diag_delta[7] / 1000ull),
                    (unsigned long long)ppu_cpu_ms, ppu_cpu_valid);
            fflush(clock_diag);
            clock_window_start = report_now;
            last_report_frames = frames_now;
            tick_ms = fifo_ms = present_ms = sleep_ms = 0;
            fifo_cpu_ns = fifo_max_ms = sleep_max_ms = 0;
            present_cpu_ns = 0;
            fifo_calls = sleep_calls = fifo_cpu_valid = 0;
        }
#endif
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* Cut at PS3_GAME, however deep the ELF sits below it: the collection's
     * games live in PS3_GAME/USRDIR/LAUNCH/. Without one, strip EBOOT.elf,
     * USRDIR and PS3_GAME as before. */
    char* game = strstr(s_vfs_root, "/PS3_GAME/");
    if (game) *game = 0;
    else for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#elif defined(__SWITCH__)
    /* Horizon cannot back a 3.5 GiB calloc. Reserve the 4 GiB guest VA and
     * commit only the main memory now; vm_commit maps later guest allocations. */
    if (vm_init() != CELL_OK) return false;
    /* GCM's control register, offset tables and other HLE structures live just
     * beyond the RSX region. Unlike Windows, the reserved guest VA is not
     * readable until these pages are explicitly backed. */
    if (vm_commit(VM_HLE_INJECT_BASE, 0x60000u) != CELL_OK) {
        fprintf(stderr, "[boot] could not commit HLE guest window at 0x%08X\n",
                VM_HLE_INJECT_BASE);
        return false;
    }
    /* cellGcmInit advertises 256 MiB of local video memory at 0xC0000000.
     * The game's allocator writes its end-of-region metadata near 0xCD700000
     * during boot, before any framebuffer is shown. Map the advertised region
     * now: Horizon's reserved guest VA does not back those writes on demand. */
    if (vm_commit(0xC0000000u, 0x10000000u) != CELL_OK) {
        fprintf(stderr, "[boot] could not commit RSX local memory\n");
        return false;
    }
    /* T23: back the guest null page with zeros. Lifted vector loads (lvx)
     * address vm_base directly, and Budokai 3 reads a null+0x10 vector
     * mid-battle: Windows demand-commits that page and reads zeros, the
     * Switch took a data abort. Scalar null accesses still go through the
     * checked path (reported, stores dropped). */
    if (vm_commit(0x00000000u, 0x10000u) != CELL_OK)
        fprintf(stderr, "[boot] could not back the guest null page\n");
    ppu_vm_size = 0;              /* the reservation covers the full 32-bit VA */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

#ifdef _WIN32
static int platform_file_smoke(void)
{
    if (!CreateDirectoryA(".tmp", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return 1;
    const Ps3Platform *platform = ps3_platform_win32();
    const Ps3PlatformFileOps *file = &platform->file;
    const char *path = ".tmp/t10-platform-file.bin";
    const char payload[] = "ps3recomp platform";
    char received[sizeof(payload)] = {};
    void *handle = NULL;
    uint64_t count = 0, position = 0;
    if (file->open(file->context, path, PS3_PLATFORM_FILE_READ |
            PS3_PLATFORM_FILE_WRITE | PS3_PLATFORM_FILE_CREATE |
            PS3_PLATFORM_FILE_TRUNCATE, &handle) != PS3_PLATFORM_OK) return 1;
    int failed = file->write(file->context, handle, payload, sizeof(payload), &count) != PS3_PLATFORM_OK ||
                 count != sizeof(payload) ||
                 file->seek(file->context, handle, 0, PS3_PLATFORM_SEEK_BEGIN, &position) != PS3_PLATFORM_OK ||
                 file->read(file->context, handle, received, sizeof(received), &count) != PS3_PLATFORM_OK ||
                 count != sizeof(received) || memcmp(payload, received, sizeof(payload)) != 0;
    if (file->close(file->context, handle) != PS3_PLATFORM_OK) failed = 1;
    Ps3PlatformFileInfo info = {};
    if (file->stat(file->context, path, &info) != PS3_PLATFORM_OK ||
        info.type != PS3_PLATFORM_FILE_TYPE_REGULAR || info.size != sizeof(payload)) failed = 1;
    void *directory = NULL;
    if (file->dir_open(file->context, ".tmp", &directory) != PS3_PLATFORM_OK) return 1;
    int found = 0;
    Ps3PlatformDirEntry entry = {};
    while (file->dir_read(file->context, directory, &entry) == PS3_PLATFORM_OK)
        if (strcmp(entry.name, "t10-platform-file.bin") == 0) found = 1;
    file->dir_close(file->context, directory);
    if (failed || !found) return 1;
    puts("[platform] file smoke OK (open/write/seek/read/stat/dir)");
    return 0;
}

static int platform_display_smoke(void)
{
    const Ps3Platform *platform = ps3_platform_win32();
    const Ps3PlatformDisplay *display = &platform->display;
    if (ps3_platform_validate(platform) != PS3_PLATFORM_OK ||
        display->open(display->context, 320, 180, "ps3recomp platform smoke") != PS3_PLATFORM_OK)
        return 1;
    Ps3PlatformFrame frame = {};
    if (display->acquire_frame(display->context, &frame) != PS3_PLATFORM_OK) {
        display->close(display->context);
        return 1;
    }
    for (uint32_t y = 0; y < frame.height; ++y) {
        uint8_t *row = (uint8_t *)frame.pixels + y * frame.pitch_bytes;
        for (uint32_t x = 0; x < frame.width; ++x) {
            row[x * 4 + 0] = (uint8_t)x;       /* BGRA blue */
            row[x * 4 + 1] = (uint8_t)y;       /* green */
            row[x * 4 + 2] = 96;               /* red */
            row[x * 4 + 3] = 255;
        }
    }
    Ps3PlatformStatus status = display->present(display->context, &frame, 0);
    display->pump_events(display->context);
    platform->time.sleep_us(platform->time.context, 250000);
    display->close(display->context);
    if (status != PS3_PLATFORM_OK) return 1;
    puts("[platform] display smoke OK (320x180 BGRA)");
    return platform_file_smoke();
}
#endif

#ifdef __SWITCH__
extern "C" int ps3_switch_spu_shufb_selftest(void);
extern "C" int ps3_switch_spu_ls_neon_selftest(void);
extern "C" int ps3_spu_profile_start(void);
extern "C" int ps3_ppu_hot_profile_start(void);
#endif
extern "C" int ps3_diag_enabled;   /* cellGcmSys.c */

int main(int argc, char** argv)
{
#ifdef __SWITCH__
    if (!ps3_switch_app_init()) return 1;
    g_main_thread_handle = ps3_switch_current_thread_handle();
    fprintf(stderr, "[t19-build] t21-taskset-waiting-v9 2026-10-21\n");
    /* main.cpp is touched by switch-dev.ps1 before every build, so this is
     * the time of the build that is running. */
    fprintf(stderr, "[build] compiled %s %s\n", __DATE__, __TIME__);
    /* A run stopping before a subsystem starts must not inherit its old log. */
    const char* boot_logs[] = {"t21-frames.log", "t21-spu-hot.log", "t21-input.log",
        "t21-input-queue.log",
        "t21-clock.log", "t21-heartbeat.log", "t21-stall.log", "t21-spu-cpu.log", "t21-spu-schedule.log",
        "t21-spu-wake.log",
        "t21-guest-cpu.log", "t20-pixels.log"};
    for (const char* name : boot_logs) {
        char path[128];
        snprintf(path, sizeof(path), "sdmc:/switch/dbz-budokai-3-switch/%s", name);
        FILE* log = fopen(path, "w");
        if (log) {
            fputs("build=t21-taskset-waiting-v9 state=subsystem_not_started\n", log);
            fclose(log);
        }
    }
    /* Diagnostics only on request: GPU log, shader dumps, PPU and thread
     * samplers, heap checks cost the game frame time (SD writes, sampler
     * threads). Create sdmc:/switch/dbz-budokai-3-switch/diag.on to turn them on. */
    {
        FILE* marker = fopen("sdmc:/switch/dbz-budokai-3-switch/diag.on", "r");
        ps3_diag_enabled = marker ? 1 : 0;
        if (marker) fclose(marker);
        fprintf(stderr, "[diag] verbose diagnostics %s (diag.on %s)\n",
                ps3_diag_enabled ? "ON" : "OFF", ps3_diag_enabled ? "found" : "absent");
    }
    #ifdef PORT_SWITCH_PPU_FUNC_PROFILE
    if (true)
    #else
    if (ps3_diag_enabled)
    #endif
        fprintf(stderr, "[t23] PPU hot-address sampler %s\n",
                ps3_ppu_hot_profile_start() ? "enabled" : "unavailable");
#ifdef BUDOKAI_SPU_PROFILE
    fprintf(stderr, "[t21] SPU function profiler %s\n",
            ps3_spu_profile_start() ? "enabled" : "unavailable");
#else
    fprintf(stderr, "[t21] SPU function profiler disabled\n");
#endif
    if (!ps3_switch_spu_shufb_selftest()) {
        fprintf(stderr, "[t21] SPU shufb NEON self-test FAILED\n");
        return 1;
    }
    fprintf(stderr, "[t21] SPU shufb NEON self-test PASS\n");
    if (!ps3_switch_spu_ls_neon_selftest()) {
        fprintf(stderr, "[t21] SPU local-store NEON self-test FAILED\n");
        ps3_switch_app_hold("SPU local-store NEON self-test failed");
        return 1;
    }
    fprintf(stderr, "[t21] SPU local-store NEON self-test PASS\n");
    const char* switch_elf = ps3_switch_game_elf_path();
    char* switch_argv[] = { argv && argc ? argv[0] : const_cast<char*>(switch_elf),
                            const_cast<char*>(switch_elf) };
    argc = 2;
    argv = switch_argv;
#endif
#ifdef _WIN32
    if (argc == 2 && strcmp(argv[1], "--platform-smoke") == 0)
        return platform_display_smoke();
#endif
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    const Ps3Platform *platform = ps3_platform_win32();
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
#ifdef __SWITCH__
        ps3_switch_app_hold("Guest address space allocation failed");
#endif
        return 1;
    }
#ifdef __SWITCH__
    ps3_switch_app_stage("Guest address space ready");
#endif

#ifdef _WIN32
    /* The loader still parses ELF data, while the host backend owns the
     * preflight filesystem query and reports a missing/non-file input early. */
    Ps3PlatformFileInfo elf_info = {};
    if (platform->file.stat(platform->file.context, argv[1], &elf_info) != PS3_PLATFORM_OK ||
        elf_info.type != PS3_PLATFORM_FILE_TYPE_REGULAR) {
        fprintf(stderr, "ERROR: PPU ELF is not a readable file: %s\n", argv[1]);
        return 1;
    }
#endif
    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
#ifdef __SWITCH__
        ps3_switch_app_hold("PPU ELF loader failed");
#endif
        return 1;
    }
#ifdef __SWITCH__
    ps3_switch_app_stage("PPU ELF loaded");
#endif

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    ppu_recomp_register();   /* lifted function table -> address map */
    printf("[boot] SPU registered fingerprints: %u\n", spu_workload_count());
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ps3_blus_register_hle_overrides();
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
#ifdef __SWITCH__
    ps3_switch_app_stage("Opening game display");
    ps3_switch_app_prepare_display();
#endif
#ifdef _WIN32
    void *clock_thread = NULL;
    if (platform->thread.create(platform->thread.context, &clock_thread,
            [](void *argument) { frame_clock(argument); }, NULL,
            4u * 1024 * 1024, 0) != PS3_PLATFORM_OK) {
        fprintf(stderr, "[rsx] frame clock thread creation failed\n");
        return 1;
    }
    platform->thread.detach(platform->thread.context, clock_thread);
#else
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);
#endif

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);
#ifdef __SWITCH__
    ps3_switch_app_stage("Dispatching PPU entry");
#endif

#ifdef __SWITCH__
    ps3_thread_stats_register("ppu", "MAIN (game)");
    if (ps3_diag_enabled)
        fprintf(stderr, "[t25] thread/core CPU sampler %s\n",
                ps3_thread_stats_start() ? "enabled" : "unavailable");
#endif
    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);
#ifdef __SWITCH__
    ps3_switch_app_hold("PPU entry returned; see t19-stdout.log");
#endif

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
