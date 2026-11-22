#!/usr/bin/env python3
"""Build and run Just Dance Couch and its strict-native boot probes."""

import argparse
from fcntl import LOCK_EX, flock
import hashlib
import os
import shlex
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.request import urlretrieve

from probe import gh, jd1, jd2, jd3, jd4
from probe.collection import EDITIONS, catalog, write_catalog
from probe.instrument import (
    fix_jd2_bigfile, instrument, instrument_jd3,
)
from probe.previews import prepare_previews


ROOT = Path(__file__).resolve().parent
REVISION = "55fb408eb1f2df75e914524dc092e69c70739ded"
ARCHIVE_SHA256 = "12ef40254eb5460767a936be7d35b6b5f831f39b64401e7428c8a8a05b9bb7b2"
BTSTACK_VERSION = "1.8.3"
BTSTACK_SHA256 = "aa164c64ce3bacc73e214562beb6b2b1e675ccb8f5d6d6764983aa58ddff311a"
DISCS = {
    "SD2Y41": ("Just Dance 2.rvz", 0, "ac002beb4985e7ba7f75708b558e0a78480def4a2b4e2e0152c5d3340de57520"),
    "SJOE41": ("Just Dance 2014.rvz", 0, "4d60b98e73de8052c20d52df026a99994186aa0c618072423e3f1007a42e8d3d"),
    "SE3E41": ("Just Dance 2015.rvz", 0, "3780fa7d05f45cf7a90d95ac7368a9c4276168d8c339a3418d8ff06e8c60dfae"),
    "SJNE41": ("Just Dance 2016.rvz", 0, "63d052ce99b41944e397ee518e3b33e8e16965b320e3d801a8e7a0022cdab264"),
    "SZ7E41": ("Just Dance 2017.rvz", 0, "fa7e5cda4da2a1ddafcc71f32bfaa1a4acca311dcafde939b247b49bb9896d16"),
    "SE8E41": ("Just Dance 2018.rvz", 0, "fefc8746c08f91f3e19a7fefca4e482b69e11bd88f06b1bdacfa7a209ee5d312"),
    "S5EE41": ("Just Dance 2019.rvz", 0, "0c30ff85d33c38fa933bdd3ff1af7de01d97b43ded7a36610cadb2c8e009ec84"),
    "SJDE41": ("Just Dance 3.rvz", 1, "18c218dd67a4c1d8f05ad40a380bc1822c3ab4a4ff7d04a24d043f933997acf7"),
    "SJXE41": ("Just Dance 4.rvz", 0, "2b55c4e869e15d89f705fcc76912ea800f73167fbe49437c0e9e1f9756ffc0da"),
    "S5DE41": ("Just Dance Disney Party 2.rvz", 0, "6ec3d07053bd2951e4f77dd27e46de366e45b3c9d5204b4dd7eba347fc3de162"),
    "SJ6E41": ("Just Dance Disney Party.rvz", 0, "5c67b9d8a0fa7b61225861b8bf823ae43b5cb10900166579b7d879eb8d1ff0b1"),
    "SJHE41": ("Just Dance Greatest Hits.rvz", 0, "d74f6d36712807084c8b71033850bde4a7778ef8662722deec5309a0050fbd87"),
    "SJZE41": ("Just Dance Kids 2.rvz", 0, "35f046d2f911fd8512c9099e3204caeaee3ef628d40ff5418405cb31b50f0411"),
    "SJ7E41": ("Just Dance Kids 2014.rvz", 0, "d7a4e53c8ba8a40c911704377d5399ab59b907f459b0fdece0a45c574b69fa75"),
    "SDZE41": ("Just Dance Kids.rvz", 0, "456e5cfe987b212a3215cc6abe90b6982c841839da2272155efdc73ee5fb9619"),
    "SJ9E41": ("Just Dance Summer Party.rvz", 0, "e6728082fd351ac90503cfec6684b4515ef0bf5ae9a766060de59fe2d59b0ee0"),
    "SDNE41": ("Just Dance.rvz", 0, "6938c96e3511568eec2c2abaaa26b64cbd47a852b51dbf20929da053a2c8beb1"),
}
EMBEDDED_TITLES = {
    "SJOE41": "jd2014_title", "SE3E41": "jd2015_title",
    **{disc: f"title_{disc}" for disc in EDITIONS},
    "SDNE41": "jd1_title",
    "SD2Y41": "jd2_title", "SJ9E41": "summer_title", "SJHE41": "gh_title",
    "SJDE41": "jd3_title", "SJXE41": "jd4_title",
}
BACKUPS = Path("/mnt/synology/Games/wii")


