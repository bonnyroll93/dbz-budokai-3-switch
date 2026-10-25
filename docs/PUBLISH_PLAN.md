# Publication preparation

The local restructuring for `dbz-budokai-3-switch` is complete. This directory currently has no `.git` metadata. Initialize or attach a GitHub repository before publishing.

## Repository contents

The public tree contains the DBZ3 Switch port, build and import scripts, documentation, the MIT license, and a patch plus overlay for a pinned ps3recomp revision. `scripts/setup/setup.ps1` retrieves the dependencies and applies the local changes.

The following stay local and are ignored by Git:

| Directory | Contents |
|---|---|
| `userdata/` | User supplied game PKG, decrypted game files, PS3 firmware, game data, and local RPCS3 installation |
| `generated/` | Lifted PPU and SPU code, image metadata, and game artwork |
| `third_party/` | Local dependency checkouts and downloaded tools |
| `build/` | Build trees, logs, packages, and staging files |
| `shaders/deko3d/glsl/` | Generated GLSL from the captured HLSL sources |

The source port is in `ports/dbz3/`. Captured HLSL under `shaders/deko3d/hlsl/` is a build input and can be committed. The generated shader header stays local. Generated PPU and SPU files are recreated under `generated/dbz3/`.

## Setup and builds

1. Run `scripts/setup/setup.ps1` on Windows. It prepares the pinned ps3recomp checkout, Python environment, RPCS3 binary, and Switch packaging tools.
2. Supply your own legitimate BLUS30966 PKG and `PS3UPDAT.PUP` to `scripts/setup/import-game.ps1`. The script installs them through RPCS3, verifies the DBZ3 ELF hash, and generates the port's PPU and SPU output.
3. Run `scripts/build/deploy-gamedata.ps1` for a mounted SD card, or `scripts/build/deploy-gamedata-mtp.ps1` through DBI MTP, then run `scripts/build/switch-dev.ps1`. Copy and install the resulting NSP manually.
4. Run the game on Switch, start DBI MTP, and run `scripts/collect-switch-shaders.ps1` to add new verified HLSL to `shaders/deko3d/hlsl/`. Rebuild the NSP to use them.
5. Run `scripts/build/switch-full.ps1` for an NSP with the DBZ3 game data in RomFS. Copy and install it manually; the runtime reads `romfs:/game/` first and can fall back to the SD directory. Saves and diagnostics remain on the SD card.

See [README.md](../README.md) and [build-layout.md](build-layout.md) for the current commands and directory layout.

## Checks before publication

- Run `scripts/check-clean.ps1` before adding files to Git. The pre-commit hook in `.githooks/` runs the same check.
- Reproduce a clean setup and import from only the documented PKG and PUP inputs. A local re-lift and shader generation check have already been performed, but a clean clone has not been tested end to end.
- Continue Switch hardware testing with the current source. Rendering and combat frame rate are still under investigation.
- Review the staged file list before any future commit or publication. Game files, translated game code, firmware, keys, and game derived artwork must remain outside the repository.

The public GitHub repository should contain this directory's source, scripts, documentation, patch, and verified HLSL in `shaders/deko3d/hlsl/`. Do not attach NSPs or game data to releases.
