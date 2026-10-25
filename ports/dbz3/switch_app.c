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

void ps3_switch_app_prepare_display(void)
{
    if (!graphics_ready) {
        consoleExit(NULL);
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
    if (!graphics_ready) consoleUpdate(NULL);
}

void ps3_switch_app_hold(const char *reason)
{
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
        if (!graphics_ready) consoleUpdate(NULL);
        svcSleepThread(50000000);
    }
}

static void switch_runner_on_exit(void)
{
    ps3_switch_app_stage("Process exit() called");
}

int ps3_switch_app_init(void)
{
    consoleInit(NULL);
    printf("Budokai BLUS30966 T19 runner\n");
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
    { extern int g_t23_postfx_nowait;   /* postfx-nowait: drop the end-of-frame wait (heap corruption risk) */
      FILE* sync = fopen("sdmc:/switch/dbz-budokai-3-switch/postfx-nowait", "r");
      if (sync) { g_t23_postfx_nowait = 1; fclose(sync); } }
    if (mkdir("sdmc:/switch/dbz-budokai-3-switch", 0777) != 0 && errno != EEXIST) {
        ps3_switch_app_hold("Cannot create /switch/dbz-budokai-3-switch on SD");
        return 0;
    }
    sd_ready = 1;
    FILE *status = fopen(status_log, "w");
    if (status) {
        fputs("T19 diagnostic runner started\n", status);
        fclose(status);
    }
    printf("Game: %s\n", game_elf);
    printf("Logs: /switch/dbz-budokai-3-switch/t19-*.log\n");
    printf("Press + to close if an error is shown.\n");
    consoleUpdate(NULL);

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
