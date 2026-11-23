"""Import owned Wii add-on WADs into read-only storage for the native games."""

from concurrent.futures import ProcessPoolExecutor
from hashlib import sha1, sha256
from itertools import repeat
from os.path import relpath
from pathlib import Path
import struct
import tempfile

from probe.coach_art import prepare_coach_art
from probe.collection import string_at
from probe.dlc_lyn import LYN_DLC
from probe.lyn_coach_art import prepare_lyn_coach_art
from probe.lyn_previews import entries as lyn_entries, raw_stream, write_lyn_clip
from probe.previews import has_preview_video, ipk_entries, read_entry, render_preview, song_assets


WII_ADD_ON_TITLES = {
    b"sD2E": "SD2Y41",
    b"sJDE": "SJDE41",
    b"sJXE": "SJXE41",
    b"sJOE": "SJOE41",
    b"sE3E": "SE3E41",
}
# Standard retail Wii common key. Per-title keys are read from local tickets.
WII_RETAIL_KEY = bytes.fromhex("ebe42a225e8593e448d9c5457381aaf7")


def align(size, block=64):
    return (size + block - 1) // block * block


def decrypt(data, key, iv):
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError as error:
        raise ValueError("Wii DLC import requires Python cryptography (python3-cryptography)") from error
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    return cipher.update(data) + cipher.finalize()


def import_wad(wad, cache, built_discs):
    with wad.open("rb") as source:
        file_bytes = wad.stat().st_size
        if file_bytes < 32:
            raise ValueError(f"Truncated Wii WAD header in {wad.name}")
        header, kind, cert, crl, ticket_bytes, tmd_bytes, data_bytes, _ = struct.unpack(">I4s6I", source.read(32))
        ticket_at = align(header) + align(cert) + align(crl)
        tmd_at = ticket_at + align(ticket_bytes)
        data_at = tmd_at + align(tmd_bytes)
        if (header != 32 or kind != b"Is\0\0" or ticket_bytes != 0x2a4 or
                not 0x1e4 <= tmd_bytes <= 0x1e4 + 36 * 512 or data_at + data_bytes > file_bytes):
            raise ValueError(f"Invalid Wii WAD header in {wad.name}")
        source.seek(ticket_at)
        ticket = source.read(ticket_bytes)
        source.seek(tmd_at)
        tmd = source.read(tmd_bytes)
        title = tmd[0x18c:0x194]
        disc = WII_ADD_ON_TITLES.get(title[4:]) if title[:4] == b"\0\1\0\5" else None
        if disc not in built_discs:
            return None
        count, = struct.unpack_from(">H", tmd, 0x1de)
        if (count * 36 + 0x1e4 != len(tmd) or title != ticket[0x1dc:0x1e4] or ticket[0x1f1] != 0 or
                tmd[:4] != b"\0\1\0\1" or ticket[:4] != b"\0\1\0\1"):
            raise ValueError(f"Invalid Wii add-on metadata in {wad.name}")
        records = list(struct.iter_unpack(">IHHQ20s", tmd[0x1e4:]))
        if (data_at + sum(align(size) for _, _, _, size, _ in records) > file_bytes or
                any(kind not in (1, 0x4001) for _, _, kind, _, _ in records) or
                len({index for _, index, _, _, _ in records}) != count or
                len({ident for ident, _, _, _, _ in records}) != count):
            raise ValueError(f"Invalid Wii add-on contents in {wad.name}")
        high, low = title[:4].hex(), title[4:].hex()
        pack = cache / (disc + "-" + sha256(ticket + tmd).hexdigest()[:24])
        relative = Path("nand/title") / high / low / "content"
        if not pack.is_dir():
            key = decrypt(ticket[0x1bf:0x1cf], WII_RETAIL_KEY, title + bytes(8))
            source.seek(data_at)
            with tempfile.TemporaryDirectory(dir=cache) as temp:
                staging = Path(temp) / "pack"
                staging.mkdir(mode=0o700)
                content = staging / relative
                content.mkdir(parents=True)
                for ident, index, _, size, digest in records:
                    encoded = source.read(align(size))
                    raw = decrypt(encoded[:align(size, 16)], key, index.to_bytes(2, "big") + bytes(14))[:size]
                    if len(raw) != size or sha1(raw).digest() != digest:
                        raise ValueError(f"Content {index} in {wad.name} failed its Wii hash check")
                    (content / f"{ident:08x}.app").write_bytes(raw)
                (content / "title.tmd").write_bytes(tmd)
                ticket_path = staging / "nand/ticket" / high / f"{low}.tik"
                ticket_path.parent.mkdir(parents=True)
                ticket_path.write_bytes(ticket)
                # Wii EC checks these Wii Shop files before enumerating local purchases.
                for name in ("ec/shopsetu.log", "succession/shop.log"):
                    path = staging / "nand/shared2" / name
                    path.parent.mkdir(parents=True)
                    path.write_bytes(bytes(32))
                staging.rename(pack)
            print(f"DLC: verified {len(records)} contents from {wad.name}", flush=True)
    return disc, pack, {index: pack / relative / f"{ident:08x}.app" for ident, index, _, _, _ in records}


