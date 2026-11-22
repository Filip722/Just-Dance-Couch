# Just Dance Couch

A native Linux x86-64 launcher for locally owned Just Dance Wii backups. It builds the original game code ahead of time and runs it in a fullscreen couch interface.

## Current state

- The fullscreen picker spans 12 Wii editions in release order, from the original **Just Dance** to **Just Dance 2019**. With the local add-on packs loaded, it shows 442 disc tiles and 66 DLC tiles. Each edition has an alphabetical five-column grid with three visible rows and coach and music previews on every tile. The launcher warms the current edition while you browse.
- **Just Dance 2014 (USA)** supports 48 Classic routines in the original game. Loopback tests verified scoring in **Just Dance**, **Get Lucky**, and **#thatPOWER**, the **Get Lucky** recap, and return to the selected row after completion and cancellation.
- **Just Dance 2015 (USA)** supports 42 Classic routines in the original game. The launcher starts the 2015 worker when you open its page and keeps the boot and lobby screens hidden. Loopback tests verified scoring in **4x4** and **Happy**, cancellation from **4x4**, and return to the picker after the **Happy** recap.
- **Just Dance 3 (USA, revision 1)** adds 47 Classic routines with local coach and music previews. Pump It and Take On Me launched their matching fullscreen routines. Escape returned to their tiles. A complete Pump It run reached the original recap and returned to the same tile on Enter.
- **Direct USB Wii Remote mode** claims and releases the Nintendo Wii Bluetooth module. Red SYNC pairing, handoff to Player 1, and motion scoring are verified with a physical Wii Remote in Just Dance 2014. A button wake and reconnect were confirmed after restarting the launcher.
- **Just Dance 2016–2019 (USA)** add 177 Classic disc routines with locally cached previews. Each edition launches its original game directly into the selected dance. Loopback tests reached scoring on **No Control**, **Cheap Thrills**, **24K Magic**, and **Familiar**. Dismissing Familiar’s original recap returned to its selected tile.
- **Just Dance (USA)** supports 32 original routines with local coach and music previews. The software checks recorded scoring and the original recap. Some test runs reached the recap early.
- **Just Dance 2 (USA)** supports 47 original routines with local coach and music previews. The original game launched Cosmic Girl, Rasputin, and Jai Ho from their native keys. Cosmic Girl reached motion scoring, and Soul Bossa Nova reached the original recap. Cancelling Cosmic Girl and Jai Ho returned to their tiles.
- **Just Dance Summer Party (USA)** supports 23 original routines with local coach and music previews. Pon De Replay scored with simulated motion, and Escape returned to its tile. Firework reached original gameplay in an isolated stock launch.
- **Just Dance Greatest Hits (USA)** adds 37 alphabetical songs with local coach and music previews. Rasputin and Jai Ho opened their original fullscreen routines in loopback. Escape restored the selected tile.
- **Just Dance 4 (USA)** adds 44 local Classic routines with MobiClip coach previews and music. The original gameplay and motion feedback were verified in Disturbia and We No Speak Americano. Disturbia opened fullscreen from the picker, and Escape restored its tile.
- The three Kids and two Disney Party editions are outside this pass. This library has no Just Dance 2020 backup. The picker uses on-disc Classic routines where the edition has multiple modes.

The repository contains the launcher, build scripts, runtime patches, and documentation. Use your own disc backups to build locally. Extracted data, generated title code, saves, binaries, and traces stay in the local cache and data directories.

## Picker controls

- Tap the D-pad to move one tile or hold it to keep moving.
- Press Up from the first song row to focus the edition name. Left and Right switch editions, and Down or A returns focus to the songs. The grid edges also change editions.
- Press A on a song tile to play it, and HOME to return from a dance.

