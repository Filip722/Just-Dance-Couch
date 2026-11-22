# Just Dance Couch

A native Linux x86-64 launcher for locally owned Just Dance Wii backups. It builds the original game code ahead of time and runs it in a fullscreen couch interface.

## Current state

- The fullscreen picker opens on **Just Dance 2014** and includes six playable editions through **Just Dance 2019**. Each edition has an alphabetical five-column grid with three visible rows. Press Right at the end of a row or Left at the start of a row to change editions. A tile shows a still from its locally cached coach video and plays an audio and video preview when highlighted. The launcher warms the edition you are browsing.
- **Just Dance 2014 (USA)** launches all 48 listed routines from the original game. Loopback tests verified scoring in **Just Dance**, **Get Lucky**, and **#thatPOWER**, the **Get Lucky** recap, and return to the selected row after completion and cancellation.
- **Just Dance 2015 (USA)** launches all 42 listed routines from the original game. The launcher starts the 2015 worker when you open its page and keeps the boot and lobby screens hidden. Loopback tests verified scoring in **4x4** and **Happy**, cancellation from **4x4**, and return to the picker after the **Happy** recap.
- **Just Dance 3 (USA, revision 1)** has a native command-line probe for **Pump It** and **Take On Me**, with local audio and coach previews for both. Both songs reached the game’s move-scoring callback. The direct path still renders a black gameplay window, so JD3 is available as a diagnostic.
- **Direct USB Wii Remote mode** claims and releases the Nintendo Wii Bluetooth module. Red SYNC pairing, handoff to Player 1, and motion scoring are verified with a physical Wii Remote in Just Dance 2014. A button wake and reconnect were confirmed after restarting the launcher.
- **Just Dance 2016–2019 (USA)** add 177 Classic disc routines with locally cached previews. Each edition launches its original game directly into the selected dance. Loopback tests reached scoring on **No Control**, **Cheap Thrills**, **24K Magic**, and **Familiar**. Dismissing Familiar’s original recap returned to its selected tile.
- All 17 Wii disc revisions were verified and compiled locally. The remaining 11 editions still need a direct song launch from the picker.

The repository contains the launcher, build scripts, runtime patches, and documentation. Use your own disc backups to build locally. Extracted data, generated title code, saves, binaries, and traces stay in the local cache and data directories.

## Build and run

