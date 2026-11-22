"""Build short local audio and coach previews from verified Wii discs."""

from array import array
from concurrent.futures import ProcessPoolExecutor
from itertools import repeat
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave


# The two initial songs already had verified preview cues.
PREVIEW_ENTRIES = {"justdance": 99.8319166667, "getlucky": 100.8291458333}
PREVIEW_SECONDS = 8
PREVIEW_WIDTH, PREVIEW_HEIGHT, PREVIEW_FPS = 160, 120, 10
FRAME_BYTES = PREVIEW_WIDTH * PREVIEW_HEIGHT * 3


def archive_entries(folder):
    for archive in sorted(folder.glob("*.ipk")):
        if not archive.name.lower().startswith("bundle"):
            continue
        with archive.open("rb") as source:
            magic, version, _, base, count = struct.unpack(">5I", source.read(20))
            if (magic, version) != (0x50EC12BA, 5):
                raise ValueError(f"Unexpected Wii archive {archive}")
            limit = archive.stat().st_size
            source.seek(48)
            for _ in range(count):
                _, size, packed, _, offset, length = struct.unpack(">IIIQQI", source.read(32))
                first = source.read(length)
                length, = struct.unpack(">I", source.read(4))
                second = source.read(length)
                source.read(8)
                directory, name = (first, second) if first.endswith(b"/") else (second, first)
                if not packed and base + offset + size <= limit and size:
                    yield directory.lower(), name.lower(), (archive, base + offset, size)


def read_entry(entry):
    archive, offset, length = entry
    with archive.open("rb") as source:
        source.seek(offset)
        return source.read(length)


