#include <switch.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef PORT_GAME_ELF
#define PORT_GAME_ELF "sdmc:/switch/dbz-budokai-3-switch/game/PS3_GAME/USRDIR/LAUNCH/DBZ3.elf"
#endif
static const char *game_elf = PORT_GAME_ELF;
const char *ps3_switch_game_elf_path(void) { return game_elf; }
static const char *const status_log =
    "sdmc:/switch/dbz-budokai-3-switch/t19-status.log";
static int sd_ready;
static int graphics_ready;
/* The text console stays off: the boot shows a black screen. It comes up
 * only when the boot stops on an error (ps3_switch_app_hold). */
static int console_on;

static void console_show(void)
{
    if (!console_on && !graphics_ready) {
        consoleInit(NULL);
        console_on = 1;
    }
}

/* Effect-list guards in postprocess-lift.py count what they skip here: first
 * node, derived address, next link, lookup. Logged by the deko3d stats line. */
unsigned g_dbz3_du_rejects[4];

void ps3_switch_app_prepare_display(void)
{
    if (!graphics_ready) {
        if (console_on) consoleExit(NULL);
        console_on = 0;
        graphics_ready = 1;
    }
}

/* The guest's 256 MiB main memory is mapped separately by switch_memory.
 * This heap holds every host thread stack (SPU async threads take 32 MiB
 * each) plus the deko3d memory blocks (~66 MiB). At 256 MiB the GPU build
 * ran out and the Bink decoder thread failed to start (T22 run 3). */
size_t __nx_heap_size = 1536u * 1024u * 1024u;  /* includes the 512 MiB deko3d image pool */

void ps3_switch_app_stage(const char *stage)
{
    if (sd_ready) {
        FILE *log = fopen(status_log, "a");
        if (log) {
            fprintf(log, "%s\n", stage);
            fclose(log);
        }
    }
    fprintf(stdout, "[T19] %s\n", stage);
    fflush(stdout);
    if (console_on && !graphics_ready) consoleUpdate(NULL);
}

void ps3_switch_app_hold(const char *reason)
{
    console_show();
    ps3_switch_app_stage(reason);
    fprintf(stderr, "[T19] %s\n", reason);
    fflush(stderr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        if (console_on && !graphics_ready) consoleUpdate(NULL);
        svcSleepThread(50000000);
    }
}

static void switch_runner_on_exit(void)
{
    ps3_switch_app_stage("Process exit() called");
}

