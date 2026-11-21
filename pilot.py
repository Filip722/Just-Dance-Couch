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

from probe.instrument import instrument


ROOT = Path(__file__).resolve().parent
REVISION = "55fb408eb1f2df75e914524dc092e69c70739ded"
ARCHIVE_SHA256 = "12ef40254eb5460767a936be7d35b6b5f831f39b64401e7428c8a8a05b9bb7b2"
PILOTS = {
    "SJOE41": ("Just Dance 2014.rvz", 0, "4d60b98e73de8052c20d52df026a99994186aa0c618072423e3f1007a42e8d3d"),
    "SJDE41": ("Just Dance 3.rvz", 1, "18c218dd67a4c1d8f05ad40a380bc1822c3ab4a4ff7d04a24d043f933997acf7"),
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
    _, revision, dol_checksum = PILOTS[disc_id]
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


def prepare_upstream():
    if not UPSTREAM.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        archive = CACHE / "wii-recomp-55fb408.tar.gz"
        with tempfile.TemporaryDirectory(dir=CACHE) as temp:
            staging = Path(temp)
            if not archive.is_file():
                downloaded = staging / "upstream.tar.gz"
                urlretrieve(f"https://codeload.github.com/Zontex/wii-recomp/tar.gz/{REVISION}", downloaded)
                if checksum(downloaded) != ARCHIVE_SHA256:
                    raise ValueError("The Wii Recomp archive failed its SHA-256 check")
                downloaded.rename(archive)
            if checksum(archive) != ARCHIVE_SHA256:
                raise ValueError(f"The Wii Recomp archive failed its SHA-256 check: {archive}")
            with tarfile.open(archive) as source:
                source.extractall(staging, filter="data")
            (staging / f"wii-recomp-{REVISION}").rename(UPSTREAM)
    for patch in sorted((ROOT / "patches").glob("*.patch")):
        command = ["patch", "-p1", "-i", str(patch)]
        applied = subprocess.run(
            [*command, "--force", "--reverse", "--dry-run"], cwd=UPSTREAM,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        if applied.returncode:
            subprocess.run([*command, "--batch", "--forward"], cwd=UPSTREAM, check=True)


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


def build(disc_id, image, jobs):
    prepare_upstream()
    env = build_environment()
    disc = CACHE / "discs" / disc_id
    prepare_disc(disc, disc_id, image, env)
    if disc_id == "SJOE41":
        adapter = RUNTIME / "runtime/jd2014_probe.c"
        adapter_source = (ROOT / "probe/jd2014.c").read_bytes()
        if not adapter.exists() or adapter.read_bytes() != adapter_source:
            adapter.write_bytes(adapter_source)
    subprocess.run(
        [sys.executable, str(RUNTIME / "tools/add_game.py"), str(disc),
         "--no-pack", "--jobs", str(jobs), "--fifo-breakpoints"],
        cwd=UPSTREAM, env=env, check=True,
    )
    if disc_id == "SJOE41":
        title_build = RUNTIME / f"build-{disc_id}"
        instrument(title_build / "generated")
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
        if disc_id != "SJOE41":
            raise ValueError("--song is available for the JD2014 probe")
        env["JD2014_PROBE_MAP"] = song
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
    adapter = RUNTIME / "runtime/jd2014_probe.c"
    for disc_id in PILOTS:
        if not (RUNTIME / f"build-{disc_id}/title.so").is_file():
            raise ValueError(f"Run python3 pilot.py build --disc {disc_id} first")
    adapter_source = (ROOT / "probe/jd2014.c").read_bytes()
    if not adapter.exists() or adapter.read_bytes() != adapter_source:
        adapter.write_bytes(adapter_source)
    console = RUNTIME / "build"
    subprocess.run(["ninja", "-j", str(jobs), "wii-recomp"], cwd=console, env=env, check=True)
    clang, runtime_objects, link_flags = link_command(console, "wii-recomp", env)

    output_folder = CACHE / "bin"
    output_folder.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=CACHE) as temp:
        staging = Path(temp)
        title_objects = []
        for disc_id, symbol in (("SJOE41", "jd2014_title"), ("SJDE41", "jd3_title")):
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
        includes = ["-I../runtime", f"-I{ROOT / 'app'}"]
        if "SDL3_PREFIX" in env:
            includes.append(f"-I{env['SDL3_PREFIX']}/include")
        subprocess.run(
            [clang, "-O2", *includes, str(ROOT / "app/couch.c"), str(ROOT / "app/worker.c"),
             str(main_object), *runtime_objects, *title_objects, "-o", str(result), *link_flags],
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
    couch = commands.add_parser("couch", help="link both built titles and the browser into one executable")
    couch.add_argument("--jobs", type=int, default=6)
    runner = commands.add_parser("boot", help="run a title in strict-native mode")
    runner.add_argument("--song", choices=("GetLucky", "JustDance"), help="probe the direct-launch branch")
    runner.add_argument("--fresh", action="store_true", help="use a new isolated save directory")
    for command in (compiler, runner):
        command.add_argument("--disc", choices=PILOTS, default="SJOE41", help="disc ID (default SJOE41)")
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
                    image = args.image or BACKUPS / PILOTS[args.disc][0]
                    build(args.disc, image.expanduser().resolve(strict=True), args.jobs)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"pilot: {error}\n")


if __name__ == "__main__":
    main()
