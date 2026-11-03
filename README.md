# dbz-budokai-3-switch

This tree contains the port, build tools, and local patches to the MIT-licensed
ps3recomp toolkit. It does **not** contain the game, Sony firmware, translated
game code, or game artwork. Supply a legitimate BLUS30966 copy of Dragon Ball Z
Budokai HD Collection and the official PS3 firmware yourself. The game and
Dragon Ball trademarks belong to their respective owners. The MIT license
applies to this project's own code; dependencies retain their own licenses.

This port is experimental. Switch rendering and combat performance are still
being tested; no game assets or installable packages are distributed here.

## Project status

This is a static recompilation of the PS3 game, not an emulator. The PowerPC
(PPU) and SPU code of your own copy of the game is lifted to C++ on your PC and
compiled for the Switch together with a runtime that implements the PS3 system
libraries. Graphics commands from the game (RSX) are replayed on the Switch GPU
through deko3d.

### What is implemented

- **Import and lifting pipeline.** `import-game.ps1` installs the PKG through
  RPCS3, checks the ELF hash and lifts PPU and SPU code locally.
  `scripts/postprocess-lift.py` applies the port's own fixes to the lifted code
  (guards for the Dragon Universe effect list, big-endian AltiVec lane fixes).
- **Runtime.** A pinned ps3recomp revision with a local patch and an overlay
  (`patches/`) provides memory, threads, file system, pad, audio and video
  modules for the Switch.
- **Graphics.** A draw engine decodes the RSX state and translates the game's
  vertex and fragment programs to HLSL. The HLSL is compiled offline with `uam`
  into deko3d shaders (`shaders/deko3d/hlsl/`). Missing shaders can be captured
  on the console and added to the build. A vertex cache, texture cache, render
  scale and an optional GPU FXAA pass are available.
- **Timing.** Game logic runs on a 60 Hz scheduler (`Logic60`) with extra logic
  passes between rendered frames. The pad is read on every logic step.
- **Packaging.** Development NSP (game data on the SD card) and full NSP (game
  data in RomFS). If deko3d fails to start, a limited software renderer is
  used.
- **Debug tools.** On-screen debug menu, screenshots, screen recording, a
  per-draw frame capture, log collection and shader collection scripts (see
  below).

### Goals reached

- The game boots and runs on the Switch; menus, duels and Dragon Universe (DU)
  maps are playable.