Summer Party sits between Just Dance 2 and Just Dance 3. Greatest Hits sits between Just Dance 3 and Just Dance 4. [Summer Party launch trailer](https://www.youtube.com/watch?v=7QLdVGqh75M) · [Greatest Hits launch trailer](https://www.youtube.com/watch?v=E_HiCMU8TIs)

## Launch and audio checks

The picker starts one hidden worker for the current edition. The six older editions run their hidden boot sequences at three times the normal clock and return to the normal clock before the dance. A worker can wait at its song catalog while you browse. Just Dance 2015 uses the same grip-reminder shortcut as 2016–2019.

The following cold launch times run from selection to fullscreen in loopback on a private Wayland display with the Radeon 760M. Each row uses the same song before and after.

| Edition | Song | Previous | Current |
| --- | --- | ---: | ---: |
| Just Dance | Girls Just Want To Have Fun | 19.2 s | 7.3 s |
| Just Dance 2 | Big Girl (You Are Beautiful) | 41.3 s | 18.1 s |
| Just Dance Summer Party | Nine In The Afternoon | 41.2 s | 17.8 s |
| Just Dance Greatest Hits | Rasputin | 44.6 s | 17.5 s |
| Just Dance 3 | Pump It | 48.2 s | 18.7 s |
| Just Dance 4 | Disturbia | 79.5 s | 27.2 s |
| Just Dance 2014 | Get Lucky | 10.0 s | 10.0 s |
| Just Dance 2015 | Happy | 13.6 s | 11.4 s |
| Just Dance 2016 | No Control | 11.2 s | 10.9 s |
| Just Dance 2017 | Cheap Thrills | 10.7 s | 10.8 s |
| Just Dance 2018 | 24K Magic | 11.1 s | 10.9 s |
| Just Dance 2019 | Familiar | 9.2 s | 9.3 s |

After the catalog warmed up, Girls Just Want To Have Fun appeared in about 6.6 seconds in the picker test, and Big Girl appeared in 10.4 seconds. Escape returned to both selected tiles. Disturbia appeared in 13.0 seconds after a 12-second pause at the ready catalog. The loopback sweep captured gameplay audio in every edition. The two original games also played through an isolated PipeWire sink without ALSA write errors at the default render scale. The audio ring now spreads each one-sample clock correction over the output period. With a 1 kHz reference tone and one correction per 384-frame period, RMS error fell from 1,884 to 60 in signed 16-bit sample units.

The Linux audio output starts with a 40 ms ALSA buffer and retries with 100 ms after its first underrun. On a private 48 kHz PipeWire graph with 2,048-frame periods, the 40 ms configuration failed before Jailhouse Rock started. The retry restored 24 seconds of uninterrupted gameplay audio in the capture. The mixer output also matched the disc track in comparisons during gameplay.

The runtime waits for the Wii audio DMA interrupt handler to finish preparing the next buffer before catching up. This prevents repeated 3 ms audio buffers during gameplay. In a ten-second sample of **Big Girl (You Are Beautiful)**, waveform correlation with the disc track improved from 0.516 to 0.999996. The private PipeWire sweep captured gameplay audio from all twelve disc editions and the Just Dance 4 and 2015 DLC copies of **Gangnam Style** before duplicate filtering.

## Build and run

The build uses Python 3.12 or newer, Clang, CMake, Ninja, pkg-config, FFmpeg, GNU binutils and patch, and the GLFW, SDL3, Vulkan, shaderc, ALSA, Fontconfig, FreeType and libusb-1.0 development libraries. Wii Recomp and the fallback nodtool download are pinned. On this host the cache supplies the `clang-19` shim, the verified nodtool, and Debian development packages staged under `deps/usr`. The build accepts system dependencies on other machines. [Wii Recomp source and requirements](https://github.com/Zontex/wii-recomp/tree/55fb408eb1f2df75e914524dc092e69c70739ded)

```bash
for disc in SJOE41 SE3E41 SJNE41 SZ7E41 SE8E41 S5EE41 SDNE41 SD2Y41 SJ9E41 SJHE41 SJDE41 SJXE41; do
  python3 -B pilot.py build --disc "$disc"
done
python3 -B pilot.py couch
couch="${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/bin/just-dance-couch"
"$couch" --usb-wii
```

The build reads `/mnt/synology/Games/wii/` by default. Pass `build --image /path/to/owned.rvz` to use a copy of the same verified revision. Derived data and build artifacts live in `${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/`. Isolated saves and controller state live in `${XDG_DATA_HOME:-$HOME/.local/share}/just-dance-couch/`. The app reads extracted disc data externally. All 12 playable editions are linked into the executable. The worker uses `WII_RECOMP_NATIVE_ONLY=1` and the executable has zero Dolphin dependencies.

### Wii DLC

`python3 -B pilot.py couch` imports owned USA add-on WADs from `/mnt/synology/Games/wii/DLC/`. Use `couch --dlc-dir /path/to/DLC` for another folder. The first import requires Python `cryptography` and verifies each content file against its Wii title metadata. The build caches the imported contents, thumbnails, and eight-second audio and video previews. The launcher reuses them on startup.

Choose **DLC** after **Just Dance 2019** in the edition picker. The shared page sorts by song title and labels each tile with its source game. When a song appears in multiple games, the picker keeps the newest available edition across disc and DLC. Within an edition, it prefers the standard routine and uses the disc copy when it offers that routine. The retained **Gangnam Style** tile uses **Just Dance 2015**. The selected title boots with the requested download and any shared DLC catalog it needs. D-pad, A, and HOME work the same way as on the disc pages.

All 135 routines reached their matching fullscreen dance in native loopback tests. The picker returned to the selected **Part of Me** tile after cancelling the Just Dance 4 routine. The Just Dance 3 **Jambo Mambo** test reached original move scoring and produced gameplay audio through an isolated PipeWire sink.

| USA Wii pack | Routines |
|---|---:|
| Just Dance 2 | 25 |
| Just Dance 3 | 37 |
| Just Dance 4 | 21 |
| Just Dance 2014 | 27 |
| Just Dance 2015 | 25 |
| **Total** | **135** |

The 2014 pack's original catalog names **We Can’t Stop** in slot 4. The current content for that slot is a placeholder. The playable routine is in the 2015 pack. The five WADs cover the USA Wii add-on packs found for the editions in this launcher. Just Dance Unlimited serves a separate streaming catalog on supported platforms such as Wii U. [Ubisoft's Just Dance 2016 announcement](https://news.ubisoft.com/en-us/article/5elZ5Kvty0JCVRuUpjhrOt/new-content-for-just-dance-unlimited) · [Ubisoft's Just Dance 2019 launch](https://news.ubisoft.com/en-us/article/01NgKj4HnqqNuKzZbuVfIR/just-dance-2019-available-now)

Game windows enter fullscreen during the handoff from the picker. Each launcher session writes detailed game output to `${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/game-<launcher-pid>.log`. Set `JUST_DANCE_GAME_TRACE=1` to print that trace in the terminal.

Use `"$couch" --loopback` for keyboard verification. The arrow keys select a tile. Enter launches it or dismisses the original recap. Escape in the game runs the HOME cancellation path. `"$couch" --boot SJDE41` boots the embedded JD3 translation for diagnostics.

## Wii Remote

Close Dolphin, connect the Nintendo Wii Bluetooth module (`057e:0305`), and run `"$couch" --usb-wii`. The process needs read and write access to the adapter's USB device. Press the red SYNC button inside one Wii Remote's battery compartment for first-time pairing. The app uses the same limited Bluetooth inquiry as the Wii console. Once paired, wake it with a button press. The picker lights Player 1 when the connection is ready. Pairing data is stored in the app's XDG data directory. Use the D-pad to select a tile, A to start an available song or dismiss results, and HOME to return from a dance. Up and Down scroll through the current edition.

The browser keeps the connection while each game worker runs. Press Escape in the browser to release the module before reopening Dolphin. If Dolphin asks to pair again, use its passthrough SYNC action. The installed backend uses BTstack 1.8.3 under its personal-use license. [BTstack license](https://github.com/bluekitchen/btstack/blob/v1.8.3/LICENSE)

Running `"$couch"` without an option uses the existing BlueZ and hidraw path. It requires read and write access to the Wii Remote's hidraw device and a driver that leaves HID reporting to userspace.

On this host, `--usb-wii` claimed the Nintendo module, left the Intel Bluetooth adapter active, and released the Nintendo module back to `btusb` on exit. A separate Dolphin passthrough run with a fresh profile was able to claim it afterward. In **Just Dance**, the physical remote produced 35 nonzero scores across 43 move evaluations before the game window was closed. A physical A return from the **Get Lucky** recap and physical HOME cancellation remain to be checked.

## Software verification

- The hidden worker ends the initial Wii splash through the game’s own cleanup routine. The Radeon 760M reached a ready song library in 6.8 seconds, down from 27.5 seconds. In software-Vulkan checks with immediate selection, **Get Lucky** appeared in 9.6 seconds and **Just Dance** in 10.1 seconds. Both reached the original scoring function and Escape returned to the selected tile. Prior runs from a ready library showed the dance in about 3.5 seconds. The first scored move in **Just Dance** arrives about 10 seconds after the dance appears. New timings and edition screenshots are in the local `probe/fast-boot/` cache.
- Focus starts a looping eight-second clip after a short selection delay. The build caches coach video and music excerpts locally for all twelve editions. It uses the verified preview cues for **Just Dance** and **Get Lucky** and a beat near the middle of the song for the other tracks. The picker stops the preview when a song starts loading. [Wii RAKI audio format](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_raki.c) · [JD2014 preview markers](https://github.com/augustovmedeiros/Just-Dance-Stuff/blob/master/dec_jd14.py)
- After bypassing fixed title intro waits, the 2015 dance appeared after about 14 seconds from a cold worker on the KDE Wayland desktop. The first submitted buffer and subsequent fullscreen configurations stayed at 1920×1200 through scoring. On the isolated test display, a worker warmed while browsing 2015 showed **4x4** in about 6.5 seconds after selection.
- The 2014–2019 Classic song databases resolved all 267 keys used by their pages. After selection, the worker loads that map and invokes the original lobby Start action. In 2015–2019, the lobby skips opening the Wii Remote grip reminder because the player already pressed A in the picker. The adapter still runs the original lobby setup and advances the ready lobby to the phase that accepts its launch event.
- In one fullscreen session, Get Lucky finished and A at the original recap restored its selected row. The same browser then launched Just Dance, and HOME during gameplay restored that row. A missing disc cache also returned to the selected row with an error.
- The original scoring function reported zero for all 167 Get Lucky move evaluations in the still run. The fullscreen loopback run with controlled motion reported 16 nonzero scores out of 167, with a maximum of 0.005639.
- With a fresh guest and a populated RADV shader cache, the first full coach and gameplay HUD image was sampled approximately 50.8 seconds after selection. The first original move score arrived after 54.8 seconds in that run. In that baseline run, before the launcher muted boot audio, the first non-silent ALSA block arrived after 34.1 seconds. The disc track matched captured console PCM at an estimated 50.8 seconds after selection, aligned to that first audio block. [RAKI audio format reference](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_raki.c)
- During a complete RADV Get Lucky probe, 7,156 present intervals between the first score and recap had a median of 33.33 ms, p95 of 33.45 ms and p99 of 33.58 ms. The complete fullscreen loopback ALSA trace and the shorter RADV timing check both recorded zero `EPIPE` events. The runtime's internal audio-ring underruns were not instrumented.
- The focused FIFO and USB tests pass. The wider upstream suite passed 38 of 39 checks with staged SDL 3.2.10. The virtual SDL Pro Controller test aborted in `realloc`.

## Reproduce the native probes

```bash
python3 -B pilot.py boot --fresh --song GetLucky
python3 -B pilot.py boot --fresh --song JustDance
python3 -B pilot.py boot --fresh --disc SDNE41 --song 18       # DARE
python3 -B pilot.py build --disc SD2Y41
python3 -B pilot.py boot --fresh --disc SD2Y41 --song 908020bd  # Cosmic Girl
python3 -B pilot.py build --disc SJ9E41
python3 -B pilot.py boot --fresh --disc SJ9E41 --song 64304716  # Pon De Replay
python3 -B pilot.py build --disc SJHE41
python3 -B pilot.py boot --fresh --disc SJHE41 --song 76c06ae0  # Rasputin
python3 -B pilot.py boot --fresh --disc SJDE41 --song 2dc0bf8b  # Take On Me
python3 -B pilot.py boot --fresh --disc SJDE41 --song 2dc0e3f1  # Pump It
python3 -B pilot.py build --disc SJXE41
python3 -B pilot.py boot --fresh --disc SJXE41 --song 2dc1c05c  # Disturbia
python3 -B pilot.py build --disc SZ7E41
python3 -B pilot.py boot --fresh --disc SZ7E41
```

Look for `original map loader requested`, `original gameplay emitted its first move score`, and `original recap entered`. Local logs are under the cache's `probe/` directory. `SJOE41/get-lucky-ready.log` holds the full score and frame trace. `SJOE41/moving.log` records the controlled motion comparison. `measure/timing/` has the timestamped ALSA and frame sampling evidence. `measure/audio-match.log` records the match against the disc track. `couch-acceptance/` contains the full two-song test harness, log, and screenshots of both restored rows. These browser checks used Xvfb with the Mesa software Vulkan driver.

Just Dance 3 and Greatest Hits use the original carousel and lobby to open a selected song. They place the requested song first in the local catalog and verify its coach key before showing gameplay. The DVD wait check prevents a callback that finished early from leaving the streaming thread asleep. [LyN Wii audio format](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ubi_lyn.c)

The original Just Dance probe uses the game’s Quick Play, Regular, one-round and Full version actions. It verifies the movie path before showing gameplay. All 32 native selections opened the matching coach video in loopback tests. The preview builder reads the stereo music streams from the separate sound banks and reuses existing coach clips. One complete run reached its original recap after 226.6 seconds of gameplay and returned to the DARE tile on Enter. Escape during Acceptable in the 80s returned to its tile. Traces and captures are under `probe/legacy-editions/SDNE41/` in the local cache. [Ubisoft sound bank format](https://github.com/vgmstream/vgmstream/blob/r2168/src/meta/ubi_bao.c)

The Just Dance 2 and Summer Party builds check for a completed DVD request before suspending the streaming thread. They also preserve the packed vertex coordinates that the games use to render their coach videos and backgrounds. Both workers move the chosen song into the first original carousel slot in memory and verify its key before revealing gameplay. Loopback tests reached scoring on Cosmic Girl and Pon De Replay, and Escape restored their selected tiles. Traces and captures are under `probe/legacy-editions/`.

Just Dance 4 reorders the original album records in memory and confirms the selected choreography scene before showing gameplay. The preview builder sends the original Wii MobiClip video packets to FFmpeg through its MODS demuxer. [Wii MobiClip container](https://github.com/quatric/mobipeg/blob/master/libavformat/modec.c) · [FFmpeg MODS demuxer](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavformat/mods.c)

The expanded launcher uses the same original Classic song lookup and lobby flow in the four later UbiArt editions. The newer catalogs are generated at build time from locally available Classic song descriptors and audio. The one-time preview cache contains coach clips for all 177 songs. In a warmed 2019 A/B test, skipping the hidden grip popup reduced **Milosc W Zakopanem** from 6.1 to 3.9 seconds between selection and fullscreen. The same cold-worker test went from 11.3 to 9.3 seconds. Software tests with the shortcut verified fullscreen scoring and HOME return in all four editions. Dismissing the original 2019 **Un Poco Loco** recap also returned to its selected tile. Timings and test logs are under `probe/launch-timing/`. A full browser test navigated across all six editions, launched **Familiar**, and confirmed return to the selected 2019 tile after the recap. Logs are under `probe/collection/`.

JD2014 initially stalled while opening the Wii microphone service. An empty IOS 57–59 USB vendor manager passes stock initialization. The native game also needs FIFO breakpoint mode. A later memory watchpoint found the graphics write pipe incorrectly wrapping a redirected vertex buffer to address zero. The fix preserves the exact-end FIFO wrap and lets redirected buffers advance beyond the old FIFO limit. [USB v5 reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/IOS/USB/USBV5.cpp) · [Wii gather pipe reference](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/GPFifo.cpp)

The build checks the USA region, outer disc ID and revision, and boot DOL SHA-256. JD3's inner boot header has revision 0 and the outer disc reports revision 1. JD2014's `BlockFlows_WII.ipk` fails nodtool's optional H0 validation. Nonvalidating extraction completed, and the boot DOL matched its full SHA-256.

## Collection coverage

All 17 backups produced video under the planning reference run and were compiled locally. Native startup traces for the older titles are in `probe/collection/legacy-smoke/`. Each disc is USA revision 0 unless marked otherwise. Routine counts in this table describe source entries before duplicate filtering. The local inventory at `probe/catalog/ipk-descriptors.json` counts distinct paths and template keys, including variants and entries whose playability is unknown. `probe/catalog/dol-identities.log` records all 17 exact boot hashes and the verified JD4 ELF segment matches. Each of the 2017–2019 symbol maps contains the original song selection, lobby launch, and move-scoring methods.

| Disc | ID | Routine metadata | Native milestone | Selected dance | Physical pass |
| --- | --- | --- | --- | --- | --- |
| Just Dance | SDNE41 | 32 original song IDs with coach and music previews | Direct launch, scoring and original recap with loopback. Some software runs reached the recap early. | DARE | Pending |
| Just Dance 2 | SD2Y41 | 47 local songs with coach and music previews | Direct launch, scoring, original recap, and picker cancellation verified | Cosmic Girl, Rasputin, Jai Ho, Soul Bossa Nova | Pending |
| Just Dance 3 | SJDE41 r1 | 47 local Classic routines with coach and music previews | Native direct launch, original recap, and picker return verified | Pump It, Take On Me | Pending |
| Just Dance 4 | SJXE41 | 44 local Classic routines with coach and music previews | Native direct launch, motion feedback, and picker cancellation verified | Disturbia, We No Speak Americano | Pending |
| Just Dance 2014 | SJOE41 | 80 descriptor paths. All 48 picker songs resolve in the original Classic database. | 44,819 main functions. Original solo scoring and Get Lucky recap verified in strict-native mode. | Just Dance, Get Lucky, and #thatPOWER | Just Dance scoring |
| Just Dance 2015 | SE3E41 | 63 descriptor paths. All 42 picker songs resolve in the original Classic database. | 50,697 native functions. Direct launch, scoring, and Happy recap verified. | Happy and 4x4 | Pending |
| Just Dance 2016 | SJNE41 | 43 local Classic source songs | Native direct launch and scoring | No Control | Pending |
| Just Dance 2017 | SZ7E41 | 40 local Classic source songs | Native direct launch and scoring | Cheap Thrills | Pending |
| Just Dance 2018 | SE8E41 | 46 local Classic source songs | Native direct launch and scoring | 24K Magic | Pending |
| Just Dance 2019 | S5EE41 | 48 local Classic source songs | Native direct launch, scoring, and original recap | Familiar | Pending |
| Just Dance Disney Party | SJ6E41 | Pending | Native translation built | Pending | Pending |
| Just Dance Disney Party 2 | S5DE41 | Pending | Native translation built | Pending | Pending |
| Just Dance Greatest Hits | SJHE41 | 37 local songs with coach and music previews | Direct launch and picker cancellation verified | Rasputin, Jai Ho | Pending |
| Just Dance Kids | SDZE41 | Pending | Native translation built | Pending | Pending |
| Just Dance Kids 2 | SJZE41 | Pending | Native translation built | Pending | Pending |
| Just Dance Kids 2014 | SJ7E41 | Pending | Native translation built | Pending | Pending |
| Just Dance Summer Party | SJ9E41 | 23 local songs with coach and music previews | Direct launch, scoring, and picker cancellation verified | Pon De Replay, Firework | Pending |

The Kids and Disney Party discs use `data_tbl/game_data.arc` and numbered stages. Their song selection and media formats differ from the 2014–2019 UbiArt adapter.
