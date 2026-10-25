/*
 * ps3recomp game project -- game-specific hooks and overrides.
 *
 * main.cpp is the runner and is game-agnostic. This file is where a port puts
 * everything that is not: the PRX loading hook it must answer, overrides for
 * firmware functions the HLE library gets wrong for this title, and patches
 * over recompiled code.
 *
 * It starts out as the empty version of all of that, which is what a game with
 * no lifted PRX and no overrides needs.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

extern "C" void ps3_hle_register(unsigned int nid, const char* name, void* handler);
extern "C" int _cellSpursLFQueuePushBody(unsigned long long, unsigned long long, unsigned int);

extern "C" int g_cellAudioOut_max_channels;   /* libs/misc/cellAvconfExt.c */
#if defined(__SWITCH__) && defined(PORT_AUDIO_SPU_LOWPRIO)
extern "C" const char* g_spu_lowprio_group;   /* runtime/platform/switch_thread_cpu.c */
#endif

/* T-A1: DBZ3 bnusCore reverb bypass (scripts/spu_patch_dbz3_audio.py). The
 * SPU's convolution reverb is what keeps it at ~1/3 of real time; skipping it
 * takes the engine's own "no reverb" path. On by default; the reverb comes
 * back with sdmc:/switch/dbz-budokai-3-switch/reverb.on (Switch) or BNUS_REVERB=1. */
extern "C" { int g_bnus_reverb_skip = 1; }

extern "C" void ps3_blus_register_hle_overrides(void)
{
    {
        int reverb = 0;
#ifdef __SWITCH__
        FILE* marker = fopen("sdmc:/switch/dbz-budokai-3-switch/reverb.on", "r");
        if (marker) { reverb = 1; fclose(marker); }
#else
        const char* e = getenv("BNUS_REVERB");
        reverb = e && *e && *e != '0';
#endif
        g_bnus_reverb_skip = reverb ? 0 : 1;
        fprintf(stderr, "[bnus] convolution reverb %s\n", reverb ? "ON (emulated)" : "skipped");
    }
    /* T32 (tried, no effect): capping the reported audio output at stereo did
     * not change DBZ3's mix -- it still opens 8-channel ports and its bnusCore
     * SPU still writes ~43 of 187.5 blocks a second. Left at the default. */
    g_cellAudioOut_max_channels = 0;
#if defined(__SWITCH__) && defined(PORT_AUDIO_SPU_LOWPRIO)
    /* T33 test: the bnusCore audio SPU at the lowest host priority, so the game
     * and the renderer always win a contended core. Audio stays as partial as
     * it is (that SPU cannot keep up regardless); the question is the FPS. */
    g_spu_lowprio_group = "bnusCore";
#endif

    /* The generated import table does not register this BLUS SPURS export. */
    ps3_hle_register(0x8A85674Du, "_cellSpursLFQueuePushBody",
                     (void*)_cellSpursLFQueuePushBody);
}

/* ---------------------------------------------------------------------------
 * PRX loading
 *
 * main.cpp calls this once, after the lifted function table is registered and
 * vm_base is live, before the game runs. A title that lifts a real system PRX
 * -- libsre, which is cellSpurs and cellSync -- loads its image into guest RAM
 * here and registers its exports. Most ports never need to: the HLE library
 * answers those modules instead.
 * -----------------------------------------------------------------------*/

extern "C" void ps3_load_prx_modules(void)
{
}

/* ---------------------------------------------------------------------------
 * Example: Override a specific NID
 *
 * The module system resolves function imports by NID.  To replace a default
 * HLE stub with custom logic, register your function at init time.
 * -----------------------------------------------------------------------*/

/*
 * Example: override cellGcmSetFlipMode (NID 0xA53D12AE) to force vsync.
 *
 *   static void my_cellGcmSetFlipMode(u32 mode)
 *   {
 *       printf("[hook] cellGcmSetFlipMode: forcing VSYNC (was %u)\n", mode);
 *       // Force vsync regardless of what the game requested
 *       cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);
 *   }
 *
 * Then in your init code:
 *
 *   ps3::modules::override_nid(0xA53D12AE,
 *       reinterpret_cast<void*>(my_cellGcmSetFlipMode));
 */

/* ---------------------------------------------------------------------------
 * Example: Hook into module loading
 *
 * You can intercept cellSysmoduleLoadModule to perform custom actions
 * when a particular module is loaded.
 * -----------------------------------------------------------------------*/

/*
 *   static s32 my_loadmodule_hook(u16 id)
 *   {
 *       printf("[hook] Module 0x%04X loaded\n", id);
 *
 *       if (id == CELL_SYSMODULE_AUDIO) {
 *           // Perform custom audio backend initialization
 *           init_my_audio_backend();
 *       }
 *
 *       // Call the original implementation
 *       return cellSysmoduleLoadModule(id);
 *   }
 */

/* ---------------------------------------------------------------------------
 * Game-specific patches
 *
 * Sometimes you need to patch recompiled code at specific addresses.
 * Use the patch system to modify behavior without editing generated code.
 * -----------------------------------------------------------------------*/

/*
 *   // Skip an integrity check at guest address 0x00812FA0
 *   ps3::patches::nop_range(0x00812FA0, 0x00812FB0);
 *
 *   // Replace a function call with a direct return
 *   ps3::patches::force_return(0x009A1000, 0);  // return 0
 */