The build uses Python 3.12 or newer, Clang, CMake, Ninja, pkg-config, FFmpeg, GNU binutils and patch, and the GLFW, SDL3, Vulkan, shaderc, ALSA, Fontconfig, FreeType and libusb-1.0 development libraries. Wii Recomp and the fallback nodtool download are pinned. On this host the cache supplies the `clang-19` shim, the verified nodtool, and Debian development packages staged under `deps/usr`. The build accepts system dependencies on other machines. [Wii Recomp source and requirements](https://github.com/Zontex/wii-recomp/tree/55fb408eb1f2df75e914524dc092e69c70739ded)

```bash
for disc in SJOE41 SE3E41 SJNE41 SZ7E41 SE8E41 S5EE41 SJDE41; do
  python3 -B pilot.py build --disc "$disc"
done
python3 -B pilot.py couch
couch="${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/bin/just-dance-couch"
"$couch" --usb-wii
```

The build reads `/mnt/synology/Games/wii/` by default. Pass `build --image /path/to/owned.rvz` to use a copy of the same verified revision. Derived data and build artifacts live in `${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/`. Isolated saves and controller state live in `${XDG_DATA_HOME:-$HOME/.local/share}/just-dance-couch/`. The app reads extracted disc data externally. The 2014–2019 title registries and the JD3 diagnostic registry are linked into the executable. The worker uses `WII_RECOMP_NATIVE_ONLY=1` and the executable has zero Dolphin dependencies.

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
- The original Classic song databases resolved all 267 picker map keys. After selection, the worker loads that map and invokes the original lobby Start action. The 2015 adapter confirms the hidden Wii Remote grip reminder. In 2016–2019, the lobby skips opening that popup because the player already pressed A in the picker. The adapter still runs the original lobby setup and advances the ready lobby to the phase that accepts its launch event.
- In one fullscreen session, Get Lucky finished and A at the original recap restored its selected row. The same browser then launched Just Dance, and HOME during gameplay restored that row. A missing disc cache also returned to the selected row with an error.
- The original scoring function reported zero for all 167 Get Lucky move evaluations in the still run. The fullscreen loopback run with controlled motion reported 16 nonzero scores out of 167, with a maximum of 0.005639.
- With a fresh guest and a populated RADV shader cache, the first full coach and gameplay HUD image was sampled approximately 50.8 seconds after selection. The first original move score arrived after 54.8 seconds in that run. In that baseline run, before the launcher muted boot audio, the first non-silent ALSA block arrived after 34.1 seconds. The disc track matched captured console PCM at an estimated 50.8 seconds after selection, aligned to that first audio block. [RAKI audio format reference](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_raki.c)
- During a complete RADV Get Lucky probe, 7,156 present intervals between the first score and recap had a median of 33.33 ms, p95 of 33.45 ms and p99 of 33.58 ms. The complete fullscreen loopback ALSA trace and the shorter RADV timing check both recorded zero `EPIPE` events. The runtime's internal audio-ring underruns were not instrumented.
- The focused FIFO and USB tests pass. The wider upstream suite passed 38 of 39 checks with staged SDL 3.2.10. The virtual SDL Pro Controller test aborted in `realloc`.

## Reproduce the native probes

```bash
python3 -B pilot.py boot --fresh --song GetLucky
python3 -B pilot.py boot --fresh --song JustDance
python3 -B pilot.py boot --fresh --disc SJDE41 --song 2dc0bf8b  # Take On Me
python3 -B pilot.py boot --fresh --disc SJDE41 --song 2dc0e3f1  # Pump It
python3 -B pilot.py build --disc SZ7E41
python3 -B pilot.py boot --fresh --disc SZ7E41
```

Look for `original map loader requested`, `original gameplay emitted its first move score`, and `original recap entered`. Local logs are under the cache's `probe/` directory. `SJOE41/get-lucky-ready.log` holds the full score and frame trace. `SJOE41/moving.log` records the controlled motion comparison. `measure/timing/` has the timestamped ALSA and frame sampling evidence. `measure/audio-match.log` records the match against the disc track. `couch-acceptance/` contains the full two-song test harness, log, and screenshots of both restored rows. These browser checks used Xvfb with the Mesa software Vulkan driver.

JD3 needs FIFO breakpoint mode for the title screen. In a stock boot, press A, decline telemetry, and select Dance to open the carousel. The exact-revision build makes the stream pending check and thread suspension atomic with respect to the DVD callback. The local `SJDE41/stream-guard/coverflow.ppm` shows the original carousel after that fix. **Pump It** reached the stock coach and HUD in `SJDE41/take-on-me-confirmed/play.png`.

The JD3 song probe enables the compiled script module shipped on the disc. It looks up the requested key in the original song database and invokes the game's own map launch action. **Take On Me** read its standard choreography at disc offset `0x7ea57188` and reached the original lobby. The original lobby completion callback starts playback and activates the native scoring loop. Loopback runs for both songs reached the original move scorer. The game windows and gameplay captures stayed black. The original results screen and picker return remain unverified. Traces are in `probe/legacy-launch/SJDE41/`. The build caches both songs' local Bink video and stereo DSP audio as eight-second previews. [LyN Wii audio format](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_lyn.c)

An isolated JD4 trace reached the original song catalog after enabling the corresponding compiled module. A later selected-song attempt stopped at an untranslated indirect call. Traces are in `probe/legacy-launch/SJXE41/`.

The expanded launcher uses the same original Classic song lookup and lobby flow in the four later UbiArt editions. The newer catalogs are generated at build time from locally available Classic song descriptors and audio. The one-time preview cache contains coach clips for all 177 songs. In a warmed 2019 A/B test, skipping the hidden grip popup reduced **Milosc W Zakopanem** from 6.1 to 3.9 seconds between selection and fullscreen. The same cold-worker test went from 11.3 to 9.3 seconds. Software tests with the shortcut verified fullscreen scoring and HOME return in all four editions. Dismissing the original 2019 **Un Poco Loco** recap also returned to its selected tile. Timings and test logs are under `probe/launch-timing/`. A full browser test navigated across all six editions, launched **Familiar**, and confirmed return to the selected 2019 tile after the recap. Logs are under `probe/collection/`.

JD2014 initially stalled while opening the Wii microphone service. An empty IOS 57–59 USB vendor manager passes stock initialization. The native game also needs FIFO breakpoint mode. A later memory watchpoint found the graphics write pipe incorrectly wrapping a redirected vertex buffer to address zero. The fix preserves the exact-end FIFO wrap and lets redirected buffers advance beyond the old FIFO limit. [USB v5 reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/IOS/USB/USBV5.cpp) · [Wii gather pipe reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/GPFifo.cpp)

The build checks the USA region, outer disc ID and revision, and boot DOL SHA-256. JD3's inner boot header has revision 0 and the outer disc reports revision 1. JD2014's `BlockFlows_WII.ipk` fails nodtool's optional H0 validation. Nonvalidating extraction completed, and the boot DOL matched its full SHA-256.

## Collection coverage

All 17 backups produced video under the planning reference run and were compiled locally. Native startup traces for the older titles are in `probe/collection/legacy-smoke/`. Each disc is USA revision 0 unless marked otherwise. The local inventory at `probe/catalog/ipk-descriptors.json` counts distinct paths and template keys, including variants and entries whose playability is unknown. `probe/catalog/dol-identities.log` records all 17 exact boot hashes and the verified JD4 ELF segment matches. Each of the 2017–2019 symbol maps contains the original song selection, lobby launch, and move-scoring methods.

| Disc | ID | Routine metadata | Native milestone | Selected dance | Physical pass |
| --- | --- | --- | --- | --- | --- |
| Just Dance | SDNE41 | All file-backed load segments in `EnjimeWii_NOA_g.elf` match the boot DOL | Native translation built | Pending | Pending |
| Just Dance 2 | SD2Y41 | `LynWiiRetail.MAP` and `.sel` present | Native startup passes the archive open after using the on-disc `WD2.bbf` name | Pending | Pending |
| Just Dance 3 | SJDE41 r1 | 73 descriptors in planning, including variants. Take On Me uses `0x2dc0bf8b`. | 32,288 main and 7,923 module functions built. The command-line song probe enables the original compiled scripts. | Pump It renders via the stock menu. Direct selection reaches scoring with a black window. | Pending |
| Just Dance 4 | SJXE41 | `LynWiiRetail.elf` load bytes match the boot DOL. `.sel` present. | Native compiled scripts populate the song catalog in an isolated trace. | Pending | Pending |
| Just Dance 2014 | SJOE41 | 80 descriptor paths. All 48 picker songs resolve in the original Classic database. | 44,819 main functions. Original solo scoring and Get Lucky recap verified in strict-native mode. | Just Dance, Get Lucky, and #thatPOWER | Just Dance scoring |
| Just Dance 2015 | SE3E41 | 63 descriptor paths. All 42 picker songs resolve in the original Classic database. | 50,697 native functions. Direct launch, scoring, and Happy recap verified. | Happy and 4x4 | Pending |
| Just Dance 2016 | SJNE41 | 43 local Classic songs in the picker | Native direct launch and scoring | No Control | Pending |
| Just Dance 2017 | SZ7E41 | 40 local Classic songs in the picker | Native direct launch and scoring | Cheap Thrills | Pending |
| Just Dance 2018 | SE8E41 | 46 local Classic songs in the picker | Native direct launch and scoring | 24K Magic | Pending |
| Just Dance 2019 | S5EE41 | 48 local Classic songs in the picker | Native direct launch, scoring, and original recap | Familiar | Pending |
| Just Dance Disney Party | SJ6E41 | Pending | Native translation built | Pending | Pending |
| Just Dance Disney Party 2 | S5DE41 | Pending | Native translation built | Pending | Pending |
| Just Dance Greatest Hits | SJHE41 | `LynWiiRetail.MAP`, `.elf` and `.sel` present | Native translation built | Pending | Pending |
| Just Dance Kids | SDZE41 | Pending | Native translation built | Pending | Pending |
| Just Dance Kids 2 | SJZE41 | Pending | Native translation built | Pending | Pending |
| Just Dance Kids 2014 | SJ7E41 | Pending | Native translation built | Pending | Pending |
| Just Dance Summer Party | SJ9E41 | `LynWiiRetail.sel` present | Native translation built | Pending | Pending |

The Kids and Disney Party discs use `data_tbl/game_data.arc` and numbered stages. Their song selection and media formats differ from the 2014–2019 UbiArt adapter.

All five DLC WADs contain USA title metadata for Just Dance 2, 3, 4, 2014 and 2015. Offline availability and gameplay remain pending.
