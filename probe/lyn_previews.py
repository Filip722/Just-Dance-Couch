"""Read the local Just Dance 3 coach and audio streams from LyN archives."""

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


def prepare_jd3_previews(disc, destination):
    pending = {key: stem for key, stem in JD3_SONGS.items()
               if not ((destination / f"{key}.wav").is_file() and (destination / f"{key}.rgb").is_file())}
    if not pending:
        return
    destination.mkdir(parents=True, exist_ok=True)
    files = disc / "files"
    aliases = dict(entries(files / "JD3.bf"))
    media = dict(entries(files / "JD3.wii.wav.bf"))
    for video in sorted(files.glob("JD3.wii.bik.bf*")):
        media.update(entries(video))
    for key, stem in pending.items():
        basename = stem.lower().encode("ascii")
        cue, rate, samples = audio_clip(raw_stream(aliases[basename + b".wav"], media))
        video = raw_stream(aliases[basename + b".bik"], media)
        if not video.startswith(b"BIK"):
            raise ValueError(f"Expected a Bink coach video for {stem}")
        write_preview(key, destination, cue, rate, samples, video)