def preview_cue(key, track):
    if key in PREVIEW_ENTRIES:
        return PREVIEW_ENTRIES[key]
    # Wii music-track sample markers are measured at 48 kHz. Sample the middle
    # of the disc's arrangement when a marked preview cue is unavailable.
    count, = struct.unpack_from(">I", track, 64)
    if count < 2 or 68 + 4 * count > len(track):
        raise ValueError("Invalid Wii music-track markers")
    marker, = struct.unpack_from(">I", track, 68 + 4 * (count // 2))
    return marker / 48000


def decode_preview(data, start):
    if data[:4] != b"RAKI" or data[4:8] not in (b"\0\0\0\x08", b"\0\0\0\x09") or data[8:16] != b"Wii adpc":
        raise ValueError("Expected a Wii RAKI ADPCM track")
    chunks = {kind: (offset, size) for kind, offset, size in struct.iter_unpack(">4sII", data[32:80])}
    format_offset, _ = chunks[b"fmt "]
    _, channels, rate = struct.unpack_from(">HHI", data, format_offset)
    stream, size = chunks[b"datS"]
    if channels != 2 or rate not in (32000, 48000) or stream + size > len(data):
        raise ValueError("Expected stereo Wii audio at 32 or 48 kHz")
    coefficients = [
        struct.unpack_from(">16h", data, chunks[k][0] + 28) for k in (b"dspL", b"dspR")
    ]
    return rate, decode_dsp_clip(memoryview(data)[stream:stream + size], rate, coefficients, start)


def decode_dsp_clip(data, rate, coefficients, start):
    length = PREVIEW_SECONDS * rate
    first = min(round(start * rate), len(data) // 16 * 14 - length)
    if first < 0:
        raise ValueError("Preview falls outside its Wii track")
    end = first + length
    samples = array("h", [0]) * (length * 2)
    for channel, channel_coefficients in enumerate(coefficients):
        previous = older = 0
        sample_index = 0
        for offset in range(channel * 8, ((end + 13) // 14) * 16, 16):
            header = data[offset]
            predictor = header >> 4
            if predictor >= 8:
                raise ValueError("Invalid Wii ADPCM predictor")
            scale = 2048 << (header & 15)
            first_coefficient, second_coefficient = channel_coefficients[predictor * 2:predictor * 2 + 2]
            for packed in data[offset + 1:offset + 8]:
                for nibble in (packed >> 4, packed & 15):
                    signed = nibble if nibble < 8 else nibble - 16
                    value = (1024 + scale * signed + previous * first_coefficient + older * second_coefficient) >> 11
                    older, previous = previous, max(-32768, min(32767, value))
                    if first <= sample_index < end:
                        i = sample_index - first
                        samples[i * 2 + channel] = previous
                    sample_index += 1
    fade = rate // 5
    for i in range(fade):
        for channel in range(2):
            for frame in (i, length - 1 - i):
                at = frame * 2 + channel
                samples[at] = round(samples[at] * i / fade)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def write_preview(key, destination, cue, rate, samples, video):
    with tempfile.TemporaryDirectory(dir=destination) as staging:
        staging = Path(staging)
        wav = staging / "preview.wav"
        if samples is not None:
            with wave.open(str(wav), "wb") as output:
                output.setparams((2, 2, rate, 0, "NONE", "not compressed"))
                output.writeframes(samples)
        if video:
            source, rgb = staging / "coach", staging / "preview.rgb"
            source.write_bytes(video)
            args = [
                "ffmpeg", "-v", "error", "-nostdin", "-threads", "1", "-ss", str(cue),
                "-i", str(source), "-an", "-filter_threads", "1",
                "-vf", f"fps={PREVIEW_FPS},scale={PREVIEW_WIDTH}:{PREVIEW_HEIGHT}",
                "-frames:v", str(PREVIEW_SECONDS * PREVIEW_FPS), "-threads", "1",
                "-pix_fmt", "rgb24", "-f", "rawvideo", str(rgb),
            ]
            result = subprocess.run(args, capture_output=True)
            if result.returncode == 0 and rgb.stat().st_size == FRAME_BYTES * PREVIEW_FPS * PREVIEW_SECONDS:
                rgb.replace(destination / f"{key}.rgb")
            else:
                print(f"preview: {key} coach video unavailable", flush=True)
        if samples is not None:
            wav.replace(destination / f"{key}.wav")


def render_preview(job, destination):
    key, audio, track, video = job
    try:
        cue = preview_cue(key, read_entry(track))
        rate, samples = decode_preview(read_entry(audio), cue)
        write_preview(key, destination, cue, rate, samples, read_entry(video) if video else None)
    except (KeyError, ValueError, struct.error) as error:
        print(f"preview: {key}: {error}", flush=True)


def song_assets(disc):
    collections = {}
    for directory, name, entry in archive_entries(disc / "files"):
        if name == b"songdesc.main_legacy.tpl.ckd" and b"/legacyconverteddata/" in directory:
            key = directory.rstrip(b"/").rsplit(b"/", 1)[-1]
            collections.setdefault(key, {})["descriptor"] = entry
        if directory.endswith(b"/audio/"):
            key = directory.rstrip(b"/").rsplit(b"/", 2)[-2]
            if name == key + b".wav.ckd":
                collections.setdefault(key, {})["audio"] = entry
            elif name in (key + b"_musictrack.tpl.ckd", key + b"_musictrack.main_legacy.tpl.ckd"):
                collections.setdefault(key, {})["track"] = entry
        elif directory.endswith(b"/videoscoach/"):
            key = directory.rstrip(b"/").rsplit(b"/", 2)[-2]
            if name == key + b".wii.webm":
                collections.setdefault(key, {})["video"] = entry
    return collections


def prepare_previews(disc: Path, destination: Path, jobs=6, songs=None):
    destination.mkdir(parents=True, exist_ok=True)
    collections = song_assets(disc)
    pending = []
    for key, entries in collections.items():
        if "audio" not in entries or "track" not in entries:
            continue
        key = key.decode("ascii")
        if songs is not None and key not in songs:
            continue
        if (destination / f"{key}.wav").is_file() and (
            "video" not in entries or (destination / f"{key}.rgb").is_file()
        ):
            continue
        pending.append((key, entries["audio"], entries["track"], entries.get("video")))
    if pending:
        with ProcessPoolExecutor(max_workers=min(6, jobs)) as workers:
            list(workers.map(render_preview, pending, repeat(destination)))