def mount_song(disc, pack, contents, index):
    # Mount the selected content, the title metadata, and the LyN catalog if needed.
    # Mounting the entire collection at once can reset the Wii game's DLC manager.
    mounts = pack / "mounts"
    mounts.mkdir(exist_ok=True)
    mount = mounts / str(index)
    if mount.is_dir():
        return
    nand = pack / "nand"
    archive = contents[index]
    files = [nand / "ticket", nand / "shared2", archive, archive.parent / "title.tmd", contents[0]]
    if disc in LYN_DLC:
        files.append(contents[1])
    with tempfile.TemporaryDirectory(dir=mounts) as temp:
        staging = Path(temp)
        for target in files:
            link = staging / target.relative_to(nand)
            link.parent.mkdir(parents=True, exist_ok=True)
            link.symlink_to(relpath(target, link.parent), target_is_directory=target.is_dir())
        staging.rename(mount)


def u8_files(path):
    limit = path.stat().st_size
    with path.open("rb") as source:
        signature, start, length = struct.unpack(">3I", source.read(12))
        if signature != 0x55aa382d:
            return
        if start + length > limit or length < 12:
            raise ValueError(f"Invalid file table in Wii archive {path}")
        source.seek(start)
        data = source.read(length)
    count, = struct.unpack_from(">I", data, 8)
    if count * 12 > length:
        raise ValueError(f"Invalid file count in Wii archive {path}")
    for i in range(1, count):
        label, offset, size = struct.unpack_from(">3I", data, i * 12)
        if label >> 24:
            continue
        name = count * 12 + label
        if offset + size > limit:
            raise ValueError(f"Invalid member in Wii archive {path}")
        yield data[name:data.index(b"\0", name)].lower(), (path, offset, size)


def ubiart_song(disc, bundle):
    files = {name: entry for _, name, entry in ipk_entries(bundle)}
    data = read_entry(files[b"songdesc.tpl.ckd"])
    if struct.unpack_from(">I", data, 48)[0] != 0x8ac2b5c6:
        raise ValueError("Unexpected Wii DLC song descriptor")
    key, end = string_at(data, 56)
    parent = None
    if disc == "SJOE41":
        alternate, = struct.unpack_from(">I", data, end + 4)
        if alternate:
            parent, end = string_at(data, end + 8)
            end -= 8
        artist, end = string_at(data, end + 56)
    else:
        artist, end = string_at(data, end + 68)
        _, end = string_at(data, end)  # Original dancer name.
    title, _ = string_at(data, end)
    if parent:
        title += " (Sweat)" if "SWT" in key or "OSC" in key else " (Alternate)"
    return (title, artist, key), parent, files


