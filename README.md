# Just Dance Couch

Just Dance Couch is a native Linux launcher for your own Just Dance Wii backups. Browse an alphabetical fullscreen grid with music and coach video previews, then launch straight into the original dance and scoring. The launcher compiles each game locally and warms the selected edition while you browse.

## Build from source

Use Linux on an x86-64-v3 CPU with a working Vulkan driver. Install Python 3.12 or newer, Clang (`clang` on your `PATH`), CMake, Ninja, `pkg-config`, FFmpeg, GNU binutils, and `patch`. Install the development packages for GLFW, SDL3, Vulkan, shaderc, ALSA, Fontconfig, FreeType, and libusb-1.0. If SDL3 is installed in a custom location, export `SDL3_PREFIX` with its installation path before building.

The launcher build requires the **12 USA Wii disc backups** listed below. Use **revision 1** for Just Dance 3 and **revision 0** for the other editions. The build checks each disc's region, revision, and executable. It downloads pinned dependencies as needed and stores the extracted discs, compiled code, and previews locally.

From the repository root, set `games_dir` and the filenames to match your backups. For optional Wii DLC, put your add-on WADs in the `DLC` subdirectory and install Python's `cryptography` package. An empty or absent DLC directory produces a disc-only picker.

```bash
(
  set -e
  games_dir="$HOME/Wii/Just Dance"
  while IFS='|' read -r disc image; do
    python3 -B pilot.py build --disc "$disc" --image "$games_dir/$image"
  done <<'DISCS'
SDNE41|Just Dance.rvz
SD2Y41|Just Dance 2.rvz
SJ9E41|Just Dance Summer Party.rvz
SJDE41|Just Dance 3.rvz
SJHE41|Just Dance Greatest Hits.rvz
SJXE41|Just Dance 4.rvz
SJOE41|Just Dance 2014.rvz
SE3E41|Just Dance 2015.rvz
SJNE41|Just Dance 2016.rvz
SZ7E41|Just Dance 2017.rvz
SE8E41|Just Dance 2018.rvz
S5EE41|Just Dance 2019.rvz
DISCS
  python3 -B pilot.py couch --dlc-dir "$games_dir/DLC"
)
```

## Play

Connect a Nintendo Wii Bluetooth module (`057e:0305`) and make sure your user can read and write to its USB device. Close Dolphin if it has the module open.

```bash
couch="${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/bin/just-dance-couch"
"$couch" --usb-wii
```

Connect up to four Wii Remotes in the picker. For first-time pairing, press the red **SYNC** button inside each remote's battery compartment. Wait for its numbered light to go solid before waking or syncing the next one. On later launches, wake the paired remotes with a button press.

Players get the lowest free number as they connect. Other players keep their numbers when a remote disconnects. The lowest-numbered connected remote controls the picker and can return to it during a dance. If Player 1 disconnects, wake or reconnect a remote to fill that slot before starting another song. Press **Escape** in the picker to exit and release the module.

Use `"$couch" --loopback` to check the picker and launch flow with a keyboard. Running `"$couch"` without flags uses a BlueZ-paired Wii Remote through a hidraw device with read and write permissions and a driver that leaves HID reporting to userspace.

| Action | Wii Remote | Keyboard in loopback |
| --- | --- | --- |
| Browse songs | D-pad | Arrow keys |
| Start a dance or dismiss the results | A | Enter |
| Return to the picker during a dance | HOME | Escape |

From the first song row, press **Up** to focus the edition name. Use **Left** and **Right** to switch editions, then **Down** to return to the songs. Hold a direction to keep moving.

## Song library and DLC

Each disc page shows that edition’s full supported Classic catalog. The **DLC** page follows **Just Dance 2019** when add-ons are available. The build imports USA Wii add-ons for **Just Dance 2–4**, **Just Dance 2014**, and **Just Dance 2015**.

When a song appears in several DLC editions, the DLC page shows the newest available add-on copy. Within that edition, the standard DLC routine takes priority.

## Audio levels

Per-song loudness uses Just Dance 4 as the reference, around −10 LUFS for gameplay and −12.5 LUFS for picker previews. Gameplay has peak protection. The build measures the cached previews with FFmpeg and reuses those measurements until the files change.

## Local files and logs

- `${XDG_CACHE_HOME:-$HOME/.cache}/just-dance-couch/` holds the extracted discs, builds, previews, and `game-<launcher-pid>.log` files.
- `${XDG_DATA_HOME:-$HOME/.local/share}/just-dance-couch/` holds saves and USB Wii Remote pairing data.

Set `JUST_DANCE_GAME_TRACE=1` when launching to print the game trace in the terminal.

## Credits

Just Dance Couch is an independent fan project. Wii Recomp provides the native game runtime, and BTstack handles direct USB Bluetooth. [Wii Recomp](https://github.com/Zontex/wii-recomp) · [BTstack 1.8.3 license](https://github.com/bluekitchen/btstack/blob/v1.8.3/LICENSE)
