"""Read the local Just Dance 3 coach and audio streams from LyN archives."""

from concurrent.futures import ProcessPoolExecutor
from itertools import repeat
import struct

from probe.previews import decode_dsp_clip, read_entry, write_preview


JD3_SONGS = {"2dc0e3f1": "PumpIt", "2dc0bf8b": "TakeOnMe"}

# The LyN Wii RIFF codec 0x5050 uses fixed Nintendo DSP coefficients.
LYN_DSP_COEFFICIENTS = struct.unpack(
    ">16h", bytes.fromhex("04ab fced 0789 fedf 09a2 fae5 0c90 fac1 084d faa4 0982 fdf7 0af6 fafa 0be6 fbf5")
)


def entries(archive):
    with archive.open("rb") as source:
        header = source.read(28)
        if header[:8] != b"ABE\0\x05\0\0\0":
            raise ValueError(f"Unexpected LyN archive {archive}")
        table, = struct.unpack_from("<I", header, 24)
        source.seek(table)
        count, = struct.unpack_from("<I", source.read(12))
        for _ in range(count):
            record = source.read(200)
            if len(record) != 200:
                raise ValueError(f"Truncated LyN file table in {archive}")
            name = record[:64].split(b"\0", 1)[0]
            high, low = struct.unpack_from("<II", record, 0x68)
            encoded, = struct.unpack_from("<I", record, 0x58)
            if name and encoded > 32:
                yield name.lower(), (archive, (high << 32 | low) + 32, encoded - 32)


def raw_stream(alias, streams):
    target = read_entry(alias).split(b"\0", 1)[0]
    if not target.startswith(b"<$shadow$>/"):
        raise ValueError("Expected a LyN media shadow")
    return read_entry(streams[target.rsplit(b"/", 1)[1].replace(b".$hd$", b"").lower()])


def audio_clip(audio):
    start = audio.find(b"RIFF", 0, 64)
    if start < 0 or audio[start + 8:start + 12] != b"WAVE":
        raise ValueError("Expected a LyN Wii RIFF stream")
    chunks = {}
    offset = start + 12
    while offset + 8 <= len(audio):
        kind, size = struct.unpack_from("<4sI", audio, offset)
        chunks[kind] = memoryview(audio)[offset + 8:offset + 8 + size]
        offset += 8 + size + (size % 2)
    codec, channels, rate = struct.unpack_from("<HHI", chunks[b"fmt "])
    if (codec, channels, rate) != (0x5050, 2, 32000):
        raise ValueError("Expected stereo LyN Wii DSP audio")
    frames, = struct.unpack_from("<I", chunks[b"fact"])
    cue = frames / (2 * rate)
    samples = decode_dsp_clip(chunks[b"data"], rate, (LYN_DSP_COEFFICIENTS,) * 2, cue)
    return cue, rate, samples


def prepare_preview(key, destination, audio, video, media):
    cue, rate, samples = audio_clip(raw_stream(audio, media))
    movie = raw_stream(video, media)
    if not movie.startswith(b"BIK"):
        raise ValueError(f"Expected a Bink coach video for {key}")
    write_preview(key, destination, cue, rate, samples, movie)


def prepare_lyn_previews(disc, destination, songs, archive, jobs):
    pending = {key: names for key, names in songs.items()
               if not ((destination / f"{key}.wav").is_file() and (destination / f"{key}.rgb").is_file())}
    if not pending:
        return
    destination.mkdir(parents=True, exist_ok=True)
    files = disc / "files"
    family = archive.split(".", 1)[0]
    aliases = dict(entries(files / archive))
    media = dict(entries(files / f"{family}.wii.wav.bf"))
    for video in sorted(files.glob(f"{family}.wii.bik.bf*")):
        media.update(entries(video))
    audio, video = zip(*(tuple(aliases[name.lower().encode("ascii")] for name in names)
                         for names in pending.values()))
    with ProcessPoolExecutor(max_workers=jobs) as work:
        for _ in work.map(prepare_preview, pending, repeat(destination), audio, video, repeat(media)):
            pass


def prepare_jd3_previews(disc, destination):
    songs = {key: (f"{stem}.wav", f"{stem}.bik") for key, stem in JD3_SONGS.items()}
    prepare_lyn_previews(disc, destination, songs, "JD3.bf", 1)
