# Just Dance Couch

A native Linux x86-64 launcher for locally owned Just Dance Wii backups. It builds the original game code ahead of time and runs it in a fullscreen couch interface.

## Current state

- **Just Dance 2014 (USA)** plays **Get Lucky** and **Just Dance**. Loopback tests verified original motion scoring, the Get Lucky recap, and return to the selected row after both completion and HOME cancellation.
- **Just Dance 3 (USA, revision 1)** reaches its original main menu in diagnostic mode. Selecting Dance stalls in audio streaming, so **Take On Me** is still pending.
- **Physical Wii Remote testing, additional editions, and DLC** remain pending. See the coverage table below for per-disc progress.

The repository contains the launcher, build scripts, runtime patches, and documentation. Use your own disc backups to build locally. Extracted data, generated title code, saves, binaries, and traces stay in the local cache and data directories.

## Build and run

The build uses Python 3.12 or newer, Clang, Ninja, GNU binutils and patch, and the GLFW, SDL3, Vulkan, shaderc and ALSA development libraries. Wii Recomp and the fallback nodtool download are pinned. On this host the cache supplies the `clang-19` shim, the verified nodtool, and Debian development packages staged under `deps/usr`. The build accepts system dependencies on other machines. [Wii Recomp source and requirements](https://github.com/Zontex/wii-recomp/tree/55fb408eb1f2df75e914524dc092e69c70739ded)

```bash
python3 -B pilot.py build --disc SJOE41
python3 -B pilot.py build --disc SJDE41
python3 -B pilot.py couch
couch="${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/bin/just-dance-couch"
"$couch"
```

The build reads `/mnt/synology/Games/wii/` by default. Pass `build --image /path/to/owned.rvz` to use a copy of the same verified revision. Derived data and build artifacts live in `${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/`. Isolated saves and controller state live in `${XDG_DATA_HOME:-$HOME/.local/share}/just-dance-couch/`. The app reads extracted disc data externally. Both title registries are linked into the executable. The worker uses `WII_RECOMP_NATIVE_ONLY=1` and the executable has zero Dolphin dependencies.

Use `"$couch" --loopback` for keyboard verification. Up and Down select a row. Enter launches it or dismisses the original recap. Escape in the game runs the HOME cancellation path. `"$couch" --boot SJDE41` boots the embedded JD3 translation for diagnostics.

## Wii Remote

The browser expects a BlueZ-connected Nintendo Wii Remote with read and write access to its `hidraw` device and a driver that leaves the HID reports to userspace. It keeps the open device while a worker runs. Up and Down select the row, A starts or dismisses results, and HOME cancels a dance. The RADV host had a powered adapter and zero connected Bluetooth devices during testing. Permissions, wake, reconnect and physical scoring are pending. The upstream setup script was reviewed and its system-wide `hid_wiimote` blacklist was left unapplied. The browser excludes devices claimed by that driver because both readers could otherwise send competing output reports.

For the couch test, connect one remote, finish **Get Lucky** and press A at the original results, then select **Just Dance** and press HOME while dancing. Check that each action restores the corresponding row and that the remote stays connected. The cross-title step remains blocked on a verified JD3 dance launch.

## Software verification

- Fresh boots confirmed the chosen disc map at the original loader for both songs. The adapter uses the original Classic song lookup, waits for one connected player, and invokes the original Start action when it becomes available. There is no user input or preview wait between selection and gameplay.
- In one fullscreen session, Get Lucky finished and A at the original recap restored its selected row. The same browser then launched Just Dance, and HOME during gameplay restored that row. A missing disc cache also returned to the selected row with an error.
- The original scoring function reported zero for all 167 Get Lucky move evaluations in the still run. The fullscreen loopback run with controlled motion reported 16 nonzero scores out of 167, with a maximum of 0.005639.
- With a fresh guest and a populated RADV shader cache, the first full coach and gameplay HUD image was sampled approximately 50.8 seconds after selection. The first original move score arrived after 54.8 seconds in that run. The first non-silent ALSA block, during the intro, arrived after 34.1 seconds. The disc track matched captured console PCM at an estimated 50.8 seconds after selection, aligned to that first audio block. [RAKI audio format reference](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_raki.c)
- During a complete RADV Get Lucky probe, 7,156 present intervals between the first score and recap had a median of 33.33 ms, p95 of 33.45 ms and p99 of 33.58 ms. The complete fullscreen loopback ALSA trace and the shorter RADV timing check both recorded zero `EPIPE` events. The runtime's internal audio-ring underruns were not instrumented.
- The focused FIFO and USB tests pass. The wider upstream suite passed 38 of 39 checks with staged SDL 3.2.10. The virtual SDL Pro Controller test aborted in `realloc`.

## Reproduce the native probes

```bash
python3 -B pilot.py boot --fresh --song GetLucky
python3 -B pilot.py boot --fresh --song JustDance
WII_RECOMP_DISC_LOG=1 WII_RECOMP_DUMP_AFTER=54 \
  python3 -B pilot.py boot --fresh --disc SJDE41
```

Look for `original map loader requested`, `original gameplay emitted its first move score`, and `original recap entered`. Local logs are under the cache's `probe/` directory. `SJOE41/get-lucky-ready.log` holds the full score and frame trace. `SJOE41/moving.log` records the controlled motion comparison. `measure/timing/` has the timestamped ALSA and frame sampling evidence. `measure/audio-match.log` records the match against the disc track. `couch-acceptance/` contains the full two-song test harness, log, and screenshots of both restored rows. These browser checks used Xvfb with the Mesa software Vulkan driver.

JD3 needed FIFO breakpoint mode to render the title screen. In the embedded build, the original A action advanced to the telemetry question. Declining it reached the original main menu. Selecting Dance then stalled before visible song selection. `SJDE41/menu.log` records the last disc-audio request at `0x99d5f870` and the guest threads waiting in the streaming path. `SJDE41/coverflow-io.log` reproduced the wait with host audio enabled, a successful IOS read reply, and an empty IPC reply queue. `SJDE41/lyn-symbol-comparison.log` matches the sampled calls to LyN graphics, texture loading, and disc streaming functions. `SJDE41/menu-post-decline.png` shows the main menu. The trace reads `LynWiiRetail.sel` and never reaches the `Ai2CppWiiFinal.rso` file. The module export table names the original `MenuOption_LaunchMap` and `MenuPage_Lobby_StartLoading` entry points.

JD2014 initially stalled while opening the Wii microphone service. An empty IOS 57–59 USB vendor manager passes stock initialization. The native game also needs FIFO breakpoint mode. A later memory watchpoint found the graphics write pipe incorrectly wrapping a redirected vertex buffer to address zero. The fix preserves the exact-end FIFO wrap and lets redirected buffers advance beyond the old FIFO limit. [USB v5 reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/IOS/USB/USBV5.cpp) · [Wii gather pipe reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/GPFifo.cpp)

The build checks the USA region, outer disc ID and revision, and boot DOL SHA-256. JD3's inner boot header has revision 0 and the outer disc reports revision 1. JD2014's `BlockFlows_WII.ipk` fails nodtool's optional H0 validation. Nonvalidating extraction completed, and the boot DOL matched its full SHA-256.

## Collection coverage

All 17 backups produced video under the planning reference run. Each disc is USA revision 0 unless marked otherwise. The local inventory at `probe/catalog/ipk-descriptors.json` counts distinct paths and template keys, including variants and entries whose playability is unknown. `probe/catalog/dol-identities.log` records all 17 exact boot hashes and the verified JD4 ELF segment matches. Each of the 2017–2019 symbol maps contains the original song selection, lobby launch, and move-scoring methods.

| Disc | ID | Routine metadata | Native milestone | Selected dance | Physical pass |
| --- | --- | --- | --- | --- | --- |
| Just Dance | SDNE41 | `EnjimeWii_NOA_g.elf` present | Pending | Pending | Pending |
| Just Dance 2 | SD2Y41 | `LynWiiRetail.MAP` and `.sel` present | Pending | Pending | Pending |
| Just Dance 3 | SJDE41 r1 | 73 descriptors in planning, including variants. Take On Me uses `0x2dc0bf8b`. | 32,288 main and 7,923 module functions built. Original main menu renders. Song selection stalls in disc audio streaming. | Pending | Pending |
| Just Dance 4 | SJXE41 | `LynWiiRetail.elf` load bytes match the boot DOL. `.sel` present. | Pending | Pending | Pending |
| Just Dance 2014 | SJOE41 | 80 descriptor paths. Two Classic routines verified in game. | 44,819 main functions. Original solo scoring and Get Lucky recap verified in strict-native mode. | Just Dance and Get Lucky | Pending |
| Just Dance 2015 | SE3E41 | 63 descriptor paths | Pending | Pending | Pending |
| Just Dance 2016 | SJNE41 | 73 candidate music-track template keys | Pending | Pending | Pending |
| Just Dance 2017 | SZ7E41 | 59 candidate music-track template keys. `wii_ua_engine_f.MAP` present. | Pending | Pending | Pending |
| Just Dance 2018 | SE8E41 | 54 candidate music-track template keys. `wii_ua_engine_f.MAP` present. | Pending | Pending | Pending |
| Just Dance 2019 | S5EE41 | 55 candidate music-track template keys. `wii_ua_engine_f.MAP` present. | Pending | Pending | Pending |
| Just Dance Disney Party | SJ6E41 | Pending | Pending | Pending | Pending |
| Just Dance Disney Party 2 | S5DE41 | Pending | Pending | Pending | Pending |
| Just Dance Greatest Hits | SJHE41 | `LynWiiRetail.MAP`, `.elf` and `.sel` present | Pending | Pending | Pending |
| Just Dance Kids | SDZE41 | Pending | Pending | Pending | Pending |
| Just Dance Kids 2 | SJZE41 | Pending | Pending | Pending | Pending |
| Just Dance Kids 2014 | SJ7E41 | Pending | Pending | Pending | Pending |
| Just Dance Summer Party | SJ9E41 | `LynWiiRetail.sel` present | Pending | Pending | Pending |

The five DLC WADs remain pending region and offline checks.