- **Duels run at 60 FPS at stock clocks** (Goku vs Vegeta, run139, 2 November
  2026: 300 images every 5 s for the whole duel, never dropping to 30). See
  [FPS milestones](#fps-milestones).
- Namek in DU now renders its full terrain. The cause was a family of lifted
  AltiVec instructions (`vmulouh`, `vmuleuh`, `vsubsws`, `vaddshs`, `vmsumshm`)
  that read vector registers with the wrong byte order.
- The same fix brought the finishing-move cutscenes back in sync.
- Missed and delayed controls were fixed by reading the pad live on every logic
  step.
- Saves are kept in the Switch save data of the chosen user, like installed
  games (see [Save data](#save-data)).
- Reproducible setup, build and packaging with PowerShell scripts.

### FPS milestones

All numbers are duels at stock clocks (CPU 1020 MHz), measured from the logs.
The full history is in [ROADto60fps.md](ROADto60fps.md).

| Date | Build / run | Duel | What changed |
|---|---|---|---|
| 29 Oct 2026 | `base60-light` | ~31 FPS (29-34) | Starting point: both the game thread (~30 ms per image) and the renderer (~22 ms) were over the 16.7 ms budget. |
| 1 Nov 2026 | run89-92 | 30 FPS, cost per image falling | Native hot math (sin/cos, matrices) and draw-state writers: about -3 ms per frame on the game thread. |
| 2 Nov 2026 | run105-113 | 30/60 switching | New 60 Hz pacer, triple buffering, three frame sets in flight. |
| 2 Nov 2026 | run130-132 | renderer no longer the limit | Renderer split into two threads (`rsx_mt_backend.c`). |
| 2 Nov 2026 | run135-137 | 60 Hz in light parts, 30 in heavy ones | Pacer tries 60 Hz instead of predicting it; native end-of-frame wait. |
| 2 Nov 2026 | **run139** | **60 FPS for the whole duel** | Seven more native draw and animation functions. |

Not measured again since the 60 FPS work: the DU map and the experience/level
screen after a DU fight (both ~30 FPS or lower at the start).

### Fixed recently

- **Dragon Universe, white skin.** The map's skin shader blends towards the fog
  colour by `fog.y`, which on the RSX is the fog factor the hardware computes
  from `SET_FOG_MODE` and `SET_FOG_PARAMS`. The port passed the raw fog
  coordinate instead. The engine now hands mode and parameters to the pixel
  shaders (`fp_alpha.yzw`) and the shader translator computes the factor with
  RPCS3's formulas. The other fog-reading map shaders get the same fix.
- **Blue opening movie.** The movie's YUV to RGB shader came out blue and
  flashed white on the Switch although every input read alone was right
  (planes, constants, vertex colour checked on the GPU, run141-146). It is
  replaced by an override with the BT.601 constants the game always uploads
  written in the code (`ps_3a9c050fe20a0ac2`); why reading them from the
  constant buffer in that formula fails is still open.
- **Boot text.** The boot no longer prints the runner's console text: the
  screen stays black until the game draws. The text console comes up only
  when the boot stops on an error.
- **Crackling audio** in music, effects and voices. The read position of the
  audio ring advanced in bursts, paced by the Switch output buffers; the
  game's sound engine and its music mixer then wrote one block twice and
  skipped the next, which played stale (about 20% of effect/voice blocks, 1% of
  music blocks). Blocks are now mixed on a steady 5.33 ms clock
  (`audio-steady-off` on the SD restores the old pacing).
- **Missing Freezer shaders**: the two vertex shaders were captured and added.
- **Animation jerks**: characters no longer jerk or run out of step (user test,
  2 November 2026). Probable reason: duels now stay at 60 Hz with one logic
  step per image, instead of switching between 30 and 60 Hz with uneven step
  counts.
- **NSP failed to install** with a progressive JPEG icon. The packager now
  accepts only a baseline 256x256 JPEG and stops with a clear message otherwise.
- **Saves outside the port folder**: earlier builds wrote them to
  `sdmc:/gamedata/...`. They are moved automatically (see [Save data](#save-data)).
- **Light build did not compile** after the end-of-frame wait counters
  (`cellGcmSys.c` used a libnx header not included there).
- **End-of-frame wait** (`0x002909C0`): the user-command callback is now
  delivered by the waiting thread instead of whichever thread made the next
  system call.

### Known bugs

- **Dragon Universe, UI.** Several menus flicker or are drawn twice, with the
  second copy offset. Seen by the user: opening the map (Triangle), the bar
  holding the Dragon Balls flickers as if a second, offset bar appeared and
  disappeared; opening concentration mode (R), a second image flashes in the
  same way. Findings (run149-153): the layers of the bar (same texture, same X
  when still) are drawn from two vertex rings the title alternates per frame
  (`0x00F80000`, `0x01088000`), and in a doubled frame some layers already
  hold the next position. At FRAME CAP 30 (two logic steps every image) the
  second bar stays on screen, so the extra Logic60 steps are involved;
  `du-full-extra` makes it worse. Not the cause: `repeat-draw-off`, the FIFO
  lagging behind the previous flip, missing memory barriers. Tried and
  reverted: waiting for the previous frame's flip, and flushing the regular
  pass before each extra step (no fix, lower FPS).
- **Opening movie stutters.** The game moves to a new movie frame only 8-10
  times per second instead of 30 (`movie` line in `t23-draw-prof.log`). The
  Sofdec decoder runs on two translated SPU threads ("SPU Delegate", ~88% of a
  core each), about half of it in a loop that waits for work
  (`0x19D98`, polling `0x1F520` while it answers busy).
- **Dragon Universe, flying.** At maximum flight speed, white pixel artifacts
  sometimes appear on screen.
- **Audio reverb** is disabled to save CPU (`reverb.on` turns it back on).

### Next goals

- More headroom in duels: images often arrive just in time, so heavier scenes
  (other stages, transformations, beam struggles) may still drop frames.
- 60 FPS on the DU map and the level screen.

## Requirements

- Windows 10 or newer and Windows PowerShell 5.1.
- Git, Python, CMake, and Ninja.
- devkitPro with devkitA64, libnx, deko3d, and `uam.exe`.
- A legitimate BLUS30966 game PKG and official firmware named `PS3UPDAT.PUP`.
- A Switch keyset at `%USERPROFILE%\.switch\prod.keys` for NSP packaging.
  Keep it outside this project.
- Several GB of free disk space. The current full NSP is about 2.3 GB.

## Setup and build

Run commands from the project root in Windows PowerShell. Setup downloads
dependencies and tools from GitHub, so Git must be in `PATH` and the PC needs
internet access. The scripts look for devkitPro at `C:\devkitPro`, or use the
`DEVKITPRO` environment variable if it is installed elsewhere.

The examples use `-ExecutionPolicy Bypass` for that PowerShell process. This
also works when Windows blocks unsigned local scripts.

### 1. Install tools and prepare dependencies

Install devkitPro and its Switch packages, including devkitA64, libnx, deko3d,
and uam. Then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\setup.ps1
```

Setup fetches the pinned ps3recomp revision, applies the local patch and
overlay, creates `.venv`, verifies the NSP packer, and downloads a
checksum-verified portable RPCS3 build. If devkitPro is missing, setup warns
that `uam.exe` is unavailable; install devkitPro before building.

### 2. Import the game

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\import-game.ps1 `
  -Pkg 'D:\Games\DBZ3\game.pkg' -Pup 'D:\PS3UPDAT.PUP'
```

If RPCS3 firmware is not installed, its installer opens first. Complete the
installation; the script then installs the PKG. Close RPCS3 when the installer
finishes. The script extracts the game, decrypts `DBZ3.BIN`, checks the ELF
hash, and generates PPU and SPU code locally. The supported decrypted ELF
SHA-256 is `AB4D04482948F51D7E3D15F7C8183BDD8DDF15957D03BC6571ED6634628B3276`.

Game files stay under `userdata/`; generated code and the game icon stay under
`generated/`. Both are ignored by Git. Switch builds compile shader sources
from `shaders/deko3d/hlsl/` before packaging.

### 3. Build the development NSP

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build\switch-dev.ps1 `
  -Game dbz3 -Jobs 2
```

This compiles HLSL with devkitPro `uam.exe`, configures CMake, builds the Switch
executable, and packages `build/dev/dbz3/Dragonball-Z-Budokai-3-dev.nsp`. Copy
and install the NSP manually with your preferred Switch installer. `-Jobs`
controls Ninja parallelism. `-NoBuild` packages the existing executable.

### 4. Copy game data to the Switch for the development build

The development NSP reads game data from the SD card. For a card mounted in
Windows, replace `E:\` with its drive:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build\deploy-gamedata.ps1 `
  -SdRoot 'E:\'
```

Or connect the Switch by USB, open **DBI > Run MTP responder**, and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build\deploy-gamedata-mtp.ps1
```

Both methods copy game data only, not the NSP. The SD destination is
`switch/dbz-budokai-3-switch/game/PS3_GAME/`.

### 5. Capture more shaders from the Switch

`ShaderCapture` is on in the default build configuration. Install and run the
development NSP, then play through the scenes to cover (for example a duel
with Freezer). The Deko3D backend saves new variants to
`sdmc:/switch/dbz-budokai-3-switch/t23-shaders/`. Connect the Switch by USB,
start **DBI > Run MTP responder**, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\collect-switch-shaders.ps1
```

The script copies the shader folder through MTP, checks capture hashes,
compiles the HLSL with `uam.exe`, and saves successful captures in
`shaders/deko3d/hlsl/`. Rebuild and reinstall the development NSP to use them.
Repeat for later scenes. If a run was already copied to the PC, pass its folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\collect-switch-shaders.ps1 `
  -CaptureDir .\build\logs\dbz3\dev\run01\t23-shaders
```

The HLSL files in `shaders/deko3d/hlsl/` and optional fixes under `override/`
are the sources to commit and publish. Generated GLSL, DKSH headers, logs, and
game data remain local. A fresh checkout compiles the included HLSL before an
NSP build.

### 6. Build the full NSP

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build\switch-full.ps1 `
  -Jobs 2
```

The full package includes DBZ3 game data in RomFS and does not need the separate
game-data copy step. It falls back to the SD card for files absent from RomFS.
Logs and settings remain on SD; saves go to the Switch save data (see
[Save data](#save-data)). The output is
`build/dev/dbz3/Dragonball-Z-Budokai-3-full.nsp`. Copy and install it manually.
Both package types request Deko3D by default and use the limited software
renderer if Deko3D initialization fails.

## Build configuration

Build features are chosen in `scripts/build/switch-build-config.ps1`. Edit the
values, then run `switch-dev.ps1` or `switch-full.ps1`; both reconfigure CMake
on every run, so no manual cleanup is needed. The development and full NSP use
the same configuration; `switch-full.ps1 -NoBuild` repackages the executable
that `switch-dev.ps1` just built.

| Flag | Meaning |
|---|---|
| `Lightweight` | Removes the high-frequency profilers and capture code. It also makes the `diag.on` marker (below) have no effect. |
| `ProfilePpuFunctions` | Samples the innermost lifted PPU function and writes `t23-ppu-hot.log`. |
| `ProfileSpuFunctions` | SPU function profiler. |
| `LevelFunctionProfile` | Times `func_00030790` while the level-up screen task runs. |
| `InputQueueProfile` | Logs input queue age, depth and dropped events every five seconds. |
| `DekoDiagnostics` | Detailed deko3d texture and draw logs. |
| `LoopMetrics` | Records game loop and task timing (`t26-loop-prof.log`). |
| `FrameClockMetrics` | Records Switch frame-clock timing. |
| `ShaderCapture` | Dumps shaders the build does not have to `t23-shaders/` on the SD card. |
| `ScreenshotCapture` | Includes the debug-menu screenshot and screen-recording code. |
| `Logic60` | Keeps the 60 Hz gameplay timing. Leave it on. |

**Default build** (use this to play and to measure FPS): `Lightweight = $true`,
`ShaderCapture = $true` (it only writes files when a shader is missing) and
`Logic60`; everything else `$false`.

**Metrics build** (use this to compare runs): `Lightweight = $false`,
`LoopMetrics = $true`, `FrameClockMetrics = $true`, `ScreenshotCapture = $true`.
`t26-loop-prof.log` then has a `pace` line every 150 images (`interval_us`
16667 = 60 Hz, 33333 = 30 Hz, `est60_ema_us` = cost of a 60 Hz image, `late`)
and an `fwait` line (time waiting for the GPU). Compare only runs of the same
build type and the same scene (the project uses Goku vs Vegeta).

**Profiling build:** the metrics build plus `-ProfilePpuFunctions` and an empty
`diag.on` file on the SD; gives `t23-ppu-hot.log` (game thread functions). Times
are inflated by the profiler: use the proportions only.

`switch-dev.ps1` also accepts `-ProfilePpuFunctions`, `-LevelFunctionProfile`,
`-InputQueueProfile` and `-Feature <name>`; the name is recorded in
`build-info.txt`.

**Icon.** `icon.jpg` in the project root is used when present, otherwise the
icon generated at import (`generated/dbz3/assets/icon.jpg`). It must be a
baseline (not progressive) 256x256 JPEG; the packager refuses anything else.

### SD switches

Empty marker files in `sdmc:/switch/dbz-budokai-3-switch/` change behaviour
without a rebuild. Create them over MTP or with the SD card in a PC, delete them
to go back. Most are read once at start, so restart the game after a change.
The defaults are the best settings: no file is needed to play.

**Diagnostics and display**

| File | Effect |
|---|---|
| `diag.on` | Verbose diagnostics, GPU and thread logs (`t22`, `t25`, `t31`; needs `Lightweight = $false`). Without it those logs are old copies. |
| `fps-off` | Hides the FPS counter until the debug settings are first saved. |
| `depth-clamp` | Depth clamp on (A/B test for terrain lost to near/far clipping). |
| `map-capture-after` | Number of seconds: dumps the draw passes from then on (debug, non-light builds). `draws.txt` lists each draw's textures (with remap, pitch, filter), vertex attributes and fragment constants; `tex_*.raw` are the guest bytes of each texture. |

**Native code** (guest functions replaced by native C++; each one falls back to
the translated PS3 code when its file exists)

| File | Function |
|---|---|
| `native-b2-off` | All seven of the second batch below (color, texref, drawsetup, quad, sprite, sprite2, anim). |
| `native-color-off` | Material color `0x0002C7D8`. |
| `native-texref-off` | Texture reference `0x00031DC8`. |
| `native-drawsetup-off` | Draw setup `0x0002F56C`. |
| `native-quad-off` | Textured quad `0x00030790`. |
| `native-sprite-off`, `native-sprite2-off` | Sprites `0x00030DF0`, `0x00031024`. |
| `native-anim-off` | Bone animation `0x000207C0`. |
| `native-material-off` | Material apply `0x0002C088`. |
| `native-shader-off` | Shader program binds. |
| `native-draw-off` | Draw-state functions (parameter block, arrays, textures). |
| `native-sincos-off`, `native-matrix-off`, `native-point-off` | Math: sin/cos, quaternion matrix, matrix x point. |
| `native-rotvec-off`, `native-scale-off`, `native-translate-off`, `native-add3-off`, `native-len3-off`, `native-planes-off`, `native-euler-off`, `native-memset-off` | Small math helpers and memset. |
| `native-put4-off`, `native-put2f-off`, `native-put12-off`, `native-vcall3-off`, `native-transpose-off`, `native-vcall-a2o-off`, `native-a2o-off` | Draw-state writers and the direct `cellGcmAddressToOffset`. |
| `native-verify` | Runs the translated code and the native one on the same input and compares them (`t37-native-math.log`). Slow; for testing only. |
| `native-mesh-off`, `mesh-verify`, `mesh-selftest` | Mesh-body harness (only in builds with `PORT_DBZ3_NATIVE_MESH`, off by default). |
| `fastvm-off` | Slower, checked guest memory access in the translated code. |

**Renderer and timing**

| File | Effect |
|---|---|
| `render-mt-off` | Renderer on one thread (no `render-draw` thread). |
| `render-mt-sync` | Second thread kept but waited for at every image. |
| `usercmd-pump-off` | End-of-frame wait as the translated code: callbacks delivered by other threads only. |
| `postfx-nowait` | Skips the end-of-frame wait entirely. Risky (can corrupt memory); tests only. |
| `present-sync` | Synchronous presents. |
| `flush512` | Submits GPU work every 512 operations mid-frame (older behaviour). |
| `texbatch-off` | One GPU wait per texture chunk (older behaviour). |
| `texhash-per-use` | Re-hashes small textures on every use (older behaviour). |
| `tex-hot-off` | Large textures that just changed go back to the sampled change check (by default they are hashed in full for 2 s after a change). |
| `du-full-extra` | Runs the whole DU task in the extra logic pass instead of only its update part. |
| `repeat-draw-off` | Disables the repeated extra run of the DU draw task. |

**Audio and saves**

| File | Effect |
|---|---|
| `reverb.on` | Turns the game's reverb back on (costs CPU). |
| `bnus-prio-normal`, `bnus-prio-high` | Audio SPU thread at normal or high priority instead of the lowest. |
| `audio-sentinel-off` | Played audio blocks cleared to zero, as the PS3 firmware does. |
| `audio-steady-off` | Audio blocks paced by the Switch output buffers instead of a steady clock (old behaviour, crackles). |
| `savedata-sd` | Saves on the SD card instead of the Switch save data. |

To collect logs after a test, start **DBI > Run MTP responder** and run
`scripts/pull-switch-logs.ps1 -Game dbz3`. Logs are copied to
`build/logs/dbz3/dev/runNN`.

## Debug menu

The debug menu runs inside the game on the Switch. While it is open, the game
receives no input.

**Open and close.** Hold **L** and press both sticks (**L3 + R3**) together.
Press the same combination again, **B**, or choose **CLOSE MENU** to close it.

**Move.** **Up/Down** select a row. **Left/Right** change a value, **A** runs an
action. Render scale, FXAA, VSync, frame cap, FPS display and clocks are saved
to `sdmc:/switch/dbz-budokai-3-switch/debug-settings.cfg` and restored at the
next start.

| Row | What it does |
|---|---|
| **Display** | |
| RENDER SCALE | Internal resolution, 0.0 to 1.0 in steps of 0.1, with the render size next to it, e.g. `0.7 (896X504)  ACTIVE: 1.0 (1280X720)`. A change takes effect after **REBOOT APPLICATION**; the menu shows `REBOOT NEEDED` until then. |
| GPU FXAA | Toggles the GPU FXAA edge smoothing pass. |
| VSYNC | On: images wait for the display refresh. Off: shown at once, may tear. |
| FRAME CAP | **60**: 60 FPS, drops to 30 when a scene is too heavy (default). **30**: fixed 30. **OFF**: never forced down to 30; the rate varies between 30 and 60 and the game speed stays correct (it never goes above 60, because the game runs at least one logic step per image). |
| **Performance** | |
| SHOW FPS | Toggles the FPS counter at the top right. |
| SHOW FRAME MS | Toggles the frame time readout. |
| SHOW FPS STATS / RESET FPS STATS | Average, minimum and maximum FPS; reset them. |
| CPU CLOCK, GPU CLOCK, RAM CLOCK | Optional overclock like sys-clk, limited to the recommended official rates: CPU 1224 to 1785 MHz, GPU 460 to 921 MHz, RAM 1600 MHz. **DEFAULT** leaves the system clock alone. `NOW` shows the clock read back from the system. The choice is re-applied after docking changes and the system clocks are restored at DEFAULT and at exit. Turn sys-clk off for this game when using it. All performance numbers in this README are at DEFAULT. |
| **Capture** | |
| SAVE SCREENSHOT (A) | Saves a JPEG to `captures/shot_*.jpg` and closes the menu. Needs `ScreenshotCapture`. |
| START / STOP SCREEN RECORDING (A) | Records an MJPEG clip to `captures/clip_*.mjpg`. Needs `ScreenshotCapture`. |
| DRAW START | First draw call a capture checkpoints from. Left/Right move by the draw step, **ZL/ZR** by 128. |
| DRAW STEP | Toggles 8 or 1 draw calls between checkpoint images. |
| CAPTURE NEXT DRAW FRAME (A) | Captures the next rendered frame draw by draw (see below) and closes the menu. |
| **System** | |
| REBOOT APPLICATION (A) | Restarts the game. |
| CLOSE MENU (A) | Closes the menu. |

## Save data

The game's PS3 saves (`cellSaveData`) are kept in the **Switch save data** of
the user chosen at launch, like installed games: they appear in **System
Settings > Data Management** and can be backed up with JKSV. The save is
created on first use (32 MiB, 16 MiB journal) and every save is committed at
once, so closing the game does not lose it.

- At the first start, saves of earlier builds are moved from
  `sdmc:/gamedata/dev_hdd0/...` to
  `sdmc:/switch/dbz-budokai-3-switch/hdd0/home/00000001/savedata/`, and then
  copied into the Switch save data. The SD copy stays as a backup but is not
  updated any more.
- If no user can be chosen or the save data cannot be opened, the game saves on
  the SD (same folder). The `savedata-sd` file forces that.
- What happened is written to `sdmc:/switch/dbz-budokai-3-switch/savedata.log`.

### Draw capture

Use it to find which draw call produces a rendering bug.

1. Get the game to the scene with the bug and open the menu.
2. Set **DRAW START** (use 0 to see the whole frame) and **DRAW STEP** (1 for the
   finest detail, up to 16 checkpoint images are saved).
3. Choose **CAPTURE NEXT DRAW FRAME**. The next frame is written to
   `sdmc:/switch/dbz-budokai-3-switch/draw-captures/frame_<n>_<time>/`.
4. Copy the captures to the PC with the Switch in **DBI > Run MTP responder**:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\import-switch-images.ps1 -SkipAlbum
```

The script writes to `userdata/screenshoot/switch-dbi-import/`, with raw files
under `raw/` and PNG conversions under `png/`. Add `-IncludeRecordings` to copy
screen recordings too.

A capture folder contains:

- `draws.txt`: one line per draw call with the target surface, pipeline, blend
  and depth state, vertex count, object-space bounding box, shader hashes,
  fragment constants and the textures bound to each unit.
- `drawNNNNN_t0.ppm` and `final.ppm`: checkpoint images and the final frame.
- `u<unit>_<address>_<format>_<size>.bin`: raw texture data (`.png` decoded
  where possible).
- `fp_*.bin` and `vp_*.bin`: the raw fragment and vertex programs.

Compare the same character or object in a good scene and a bad scene: if the
texture hashes are equal and only the shader or its constants differ, the
problem is in the shader path rather than in the texture data.

## Common errors and blockers

| Message or symptom | Likely cause and action |
|---|---|
| `running scripts is disabled` | Run the script with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File ...` as above. The bypass applies only to that process. |
| `Git is required`, `Could not download ps3recomp`, or setup stops during clone | Install Git and check `git --version` in a new PowerShell window. Setup needs network access to GitHub. If it reports that `third_party/ps3recomp` has the wrong base revision, preserve that checkout and resolve it before rerunning setup. |
| `Python was not found`, `pip failed`, or dependency check failed | Install Python, then run `scripts/setup-python.ps1 -Python 'C:\path\python.exe'`. Check internet access if pip cannot download requirements. Rerun setup after Python setup succeeds. |
| `uam not found` | Install devkitPro's Switch tools. If it is outside `C:\devkitPro`, set `DEVKITPRO` in this terminal or pass `-DevkitPro` to the build script. |
| `Missing: ...ppu_recomp.h`, `Game data is missing`, or `DBZ3.BIN not found` | Complete setup, then rerun `import-game.ps1` with the correct PKG and firmware paths. Wait for import to finish before building. |
| `Firmware must be named PS3UPDAT.PUP` | Use that exact filename and pass its full path. The installer rejects empty or unusually small files. |
| `Unsupported DBZ3.elf SHA-256` | The PKG did not produce the supported BLUS30966 executable. Check the game release and RPCS3 installation; the script stops on unknown ELFs. |
| `prod.keys` missing or NSP packaging fails while loading keys | Put your keyset at `%USERPROFILE%\.switch\prod.keys`. Keep it outside the project directory. |
| `Expected program, control and meta NCAs`, `Expected one installable NSP`, or output-copy verification fails | Packaging did not produce a complete package. Check the preceding hacBrewPack error and free disk space, then rerun the build. The script now checks that the final NSP exists and matches the packaged file size. |
| `MSYS2 not found`, CMake configuration fails, or Ninja is missing | Repair devkitPro and verify its MSYS2 Bash and Switch CMake toolchain are installed. Also check CMake and Ninja are available. |
| Switch or DBI SD Card not found | Connect by USB and start **DBI > Run MTP responder**. If the device name differs in Explorer, pass `-DeviceName 'name shown in Explorer'`. |
| No shader capture folder or no captures found | Run the game with Deko3D and play a scene that draws, then retry with DBI MTP active. |
| Shader filename/hash mismatch or `uam` compilation failure | The capture may be incomplete or edited. Recapture it from the Switch. The collector removes files newly added by a failed import, and the build stops before making an NSP. |
| Full build runs out of disk space | Free several GB for RomFS staging and the final NSP. The current full package is about 2.3 GB. |
| Project path contains an apostrophe and CMake fails | Move the project to a path without apostrophes; current MSYS2 command quoting does not support them. |

To collect Switch logs for diagnosis, run
`scripts/pull-switch-logs.ps1 -Game dbz3`; logs are placed under
`build/logs/dbz3/dev/`.

## Repository hygiene

If you initialize this directory as a Git repository, commit the sources under
`shaders/deko3d/hlsl/`. `.gitignore` excludes generated GLSL, shader headers,
and NSP files. The `.githooks/pre-commit` hook runs `scripts/check-clean.ps1`
when installed.