def app_directory(variable, fallback):
    configured = Path(os.environ.get(variable, ""))
    base = configured if configured.is_absolute() else Path.home() / fallback
    return base / "just-dance-couch"


CACHE = app_directory("XDG_CACHE_HOME", ".cache")
STATE = app_directory("XDG_DATA_HOME", ".local/share")
UPSTREAM = CACHE / "wii-recomp"
RUNTIME = UPSTREAM / "recomp"


def checksum(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def verify_disc(disc, disc_id):
    _, revision, dol_checksum = DISCS[disc_id]
    with (disc / "disc/header.bin").open("rb") as raw_header:
        header = raw_header.read(8)
    with (disc / "sys/boot.bin").open("rb") as boot:
        partition_id = boot.read(6)
    with (disc / "disc/region.bin").open("rb") as region:
        region_code = region.read(4)
    expected_id = disc_id.encode("ascii")
    if (header != expected_id + bytes((0, revision)) or partition_id != expected_id
            or region_code != b"\x00\x00\x00\x01"):
        raise ValueError(f"{disc} must be {disc_id} revision {revision}, USA")
    if checksum(disc / "sys/main.dol") != dol_checksum:
        raise ValueError(f"{disc} has a different boot executable")


def build_environment():
    env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}
    local = CACHE / "deps/usr"
    libraries = local / "lib/x86_64-linux-gnu"
    if (local / "include/SDL3/SDL.h").is_file():
        env["SDL3_PREFIX"] = str(local)
        for key, folder in (("CPATH", local / "include"), ("LIBRARY_PATH", libraries),
                            ("LD_LIBRARY_PATH", libraries)):
            env[key] = os.pathsep.join(filter(None, (str(folder), env.get(key))))
    if (CACHE / "bin").is_dir():
        env["PATH"] = os.pathsep.join((str(CACHE / "bin"), env["PATH"]))
    return env


def prepare_archive(destination, archive_name, url, sha256, directory):
    if destination.exists():
        return
    CACHE.mkdir(parents=True, exist_ok=True)
    archive = CACHE / archive_name
    with tempfile.TemporaryDirectory(dir=CACHE) as temp:
        staging = Path(temp)
        if not archive.is_file():
            downloaded = staging / "upstream.tar.gz"
            urlretrieve(url, downloaded)
            if checksum(downloaded) != sha256:
                raise ValueError(f"SHA-256 mismatch for {archive_name}")
            downloaded.rename(archive)
        if checksum(archive) != sha256:
            raise ValueError(f"SHA-256 mismatch for {archive}")
        with tarfile.open(archive) as source:
            source.extractall(staging, filter="data")
        (staging / directory).rename(destination)


def apply_patch(folder, patch, *, revert=False):
    command = ["patch", "-p1", "-i", str(patch)]
    applied = subprocess.run(
        [*command, "--force", "--reverse", "--dry-run"], cwd=folder,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    if revert and not applied.returncode:
        subprocess.run([*command, "--force", "--reverse"], cwd=folder, check=True)
    elif not revert and applied.returncode:
        subprocess.run([*command, "--batch", "--forward"], cwd=folder, check=True)


def prepare_upstream():
    prepare_archive(UPSTREAM, "wii-recomp-55fb408.tar.gz",
                    f"https://codeload.github.com/Zontex/wii-recomp/tar.gz/{REVISION}",
                    ARCHIVE_SHA256, f"wii-recomp-{REVISION}")
    # Upgrade runtime source caches that already contain the old audio correction.
    apply_patch(UPSTREAM, ROOT / "patches/obsolete/audio-drift-crossfade.patch", revert=True)
    for patch in sorted((ROOT / "patches").glob("*.patch")):
        apply_patch(UPSTREAM, patch)


def build_btstack(jobs, env, clang):
    name = f"btstack-{BTSTACK_VERSION}"
    source = CACHE / name
    prepare_archive(source, f"{name}.tar.gz",
                    f"https://codeload.github.com/bluekitchen/btstack/tar.gz/refs/tags/v{BTSTACK_VERSION}",
                    BTSTACK_SHA256, name)
    apply_patch(source, ROOT / "deps/btstack-usb.patch")
    build_dir = CACHE / f"{name}-build"
    subprocess.run(["cmake", "-S", str(ROOT / "deps"), "-B", str(build_dir), "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_C_COMPILER={shutil.which(clang, path=env['PATH'])}",
                    f"-DBTSTACK_SOURCE={source}"], env=env, check=True)
    subprocess.run(["cmake", "--build", str(build_dir), "-j", str(jobs)], env=env, check=True)
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "libusb-1.0"],
                                               env=env, text=True))
    includes = [f"-I{ROOT / 'deps'}", f"-I{source / 'src'}", f"-I{source / 'platform/posix'}"]
    return includes, [str(build_dir / "libcouch-btstack.a"), *flags]


