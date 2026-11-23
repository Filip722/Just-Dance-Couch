"""Read the Kids and Disney Party song tables and Bink previews from local discs."""

from array import array
from concurrent.futures import ProcessPoolExecutor
from itertools import repeat
import struct
import subprocess

from probe.collection import song_order
from probe.dlc import u8_files
from probe.previews import PREVIEW_SECONDS, fade_preview, has_preview_video, read_entry, write_preview


FAMILY_EDITIONS = {
    "SDZE41": "Just Dance Kids",
    "SJZE41": "Just Dance Kids 2",
    "SJ6E41": "Just Dance Disney Party",
    "SJ7E41": "Just Dance Kids 2014",
    "S5DE41": "Just Dance Disney Party 2",
}


def table(data):
    magic, columns, rows, strings = struct.unpack_from(">4sIII", data)
    end = 16 + columns + 4 * (columns - 1) * rows
    if magic != b"DBLE" or columns < 2 or end + 4 * strings > len(data):
        raise ValueError("Invalid Land Ho! data table")
    kinds = data[16:15 + columns]
    offsets = struct.unpack_from(f">{strings}I", data, end)
    text = data[end + 4 * strings:]

    def decode(index, kind):
        if kind == ord("s"):
            at = offsets[index]
            return text[at:text.index(b"\0", at)].decode("utf-8")
        if kind == ord("S"):
            at = offsets[index]
            end = next(pos for pos in range(at, len(text), 2) if text[pos:pos + 2] == b"\0\0")
            return text[at:end].decode("utf-16be")
        return index

    return [tuple(decode(index, kind) for index, kind in zip(row, kinds, strict=True))
            for row in struct.iter_unpack(f">{columns - 1}I", data[16 + columns:end])]


def songs(disc):
    files = disc / "files"
    movies = {path.name.lower(): path for path in (files / "movies").iterdir() if path.suffix.lower() == ".bik"}
    messages = files / "message"
    kids = disc.name == "SDZE41"
    keys = table((messages / ("message_key.bin" if kids else "message_key_wii.bin")).read_bytes())
    values = table((messages / ("message_eng.bin" if kids else "message_us_wii.bin")).read_bytes())
    english = {key[0]: value[0].replace("%b", " ").strip() for key, value in zip(keys, values, strict=True)}
    archive = dict(u8_files(files / "data_tbl" / "game_data.arc"))
    rows = table(read_entry(archive[b"music_data.bin"]))
    if kids:
        # The first game keeps 36-byte song records in the verified DOL. Each
        # record gives the movie pointer and a separate, zero-based message ID.
        program = (disc / "sys" / "main.dol").read_bytes()
        first_record, record_bytes = 0x3d68bc, 36
        names, labels = [], []
        for index in range(len(rows)):
            movie, _, _, message = struct.unpack_from(">4I", program, first_record + index * record_bytes)
            offset = movie - 0x80003f20
            path = program[offset:program.index(b"\0", offset)].decode("ascii")
            names.append(path.removeprefix("movies/"))
            labels.append(message + 1)
        if not all(name in movies for name in names) or sorted(labels) != list(range(1, len(rows) + 1)):
            raise ValueError("Just Dance Kids movie table did not match its song table")
    elif disc.name == "SJZE41":
        numbered = {int(path.name[:2]): path.name for path in movies.values() if path.name[:2].isdigit()}
        # The executable's standard Wii playlist indexes movie numbers from
        # zero. Later database rows reuse those movies for other platforms.
        names = [numbered.get(index + 1, "") for index in range(len(rows))]
        labels = range(1, len(rows) + 1)
    else:
        names = [row[21] for row in rows]

    result = []
    for index, (row, name) in enumerate(zip(rows, names, strict=True)):
        movie = movies.get(name.lower())
        if not movie:
            continue  # The table can include rows from another region or console.
        if kids or disc.name == "SJZE41":
            number = labels[index]
            title_key = f"MESS_{'MUSIC' if kids else 'SONG'}_NAME_{number:04}"
            artist_key = f"MESS_ARTIST_NAME_{number:04}"
            title, artist = english[title_key], english.get(artist_key, "")
        else:
            title, artist = english.get(row[18]) or row[20], english.get(row[19], "")
        key = f"{index}-{movie.stem.lower()}"
        result.append((title, artist if artist != "-" else "", key, movie))
    return sorted(result, key=song_order)


def build_preview(song, destination):
    _, _, key, movie = song
    with movie.open("rb") as source:
        header = source.read(36)
    frames, = struct.unpack_from("<I", header, 8)
    rate, divisor = struct.unpack_from("<II", header, 28)
    cue = max(0, (frames * divisor / rate - PREVIEW_SECONDS) / 2)
    hz = 32000
    samples = subprocess.check_output([
        "ffmpeg", "-v", "error", "-nostdin", "-threads", "1", "-ss", str(cue), "-i", str(movie),
        "-map", "0:a:0", "-t", str(PREVIEW_SECONDS), "-ac", "2", "-ar", str(hz), "-f", "s16le", "-",
    ])
    pcm = array("h")
    pcm.frombytes(samples)
    write_preview(key, destination, cue, hz, fade_preview(pcm, hz), movie)


def prepare_previews(destination, catalog, jobs):
    pending = [song for song in catalog if not (
        (destination / f"{song[2]}.wav").is_file() and has_preview_video(destination, song[2])
    )]
    if pending:
        destination.mkdir(parents=True, exist_ok=True)
        with ProcessPoolExecutor(max_workers=min(6, jobs)) as pool:
            list(pool.map(build_preview, pending, repeat(destination)))
