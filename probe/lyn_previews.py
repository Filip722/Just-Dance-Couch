"""Read local coach and audio streams from LyN Wii archives."""

from concurrent.futures import ProcessPoolExecutor
from itertools import repeat
import struct

from probe.previews import decode_dsp_clip, read_entry, write_preview


# The LyN Wii RIFF codec 0x5050 uses fixed Nintendo DSP coefficients.
LYN_DSP_COEFFICIENTS = struct.unpack(
    ">16h", bytes.fromhex("04ab fced 0789 fedf 09a2 fae5 0c90 fac1 084d faa4 0982 fdf7 0af6 fafa 0be6 fbf5")
)


def entries(archive, start=0):
    with archive.open("rb") as source:
        source.seek(start)
        header = source.read(28)
        if header[:8] != b"ABE\0\x05\0\0\0":
            raise ValueError(f"Unexpected LyN archive {archive}")
        table, = struct.unpack_from("<I", header, 24)
        source.seek(start + table)
        count, = struct.unpack_from("<I", source.read(12))
        for _ in range(count):
            record = source.read(200)
            if len(record) != 200:
                raise ValueError(f"Truncated LyN file table in {archive}")
            name = record[:64].split(b"\0", 1)[0]
            high, low = struct.unpack_from("<II", record, 0x68)
            encoded, = struct.unpack_from("<I", record, 0x58)
            if name and encoded > 32:
                yield name.lower(), (archive, start + (high << 32 | low) + 32, encoded - 32)


def raw_stream(alias, streams):
    data = read_entry(alias)
    if not data.startswith(b"<$shadow$>/"):
        return data
    target = data.split(b"\0", 1)[0]
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


def mobiclip_packets(movie):
    # FFmpeg decodes MobiClip video through its MODS demuxer. Keep the Wii
    # video packets and use a video-only MODS header to pass them through.
    end = 8 + struct.unpack_from("<I", movie, 4)[0]
    offset = 8
    tags = {}
    while offset < end:
        tag, words = struct.unpack_from("<2sH", movie, offset)
        offset += 4
        tags[tag] = movie[offset:offset + words * 4]
        offset += words * 4
    if offset != end or b"HE" not in tags:
        raise ValueError("Invalid Wii MobiClip header")
    fps, frames = struct.unpack_from("<II", tags[b"TL"])
    width, height = struct.unpack_from("<II", tags[b"V2"])
    packets = bytearray(b"MODSN3\x0a\0" + struct.pack("<4I16x2I", frames, width, height, fps << 16, 40, 48))
    for _ in range(frames):
        chunk_bytes, video_bytes = struct.unpack_from("<II", movie, offset)
        if not 0 < video_bytes <= chunk_bytes - 4 or offset + 8 + video_bytes > len(movie):
            raise ValueError("Invalid Wii MobiClip frame")
        packets += struct.pack("<I", video_bytes << 14)
        packets += movie[offset + 8:offset + 8 + video_bytes]
        offset += chunk_bytes
        offset += 4 - offset % 4
    return packets


def prepare_preview(key, destination, audio, video, media):
    write_lyn_clip(key, destination, raw_stream(audio, media), raw_stream(video, media))


def write_lyn_clip(key, destination, audio, movie):
    cue, rate, samples = audio_clip(audio)
    if movie.startswith(b"MOC5"):
        write_preview(key, destination, cue, rate, samples, mobiclip_packets(movie), seek_before_input=False)
    elif movie.startswith(b"BIK"):
        write_preview(key, destination, cue, rate, samples, movie)
    else:
        raise ValueError(f"Unexpected coach video for {key}")


def prepare_lyn_previews(disc, destination, songs, archive, jobs, *, movie_extension="bik"):
    pending = {key: names for key, names in songs.items()
               if not ((destination / f"{key}.wav").is_file() and (destination / f"{key}.rgb").is_file())}
    if not pending:
        return
    destination.mkdir(parents=True, exist_ok=True)
    files = disc / "files"
    family = archive.split(".", 1)[0]
    aliases = dict(entries(files / archive))
    media = dict(entries(files / f"{family}.wii.wav.bf"))
    for video in sorted(files.glob(f"{family}.wii.{movie_extension}.bf*")):
        media.update(entries(video))
    audio, video = zip(*(tuple(aliases[name.lower().encode("ascii")] for name in names)
                         for names in pending.values()))
    with ProcessPoolExecutor(max_workers=jobs) as work:
        for _ in work.map(prepare_preview, pending, repeat(destination), audio, video, repeat(media)):
            pass