def prepare_disc(disc, disc_id, image, env):
    if disc.exists():
        verify_disc(disc, disc_id)
        return
    nodtool = shutil.which("nodtool", path=env["PATH"])
    if not nodtool:
        sys.path.insert(0, str(RUNTIME / "tools"))
        from extract_disc import find_nodtool
        nodtool = find_nodtool()
    disc.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=disc.parent) as temp:
        extracted = Path(temp) / "disc"
        # This RVZ fails nodtool's optional H0 validation. Check the exact boot identity.
        subprocess.run([nodtool, "extract", "--quiet", str(image), str(extracted)], check=True)
        verify_disc(extracted, disc_id)
        extracted.rename(disc)


def prepare_probe_adapter():
    folder = ROOT / "probe"
    adapter = RUNTIME / "runtime/jd2014_probe.c"
    selection = (folder / "jd2.inc").read_text()
    for placeholder, songs in (("JD2_SONG_SLOTS", jd2.JD2_SONGS), ("SUMMER_SONG_SLOTS", jd2.SUMMER_SONGS)):
        slots = "\n".join(f"  {{0x{key}u, {offset}, {slot}u, 0}}," for key, _, _, _, slot, offset in songs)
        selection = selection.replace(placeholder, slots)
    for placeholder, songs in (("GH_SONG_SLOTS", gh.SONGS), ("JD3_SONG_SLOTS", jd3.SONGS),
                               ("JD4_SONG_SLOTS", jd4.SONGS)):
        slots = "\n".join(f"  {{0x{key}u, {offset}, {slot}u, 0x{asset}u}},"
                          for key, _, _, _, slot, offset, asset, *_ in songs)
        selection = selection.replace(placeholder, slots)
    content = (folder / "jd2014.c").read_text() + "\n" + selection + "\n" + (folder / "lyn_classic.inc").read_text()
    if not adapter.exists() or adapter.read_text() != content:
        adapter.write_text(content)


def build(disc_id, image, jobs):
    prepare_upstream()
    env = build_environment()
    disc = CACHE / "discs" / disc_id
    prepare_disc(disc, disc_id, image, env)
    if disc_id in EMBEDDED_TITLES:
        prepare_probe_adapter()
    subprocess.run(
        [sys.executable, str(RUNTIME / "tools/add_game.py"), str(disc),
         "--no-pack", "--jobs", str(jobs), "--fifo-breakpoints"],
        cwd=UPSTREAM, env=env, check=True,
    )
    if disc_id in EMBEDDED_TITLES:
        title_build = RUNTIME / f"build-{disc_id}"
        generated = title_build / "generated"
        if disc_id == "SJDE41":
            instrument_jd3(generated)
            jd3.prepare_previews(disc, CACHE / "previews" / disc_id, jobs)
        else:
            instrument(generated, disc_id)
        if disc_id == "SD2Y41":
            if (disc / "files/WD2.bbf").is_file() and not (disc / "files/WD2.bf").is_file():
                fix_jd2_bigfile(generated)
        subprocess.run(["ninja", "-j", str(jobs), "title.so"], cwd=title_build, env=env, check=True)