int ps3_switch_app_init(void)
{
    /* libnx normally mounts sdmc in __appInit. Mount it only when absent. */
    Result rc = 0;
    if (!fsdevGetDeviceFileSystem("sdmc"))
        rc = fsdevMountSdmc();
    if (R_FAILED(rc) || !fsdevGetDeviceFileSystem("sdmc")) {
        char message[80];
        snprintf(message, sizeof(message), "SD mount failed: %08x", rc);
        ps3_switch_app_hold(message);
        return 0;
    }
    /* A full package mounts its game tree in RomFS; a dev package has none. */
    if (R_SUCCEEDED(romfsInit())) {
        const char *romfs_elf = "romfs:/game/PS3_GAME/USRDIR/LAUNCH/DBZ3.elf";
        FILE *probe = fopen(romfs_elf, "rb");
        if (probe) { fclose(probe); game_elf = romfs_elf; }
    }
    /* T23: sdmc:/switch/dbz-budokai-3-switch/fastvm-off turns the lifted code's
     * inline guest memory path off (A/B test without a rebuild). */
    { extern int g_ppu_fast_vm;
      FILE* off = fopen("sdmc:/switch/dbz-budokai-3-switch/fastvm-off", "r");
      if (off) { g_ppu_fast_vm = 0; fclose(off); } }
    { extern int g_rsx_tex_hash_per_use;   /* texhash-per-use: re-hash small textures on every use */
      FILE* per_use = fopen("sdmc:/switch/dbz-budokai-3-switch/texhash-per-use", "r");
      if (per_use) { g_rsx_tex_hash_per_use = 1; fclose(per_use); } }
    { extern int g_rsx_tex_hot;   /* tex-hot-off: changing large textures back on the sampled check */
      FILE* off = fopen("sdmc:/switch/dbz-budokai-3-switch/tex-hot-off", "r");
      if (off) { g_rsx_tex_hot = 0; fclose(off); } }
    { extern int g_t23_postfx_nowait;   /* postfx-nowait: drop the end-of-frame wait (heap corruption risk) */
      FILE* sync = fopen("sdmc:/switch/dbz-budokai-3-switch/postfx-nowait", "r");
      if (sync) { g_t23_postfx_nowait = 1; fclose(sync); } }
    if (mkdir("sdmc:/switch/dbz-budokai-3-switch", 0777) != 0 && errno != EEXIST) {
        ps3_switch_app_hold("Cannot create /switch/dbz-budokai-3-switch on SD");
        return 0;
    }
    /* A numeric delay in seconds arms one short per-pass GPU capture; an
     * optional second number is how many passes to keep (default 24, up to
     * 400). Keep this separate from diag.on: readback stalls make FPS
     * measurements invalid. */
#ifndef PORT_DBZ3_LIGHT_BUILD
    {
        FILE *capture = fopen("sdmc:/switch/dbz-budokai-3-switch/map-capture-after", "r");
        if (capture) {
            unsigned after = 0, passes = 24;
            if (fscanf(capture, "%u", &after) == 1 && after > 0 && after <= 3600) {
                if (fscanf(capture, "%u", &passes) != 1 || passes == 0 || passes > 400)
                    passes = 24;
                const char *dir = "sdmc:/switch/dbz-budokai-3-switch/map-pass-dump";
                char delay[16], max_passes[16];
                snprintf(delay, sizeof delay, "%u", after);
                snprintf(max_passes, sizeof max_passes, "%u", passes);
                if (mkdir(dir, 0777) == 0 || errno == EEXIST) {
                    setenv("RSX_ENGINE_PASS_DUMP", dir, 1);
                    setenv("RSX_ENGINE_PASS_DUMP_AFTER", delay, 1);
                    setenv("RSX_ENGINE_PASS_DUMP_MAX", max_passes, 1);
                    setenv("RSX_ENGINE_PASS_DUMP_DRAWS", "1", 1);
                    printf("Map pass capture armed after %u s\n", after);
                }
            }
            fclose(capture);
        }
    }
#endif
    sd_ready = 1;
    FILE *status = fopen(status_log, "w");
    if (status) {
        fputs("T19 diagnostic runner started\n", status);
        fclose(status);
    }

    FILE *elf = fopen(game_elf, "rb");
    if (!elf) {
        ps3_switch_app_hold("DBZ3.elf missing from RomFS and SD");
        return 0;
    }
    unsigned char magic[4];
    size_t read_count = fread(magic, 1, sizeof(magic), elf);
    fclose(elf);
    if (read_count != sizeof(magic) || magic[0] != 0x7f ||
        magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
        ps3_switch_app_hold("EBOOT.elf is not a valid ELF file");
        return 0;
    }
    ps3_switch_app_stage("EBOOT.elf readable and ELF header valid");
    if (!freopen("sdmc:/switch/dbz-budokai-3-switch/t19-stdout.log", "w", stdout)) {
        ps3_switch_app_hold("Cannot open t19-stdout.log");
        return 0;
    }
    if (!freopen("sdmc:/switch/dbz-budokai-3-switch/t19-stderr.log", "w", stderr)) {
        ps3_switch_app_hold("Cannot open t19-stderr.log");
        return 0;
    }
    /* T23: fully buffered. Unbuffered, every printf was its own SD write on
     * the guest thread that made it (three per frame from RESC/avconf alone).
     * The frame clock flushes both every 5 s (main.cpp heartbeat). */
    { static char out_buf[64 * 1024], err_buf[64 * 1024];
      setvbuf(stdout, out_buf, _IOFBF, sizeof out_buf);
      setvbuf(stderr, err_buf, _IOFBF, sizeof err_buf); }
    atexit(switch_runner_on_exit);
    ps3_switch_app_stage("Log files open; entering runner");
    return 1;
}
