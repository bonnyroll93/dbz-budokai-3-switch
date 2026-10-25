# dbz-budokai-3-switch

This tree contains the port, build tools, and local patches to the MIT-licensed
ps3recomp toolkit. It does **not** contain the game, Sony firmware, translated
game code, or game artwork. Supply a legitimate BLUS30966 copy of Dragon Ball Z
Budokai HD Collection and the official PS3 firmware yourself. The game and
Dragon Ball trademarks belong to their respective owners. The MIT license
applies to this project's own code; dependencies retain their own licenses.

This port is experimental. Switch rendering and combat performance are still
being tested; no game assets or installable packages are distributed here.

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
