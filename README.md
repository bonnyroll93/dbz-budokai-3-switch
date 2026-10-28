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
- Namek in DU now renders its full terrain. The cause was a family of lifted
  AltiVec instructions (`vmulouh`, `vmuleuh`, `vsubsws`, `vaddshs`, `vmsumshm`)
  that read vector registers with the wrong byte order.
- The same fix brought the finishing-move cutscenes back in sync.
- Missed and delayed controls were fixed by reading the pad live on every logic
  step.
- Reproducible setup, build and packaging with PowerShell scripts.

### Known bugs

- **Dragon Universe, UI.** Several menus flicker or are drawn twice, with the
  second copy offset.
- **Dragon Universe, skin.** The character's skin is rendered white, only on the
  map. The same textures render correctly in the character select screen and in
  duels, which use a different pixel shader variant; the map variant is the
  suspect.
- **Dragon Universe, flying.** At maximum flight speed, white pixel artifacts
  sometimes appear on screen.
- **Audio.** Reverb is disabled and some sounds are sometimes not played. Audio
  needs to be improved.

### Next goal: 60 FPS

Performance work so far aimed at a stable 30 FPS in duels, the DU map and the
screen shown after a DU fight. The next goal is an optimization pass to reach
60 FPS. Earlier measurements pointed at the CPU cost of the DU map task, the
post-fight experience and level screen, and the per-draw work of the draw
engine. Use the light build (below) for performance measurements and change one
thing per build.

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

Install and run the development NSP, then play through the scenes to cover.
The Deko3D backend saves new variants to
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
Logs, settings, and save data remain on SD. The output is
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

**Maximum-performance build** (use this to measure FPS): `Lightweight = $true`
and every other flag `$false` except `Logic60`. This is the current default.

**Debug build:** set `ScreenshotCapture = $true` to make the screenshot and
recording rows of the debug menu work, `ShaderCapture = $true` to collect new
shaders, and the profiler you need. `switch-dev.ps1` also accepts
`-ProfilePpuFunctions`, `-LevelFunctionProfile`, `-InputQueueProfile` and
`-Feature <name>`; the name is recorded in `build-info.txt`.

Empty marker files in `sdmc:/switch/dbz-budokai-3-switch/` switch behaviour
without a rebuild. Create them over MTP or with the SD card in a PC, and delete
them to turn the option off:

| File | Effect |
|---|---|
| `diag.on` | Verbose diagnostics and the GPU log (needs `Lightweight = $false`). |
| `du-full-extra` | Runs the whole Dragon Universe task in the extra logic pass instead of only its update part. |
| `repeat-draw-off` | Disables the repeated extra run of the DU draw task. |
| `flush512` | Submits GPU work every 512 operations mid-frame (older behaviour). |
| `texbatch-off` | One GPU wait per texture chunk (older behaviour). |
| `present-sync` | Synchronous presents. |
| `fps-off` | Hides the FPS counter until the debug settings are first saved. |

To collect logs after a test, start **DBI > Run MTP responder** and run
`scripts/pull-switch-logs.ps1 -Game dbz3`. Logs are copied to
`build/logs/dbz3/dev/runNN`.

## Debug menu

The debug menu runs inside the game on the Switch. While it is open, the game
receives no input.

**Open and close.** Hold **L** and press both sticks (**L3 + R3**) together.
Press the same combination again, **B**, or choose **CLOSE MENU** to close it.

**Move.** **Up/Down** select a row. **Left/Right** change a value, **A** runs an
action. Render scale, FPS, frame time and FXAA are saved to
`sdmc:/switch/dbz-budokai-3-switch/debug-settings.cfg` and restored at the next
start.

| Row | What it does |
|---|---|
| RENDER SCALE | Internal resolution, 0.0 to 1.0 in steps of 0.1. A change takes effect after **REBOOT APPLICATION**; the menu shows `REBOOT NEEDED` until then. |
| SHOW FPS | Toggles the FPS counter at the top right. |
| SHOW FRAME MS | Toggles the frame time readout. |
| SAVE SCREENSHOT (A) | Saves a JPEG to `captures/shot_*.jpg` and closes the menu. Needs `ScreenshotCapture`. |
| START / STOP SCREEN RECORDING (A) | Records an MJPEG clip to `captures/clip_*.mjpg`. Needs `ScreenshotCapture`. |
| DRAW START | First draw call a capture checkpoints from. Left/Right move by the draw step, **ZL/ZR** by 128. |
| DRAW STEP | Toggles 8 or 1 draw calls between checkpoint images. |
| CAPTURE NEXT DRAW FRAME (A) | Captures the next rendered frame draw by draw (see below) and closes the menu. |
| REBOOT APPLICATION (A) | Restarts the game. |
| CLOSE MENU (A) | Closes the menu. |
| GPU FXAA | Toggles the GPU FXAA edge smoothing pass. |

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