def boot(disc_id, fresh, song):
    disc = CACHE / "discs" / disc_id
    verify_disc(disc, disc_id)
    env = build_environment()
    binary = RUNTIME / "build/wii-recomp"
    library = RUNTIME / f"build-{disc_id}/title.so"
    if not binary.is_file() or not library.is_file():
        raise ValueError("Run python3 pilot.py build first")
    STATE.mkdir(parents=True, exist_ok=True)
    if fresh:
        save_dir = Path(tempfile.mkdtemp(prefix="probe-", dir=STATE))
    else:
        save_dir = STATE / disc_id
    save_dir.mkdir(exist_ok=True)
    env.update(
        WII_RECOMP_SAVES=str(save_dir),
        WII_RECOMP_NATIVE=str(library),
        WII_RECOMP_NATIVE_ONLY="1",
    )
    env.setdefault("WII_RECOMP_REMOTES", "loopback")
    env.setdefault("WII_RECOMP_ATTACHMENT", "none")
    env.setdefault("WII_RECOMP_SHOP_SERVER", "off")
    env.setdefault("WII_RECOMP_REMOTE_SLOTS", str(save_dir / "remotes.txt"))
    env.setdefault("WII_RECOMP_CONTROLLERS", str(save_dir / "controllers.ini"))
    if song:
        if disc_id == "SJOE41" and song in ("GetLucky", "JustDance"):
            env["JD2014_PROBE_MAP"] = song
        elif disc_id == "SDNE41" and any(key == song for key, _, _, _ in jd1.SONGS):
            env["JD1_PROBE_MAP"] = song
        elif disc_id in jd2.EDITIONS and any(key == song for key, *_ in jd2.EDITIONS[disc_id][1]):
            env["JD2_PROBE_MAP"] = song
        elif disc_id == "SJHE41" and any(key == song for key, *_ in gh.SONGS):
            env["JD2_PROBE_MAP"] = song
        elif disc_id == "SJDE41" and any(key == song for key, *_ in jd3.SONGS):
            env["JD2_PROBE_MAP"] = song
        elif disc_id == "SJXE41" and any(key == song for key, *_ in jd4.SONGS):
            env["JD2_PROBE_MAP"] = song
        else:
            raise ValueError("Unknown song key for this native probe")
    os.chdir(STATE)
    os.execve(binary, [str(binary), str(disc / "sys")], env)


def link_command(folder, target, env):
    lines = subprocess.check_output(
        ["ninja", "-t", "commands", target], cwd=folder, env=env, text=True,
    ).splitlines()
    command = shlex.split(lines[-1])
    output = command.index("-o")
    return command[0], command[1:output], command[output + 2:]