def ubiart_catalog(disc, pack, contents, open_disc, jobs, coaches):
    previews = pack / "previews"
    previews.mkdir(exist_ok=True)
    songs, pending, base = [], [], None
    for index, app in sorted(contents.items()):
        if not index:
            continue
        bundle = next((entry for name, entry in u8_files(app) if name.endswith(b".ipk")), None)
        if bundle is None:
            continue
        song, parent, files = ubiart_song(disc, bundle)
        key = song[2].lower()
        if not key.isascii() or not key.replace("_", "").isalnum():
            raise ValueError(f"Invalid Wii DLC map key {key!r}")
        if coaches is not None:
            counts = prepare_coach_art(ipk_entries(bundle, compressed=True), previews, (key,))
            coaches.update(((pack.name, name), count) for name, count in counts.items())
        stem = key.encode("ascii")
        audio = files.get(stem + b".wav.ckd")
        track = files[stem + b"_musictrack.tpl.ckd"]
        video = files.get(stem + b".wii.webm")
        if not ((previews / f"{key}.wav").is_file() and has_preview_video(previews, key)):
            if not audio and parent:
                if base is None:
                    base = song_assets(open_disc(disc))
                audio = base.get(parent.lower().encode("ascii"), {}).get("audio")
            if audio:
                pending.append((key, audio, track, video))
        songs.append((*song, disc, pack.name, index))
    if pending:
        with ProcessPoolExecutor(max_workers=min(6, jobs)) as workers:
            list(workers.map(render_preview, pending, repeat(previews)))
    print(f"DLC: {len(songs)} routines from {disc}", flush=True)
    return songs


def build_lyn_preview(pack, job):
    key, stem, app = job
    archives = [entry for name, entry in u8_files(app) if b".bf" in name]
    streams = {}
    for path, offset, _ in archives:
        streams.update(lyn_entries(path, offset))
    stem = stem.lower().encode("ascii")
    # The Christmas routine keeps its DSP-ADPCM stream in its effect bank.
    if stem == b"xmas" and stem + b".wav" not in streams:
        samples = read_entry(streams[b"fdf03edd.bin"])
        size, = struct.unpack_from("<I", samples, 40)
        chunks = (b"WAVEfmt " + struct.pack("<IHHI", 8, 0x5050, 2, 32000) +
                  b"fact" + struct.pack("<II", 4, size // 16 * 14) + b"data" + struct.pack("<I", size))
        audio = b"RIFF" + struct.pack("<I", len(chunks) + size) + chunks + samples[44:44 + size]
    else:
        audio = raw_stream(streams[stem + b".wav"], streams)
    video = streams.get(stem + b"_wii.mo") or streams[stem + b".mo"]
    write_lyn_clip(key, pack / "previews", audio, raw_stream(video, streams))


def lyn_catalog(disc, pack, contents, jobs, coaches):
    previews = pack / "previews"
    previews.mkdir(exist_ok=True)
    songs, pending = [], []
    for index, key, title, artist, stem in LYN_DLC[disc]:
        if index not in contents:
            continue
        songs.append((title, artist, key, disc, pack.name, index))
        if coaches is not None and disc != "SD2Y41":
            archives = (entry for name, entry in u8_files(contents[index])
                        if name.endswith(b".bf") and
                        (b".wii." not in name or name.endswith(b".wii.tdt.bf")))
            entries = {name: entry for path, offset, _ in archives for name, entry in lyn_entries(path, offset)}
            counts = prepare_lyn_coach_art(entries, previews, {key: stem})
            coaches.update(((pack.name, name), count) for name, count in counts.items())
        if not ((previews / f"{key}.wav").is_file() and has_preview_video(previews, key)):
            pending.append((key, stem, contents[index]))
    if pending:
        with ProcessPoolExecutor(max_workers=min(6, jobs)) as workers:
            list(workers.map(build_lyn_preview, repeat(pack), pending))
    print(f"DLC: {len(songs)} routines from {disc}", flush=True)
    return songs


def prepare_dlc(folder, cache, open_disc, built_discs, jobs, coaches=None):
    wads = sorted(path for path in folder.glob("*") if path.suffix.lower() == ".wad")
    if not wads:
        return []
    cache.mkdir(parents=True, exist_ok=True)
    songs, imported = {}, set()
    for wad in wads:
        pack = import_wad(wad, cache, built_discs)
        if pack is None or pack[1] in imported:
            continue
        disc, local, contents = pack
        imported.add(local)
        if disc in LYN_DLC:
            edition = lyn_catalog(*pack, jobs, coaches)
        else:
            edition = ubiart_catalog(*pack, open_disc, jobs, coaches)
        for song in edition:
            _, _, key, _, _, index = song
            if (disc, key) not in songs:
                mount_song(disc, local, contents, index)
                songs[disc, key] = song
    return list(songs.values())
