# Just Dance Couch

A native Linux x86-64 launcher for locally owned Just Dance Wii backups. It builds the original game code ahead of time and runs it in a fullscreen couch interface.

## Current state

- The fullscreen picker opens on **Just Dance 2014** with five columns and three visible rows. Scroll down to see the rest of the edition. Press Right at the end of any row to see **Just Dance 2015**. Press Left at the start of a 2015 row to go back. The pages list 48 and 42 disc songs, respectively. Each edition is sorted alphabetically by title. Every thumbnail is a still from the song’s coach video. Highlighting a tile plays its coach video and audio preview. The launcher warms the edition you are browsing.
- **Just Dance 2014 (USA)** launches all 48 listed routines from the original game. Loopback tests verified scoring in **Just Dance**, **Get Lucky**, and **#thatPOWER**, the **Get Lucky** recap, and return to the selected row after completion and cancellation.
- **Just Dance 2015 (USA)** launches all 42 listed routines from the original game. The launcher starts the 2015 worker when you open its page and keeps the boot and lobby screens hidden. Loopback tests verified scoring in **4x4** and **Happy**, cancellation from **4x4**, and return to the picker after the **Happy** recap.
- **Just Dance 3 (USA, revision 1)** reaches its original song carousel in strict-native mode after a disc streaming fix. **Pump It** reached gameplay through the stock menu. **Take On Me** loaded its standard choreography and reached the original lobby.
- **Direct USB Wii Remote mode** claims and releases the Nintendo Wii Bluetooth module. Red SYNC pairing, handoff to Player 1, and motion scoring are verified with a physical Wii Remote in Just Dance 2014. A button wake and reconnect were confirmed after restarting the launcher.
- **Just Dance 2017 (USA)** boots in strict-native mode. A diagnostic probe confirmed **Cheap Thrills** at the original map loader and lobby.

The repository contains the launcher, build scripts, runtime patches, and documentation. Use your own disc backups to build locally. Extracted data, generated title code, saves, binaries, and traces stay in the local cache and data directories.

## Build and run