def build_couch(jobs):
    prepare_upstream()
    env = build_environment()
    for disc_id in EMBEDDED_TITLES:
        if not (RUNTIME / f"build-{disc_id}/title.so").is_file():
            raise ValueError(f"Run python3 pilot.py build --disc {disc_id} first")
    prepare_probe_adapter()

    collection = {}
    for disc_id in EMBEDDED_TITLES:
        disc = CACHE / "discs" / disc_id
        verify_disc(disc, disc_id)
        title_build = RUNTIME / f"build-{disc_id}"
        if disc_id == "SJDE41":
            collection[disc_id] = "Just Dance 3", jd3.catalog()
            jd3.prepare_previews(disc, CACHE / "previews" / disc_id, jobs)
            instrument_jd3(title_build / "generated")
        elif disc_id == "SJXE41":
            collection[disc_id] = "Just Dance 4", jd4.catalog()
            jd4.prepare_previews(disc, CACHE / "previews" / disc_id, jobs)
            instrument(title_build / "generated", disc_id)
        elif disc_id == "SDNE41":
            collection[disc_id] = "Just Dance", jd1.catalog()
            jd1.prepare_previews(disc, CACHE / "previews" / disc_id)
            instrument(title_build / "generated", disc_id)
        elif disc_id == "SJHE41":
            collection[disc_id] = "Just Dance Greatest Hits", gh.catalog()
            gh.prepare_previews(disc, CACHE / "previews" / disc_id, jobs)
            instrument(title_build / "generated", disc_id)
        elif disc_id in jd2.EDITIONS:
            collection[disc_id] = jd2.catalog(disc_id)
            jd2.prepare_previews(disc_id, disc, CACHE / "previews" / disc_id, jobs)
            instrument(title_build / "generated", disc_id)
        else:
            if disc_id in EDITIONS:
                songs = catalog(disc)
                collection[disc_id] = EDITIONS[disc_id], songs
                if not songs:
                    raise ValueError(f"No local Classic routines for {disc_id}")
                keys = {key.lower() for _, _, key in songs}
            else:
                keys = None
            prepare_previews(disc, CACHE / "previews" / disc_id, jobs, keys)
            instrument(title_build / "generated", disc_id)
        subprocess.run(["ninja", "-j", str(jobs), "title.so"], cwd=title_build, env=env, check=True)

    console = RUNTIME / "build"
    subprocess.run(["ninja", "-j", str(jobs), "wii-recomp"], cwd=console, env=env, check=True)
    clang, runtime_objects, link_flags = link_command(console, "wii-recomp", env)
    bluetooth_includes, bluetooth_flags = build_btstack(jobs, env, clang)
    font_flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "fontconfig", "freetype2"], env=env, text=True,
    ))

    output_folder = CACHE / "bin"
    output_folder.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=CACHE) as temp:
        staging = Path(temp)
        write_catalog(staging, collection)
        registry = staging / "collection_titles.c"
        declarations = "\n".join(f"extern const Title {symbol};" for symbol in EMBEDDED_TITLES.values())
        lookups = "\n".join(f'  if (!strcmp(path, "builtin:{disc}")) return &{symbol};'
                             for disc, symbol in EMBEDDED_TITLES.items())
        registry.write_text('#include <string.h>\n#include "title.h"\n' + declarations
                            + "\nconst Title *jd_builtin_title(const char *path) {\n"
                            + lookups + "\n  return NULL;\n}\n")
        title_objects = []
        for disc_id, symbol in EMBEDDED_TITLES.items():
            folder = RUNTIME / f"build-{disc_id}"
            _, original, _ = link_command(folder, "title.so", env)
            combined = staging / f"{disc_id}.o"
            subprocess.run(["ld", "-r", "-o", str(combined), *original], cwd=folder, env=env, check=True)
            subprocess.run(
                ["objcopy", "--localize-hidden", "--redefine-sym", f"wii_recomp_title={symbol}", str(combined)],
                env=env, check=True,
            )
            title_objects.append(str(combined))

        main_object = staging / "runtime_main.o"
        original_main = subprocess.check_output(
            ["ninja", "-t", "commands", "obj/runtime_main.o"], cwd=console, env=env, text=True,
        ).splitlines()[-1]
        compile_main = shlex.split(original_main)
        dependency_flags = compile_main.index("-MMD")
        subprocess.run(
            [*compile_main[:dependency_flags], "-Dmain=jd_runtime_main", "-c",
             "../runtime/main.c", "-o", str(main_object)],
            cwd=console, env=env, check=True,
        )
        runtime_objects.remove("obj/runtime_main.o")
        result = staging / "just-dance-couch"
        includes = ["-I../runtime", f"-I{ROOT / 'app'}", f"-I{staging}"]
        if "SDL3_PREFIX" in env:
            includes.append(f"-I{env['SDL3_PREFIX']}/include")
        subprocess.run(
            [clang, "-O2", *includes, *bluetooth_includes, str(ROOT / "app/couch.c"),
             str(ROOT / "app/worker.c"), str(ROOT / "app/usb_remote.c"), str(ROOT / "app/song_grid.c"),
             str(ROOT / "app/catalog.c"), str(registry),
             str(main_object), *runtime_objects, *title_objects, "-o", str(result),
             "-Wl,-z,noexecstack", *bluetooth_flags, *font_flags, *link_flags],
            cwd=console, env=env, check=True,
        )
        destination = output_folder / "just-dance-couch"
        result.replace(destination)
        print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    compiler = commands.add_parser("build", help="extract the owned disc and build locally")
    compiler.add_argument("--image", type=Path)
    compiler.add_argument("--jobs", type=int, default=6)
    couch = commands.add_parser("couch", help="link the built titles and the browser into one executable")
    couch.add_argument("--jobs", type=int, default=6)
    runner = commands.add_parser("boot", help="run a title in strict-native mode")
    runner.add_argument("--song", help="probe a verified game and song key")
    runner.add_argument("--fresh", action="store_true", help="use a new isolated save directory")
    for command in (compiler, runner):
        command.add_argument("--disc", choices=DISCS, default="SJOE41", help="disc ID (default SJOE41)")
    args = parser.parse_args()
    try:
        if args.command == "boot":
            boot(args.disc, args.fresh, args.song)
        else:
            CACHE.mkdir(parents=True, exist_ok=True)
            with (CACHE / "build.lock").open("a") as lock:
                flock(lock, LOCK_EX)
                if args.command == "couch":
                    build_couch(args.jobs)
                else:
                    image = args.image or BACKUPS / DISCS[args.disc][0]
                    build(args.disc, image.expanduser().resolve(strict=True), args.jobs)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"pilot: {error}\n")


if __name__ == "__main__":
    main()