The build uses Python 3.12 or newer, Clang, CMake, Ninja, pkg-config, FFmpeg, GNU binutils and patch, and the GLFW, SDL3, Vulkan, shaderc, ALSA, Fontconfig, FreeType and libusb-1.0 development libraries. Wii Recomp and the fallback nodtool download are pinned. On this host the cache supplies the `clang-19` shim, the verified nodtool, and Debian development packages staged under `deps/usr`. The build accepts system dependencies on other machines. [Wii Recomp source and requirements](https://github.com/Zontex/wii-recomp/tree/55fb408eb1f2df75e914524dc092e69c70739ded)

```bash
python3 -B pilot.py build --disc SJOE41
python3 -B pilot.py build --disc SE3E41
python3 -B pilot.py build --disc SJDE41
python3 -B pilot.py couch
couch="${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/bin/just-dance-couch"
"$couch" --usb-wii
```

The build reads `/mnt/synology/Games/wii/` by default. Pass `build --image /path/to/owned.rvz` to use a copy of the same verified revision. Derived data and build artifacts live in `${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/`. Isolated saves and controller state live in `${XDG_DATA_HOME:-$HOME/.local/share}/just-dance-couch/`. The app reads extracted disc data externally. The 2014, 2015, and JD3 title registries are linked into the executable. The worker uses `WII_RECOMP_NATIVE_ONLY=1` and the executable has zero Dolphin dependencies.

Game windows enter fullscreen during the handoff from the picker. Each launcher session writes detailed game output to `${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/game-<launcher-pid>.log`. Set `JUST_DANCE_GAME_TRACE=1` to print that trace in the terminal.

Use `"$couch" --loopback` for keyboard verification. The arrow keys select a tile. Enter launches it or dismisses the original recap. Escape in the game runs the HOME cancellation path. `"$couch" --boot SJDE41` boots the embedded JD3 translation for diagnostics.

## Wii Remote

Close Dolphin, connect the Nintendo Wii Bluetooth module (`057e:0305`), and run `"$couch" --usb-wii`. The process needs read and write access to the adapter's USB device. Press the red SYNC button inside one Wii Remote's battery compartment for first-time pairing. The app uses the same limited Bluetooth inquiry as the Wii console. Once paired, wake it with a button press. The picker lights Player 1 when the connection is ready. Pairing data is stored in the app's XDG data directory. Use the D-pad to select a tile, A to start an available song or dismiss results, and HOME to return from a dance. Up and Down scroll through the current edition.

The browser keeps the connection while each game worker runs. Press Escape in the browser to release the module before reopening Dolphin. If Dolphin asks to pair again, use its passthrough SYNC action. The installed backend uses BTstack 1.8.3 under its personal-use license. [BTstack license](https://github.com/bluekitchen/btstack/blob/v1.8.3/LICENSE)

Running `"$couch"` without an option uses the existing BlueZ and hidraw path. It requires read and write access to the Wii Remote's hidraw device and a driver that leaves HID reporting to userspace.

On this host, `--usb-wii` claimed the Nintendo module, left the Intel Bluetooth adapter active, and released the Nintendo module back to `btusb` on exit. A separate Dolphin passthrough run with a fresh profile was able to claim it afterward. In **Just Dance**, the physical remote produced 35 nonzero scores across 43 move evaluations before the game window was closed. A physical A return from the **Get Lucky** recap and physical HOME cancellation remain to be checked.

## Software verification

- The hidden worker ends the initial Wii splash through the game’s own cleanup routine. The Radeon 760M reached a ready song library in 6.8 seconds, down from 27.5 seconds. In software-Vulkan checks with immediate selection, **Get Lucky** appeared in 9.6 seconds and **Just Dance** in 10.1 seconds. Both reached the original scoring function and Escape returned to the selected tile. Prior runs from a ready library showed the dance in about 3.5 seconds. The first scored move in **Just Dance** arrives about 10 seconds after the dance appears. New timings and edition screenshots are in the local `probe/fast-boot/` cache.
- Focus starts a looping eight-second clip after a short selection delay. The build caches the original Wii coach video and a music excerpt locally. It uses the verified preview cues for **Just Dance** and **Get Lucky** and a beat near the middle of the song for the other tracks. The picker stops the preview when a song starts loading. [Wii RAKI audio format](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_raki.c) · [JD2014 preview markers](https://github.com/augustovmedeiros/Just-Dance-Stuff/blob/master/dec_jd14.py)
- After bypassing fixed title intro waits, the 2015 dance appeared after about 14 seconds from a cold worker on the KDE Wayland desktop. The first submitted buffer and subsequent fullscreen configurations stayed at 1920×1200 through scoring. On the isolated test display, a worker warmed while browsing 2015 showed **4x4** in about 6.5 seconds after selection.
- The original Classic song databases resolved all 90 picker map keys. After selection, the worker loads that map and invokes the original lobby Start action. The 2015 adapter also confirms the hidden Wii Remote grip reminder and advances the ready lobby to the phase that accepts its own launch event.
- In one fullscreen session, Get Lucky finished and A at the original recap restored its selected row. The same browser then launched Just Dance, and HOME during gameplay restored that row. A missing disc cache also returned to the selected row with an error.
- The original scoring function reported zero for all 167 Get Lucky move evaluations in the still run. The fullscreen loopback run with controlled motion reported 16 nonzero scores out of 167, with a maximum of 0.005639.
- With a fresh guest and a populated RADV shader cache, the first full coach and gameplay HUD image was sampled approximately 50.8 seconds after selection. The first original move score arrived after 54.8 seconds in that run. In that baseline run, before the launcher muted boot audio, the first non-silent ALSA block arrived after 34.1 seconds. The disc track matched captured console PCM at an estimated 50.8 seconds after selection, aligned to that first audio block. [RAKI audio format reference](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_raki.c)
- During a complete RADV Get Lucky probe, 7,156 present intervals between the first score and recap had a median of 33.33 ms, p95 of 33.45 ms and p99 of 33.58 ms. The complete fullscreen loopback ALSA trace and the shorter RADV timing check both recorded zero `EPIPE` events. The runtime's internal audio-ring underruns were not instrumented.
- The focused FIFO and USB tests pass. The wider upstream suite passed 38 of 39 checks with staged SDL 3.2.10. The virtual SDL Pro Controller test aborted in `realloc`.

## Reproduce the native probes

```bash
python3 -B pilot.py boot --fresh --song GetLucky
python3 -B pilot.py boot --fresh --song JustDance
WII_RECOMP_DISC_LOG=1 WII_RECOMP_DUMP_AFTER=54 \
  python3 -B pilot.py boot --fresh --disc SJDE41
python3 -B pilot.py build --disc SZ7E41
python3 -B pilot.py boot --fresh --disc SZ7E41
```

Look for `original map loader requested`, `original gameplay emitted its first move score`, and `original recap entered`. Local logs are under the cache's `probe/` directory. `SJOE41/get-lucky-ready.log` holds the full score and frame trace. `SJOE41/moving.log` records the controlled motion comparison. `measure/timing/` has the timestamped ALSA and frame sampling evidence. `measure/audio-match.log` records the match against the disc track. `couch-acceptance/` contains the full two-song test harness, log, and screenshots of both restored rows. These browser checks used Xvfb with the Mesa software Vulkan driver.

JD3 needs FIFO breakpoint mode for the title screen. Press A to advance, decline telemetry, and select Dance to reach the original carousel. The baseline `SJDE41/stream/trace.log` captured a successful 0x6a00-byte read and its completion callback followed by a thread that stayed suspended. The exact-revision build makes the pending check and thread suspension atomic with respect to the DVD callback. `SJDE41/stream-guard/trace.log` and its `coverflow.ppm` capture show the original carousel with **California Gurls** selected. A diagnostic selection of **Take On Me** read the standard choreography from `JD3.bf` at disc offset `0x7ea57188` and reached the original lobby. **Pump It** reached the original coach and gameplay HUD in a separate loopback stock-menu probe. Launcher-driven selection and recap are pending. The original module exports include `MenuOption_LaunchMap` and `MenuPage_Lobby_StartLoading`.

JD2017's exact US revision built 47,084 native functions and reached its Dancer Card setup after A at the title screen. A cache-only launch probe resolved **Cheap Thrills** through the original Classic song database and verified the same entry at the map loader. It reached the original lobby after the first-run Wii Remote instructions. Scoring and recap remain pending. Captures and logs are under `SZ7E41/`.

JD2014 initially stalled while opening the Wii microphone service. An empty IOS 57–59 USB vendor manager passes stock initialization. The native game also needs FIFO breakpoint mode. A later memory watchpoint found the graphics write pipe incorrectly wrapping a redirected vertex buffer to address zero. The fix preserves the exact-end FIFO wrap and lets redirected buffers advance beyond the old FIFO limit. [USB v5 reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/IOS/USB/USBV5.cpp) · [Wii gather pipe reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/GPFifo.cpp)

The build checks the USA region, outer disc ID and revision, and boot DOL SHA-256. JD3's inner boot header has revision 0 and the outer disc reports revision 1. JD2014's `BlockFlows_WII.ipk` fails nodtool's optional H0 validation. Nonvalidating extraction completed, and the boot DOL matched its full SHA-256.

## Collection coverage

All 17 backups produced video under the planning reference run. Each disc is USA revision 0 unless marked otherwise. The local inventory at `probe/catalog/ipk-descriptors.json` counts distinct paths and template keys, including variants and entries whose playability is unknown. `probe/catalog/dol-identities.log` records all 17 exact boot hashes and the verified JD4 ELF segment matches. Each of the 2017–2019 symbol maps contains the original song selection, lobby launch, and move-scoring methods.

| Disc | ID | Routine metadata | Native milestone | Selected dance | Physical pass |
| --- | --- | --- | --- | --- | --- |
| Just Dance | SDNE41 | `EnjimeWii_NOA_g.elf` present | Pending | Pending | Pending |
| Just Dance 2 | SD2Y41 | `LynWiiRetail.MAP` and `.sel` present | Pending | Pending | Pending |
| Just Dance 3 | SJDE41 r1 | 73 descriptors in planning, including variants. Take On Me uses `0x2dc0bf8b`. | 32,288 main and 7,923 module functions built. Original song carousel verified after fixing a disc callback race. | Pump It gameplay. Take On Me through lobby. | Pending |
| Just Dance 4 | SJXE41 | `LynWiiRetail.elf` load bytes match the boot DOL. `.sel` present. | Pending | Pending | Pending |
| Just Dance 2014 | SJOE41 | 80 descriptor paths. All 48 picker songs resolve in the original Classic database. | 44,819 main functions. Original solo scoring and Get Lucky recap verified in strict-native mode. | Just Dance, Get Lucky, and #thatPOWER | Just Dance scoring |
| Just Dance 2015 | SE3E41 | 63 descriptor paths. All 42 picker songs resolve in the original Classic database. | 50,697 native functions. Direct launch, scoring, and Happy recap verified. | Happy and 4x4 | Pending |
| Just Dance 2016 | SJNE41 | 73 candidate music-track template keys | Pending | Pending | Pending |
| Just Dance 2017 | SZ7E41 | 59 candidate music-track template keys. `wii_ua_engine_f.MAP` present. | 47,084 native functions. Original map loader and lobby verified with a diagnostic probe. | Cheap Thrills through lobby | Pending |
| Just Dance 2018 | SE8E41 | 54 candidate music-track template keys. `wii_ua_engine_f.MAP` present. | Pending | Pending | Pending |
| Just Dance 2019 | S5EE41 | 55 candidate music-track template keys. `wii_ua_engine_f.MAP` present. | Pending | Pending | Pending |
| Just Dance Disney Party | SJ6E41 | Pending | Pending | Pending | Pending |
| Just Dance Disney Party 2 | S5DE41 | Pending | Pending | Pending | Pending |
| Just Dance Greatest Hits | SJHE41 | `LynWiiRetail.MAP`, `.elf` and `.sel` present | Pending | Pending | Pending |
| Just Dance Kids | SDZE41 | Pending | Pending | Pending | Pending |
| Just Dance Kids 2 | SJZE41 | Pending | Pending | Pending | Pending |
| Just Dance Kids 2014 | SJ7E41 | Pending | Pending | Pending | Pending |
| Just Dance Summer Party | SJ9E41 | `LynWiiRetail.sel` present | Pending | Pending | Pending |

All five DLC WADs contain USA title metadata for Just Dance 2, 3, 4, 2014 and 2015. Offline availability and gameplay remain pending.
