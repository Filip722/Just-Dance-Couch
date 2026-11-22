"""Measure the cached song previews once and calculate each song's playback gains."""

from concurrent.futures import ThreadPoolExecutor
import json
import math
import re
import subprocess


# The 44 Just Dance 4 disc previews have a median of -7.4 LUFS before the
# game's -2.5 dB mix gain or the picker's original 0.55x gain is applied.
GAMEPLAY_LUFS = -10.0
PREVIEW_LUFS = -12.5
PREVIEW_PEAK_DBTP = -1.0
MAX_BOOST_DB = 12.0  # Bound the gain estimated from unusually quiet excerpts.

# Music gain measured from native gameplay against the original audio streams.
# Just Dance 4 and 2015–2019 all use approximately -2.5 dB.
GAME_MIX_DB = {
    "SDNE41": -2.0,  # Just Dance
    "SD2Y41": -6.0,  # Just Dance 2
    "SJ9E41": -8.9,  # Summer Party
    "SJDE41": -2.0,  # Just Dance 3
    "SJHE41": -2.0,  # Greatest Hits
    "SJOE41": -1.5,  # Just Dance 2014
}


def measure(path):
    run = subprocess.run(
        ["ffmpeg", "-nostdin", "-hide_banner", "-nostats", "-threads", "1", "-filter_threads", "1",
         "-i", str(path), "-af", "ebur128=peak=true:framelog=verbose", "-f", "null", "-"],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )
    summary = run.stderr.rpartition(b"Summary:")[2]
    levels = re.search(rb"\bI:\s+(-?[\d.]+) LUFS.*\bPeak:\s+(-?(?:[\d.]+|inf)) dBFS", summary, re.S)
    if run.returncode or levels is None:
        print(f"Audio loudness unavailable for {path.name}", flush=True)
        return None
    loudness, peak = map(float, levels.groups())
    return [loudness, peak] if math.isfinite(peak) else None


def playback_gains(disc, loudness, peak):
    preview_db = min(PREVIEW_LUFS - loudness, PREVIEW_PEAK_DBTP - peak, MAX_BOOST_DB)
    game_db = min(GAMEPLAY_LUFS - loudness - GAME_MIX_DB.get(disc, -2.5), MAX_BOOST_DB)
    return 10 ** (preview_db / 20), 10 ** (game_db / 20)


def prepare_loudness(cache, collection, dlc, jobs):
    paths = {(disc, song): cache / "previews" / disc / (song[2].lower() + ".wav")
             for disc, (_, songs) in collection.items() for song in songs}
    paths.update({(None, song): cache / "dlc" / song[4] / "previews" / (song[2].lower() + ".wav")
                  for song in dlc})
    index = cache / "preview-loudness.json"
    try:
        measurements = json.loads(index.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        measurements = {}

    available, pending = {}, {}
    for song, path in paths.items():
        try:
            stat = path.stat()
        except FileNotFoundError:
            continue
        name = path.relative_to(cache).as_posix()
        signature = [stat.st_mtime_ns, stat.st_size]
        available[song] = name
        if name not in measurements or measurements[name][0] != signature:
            pending[name] = (path, signature)

    if pending:
        with ThreadPoolExecutor(max_workers=min(jobs, 6)) as pool:
            results = pool.map(measure, (path for path, _ in pending.values()))
            for (name, (_, signature)), levels in zip(pending.items(), results):
                measurements[name] = [signature, levels]
        temporary = index.with_suffix(".tmp")
        temporary.write_text(json.dumps(measurements))
        temporary.replace(index)
        print(f"Measured loudness for {len(pending)} previews", flush=True)

    return {(disc, song): playback_gains(disc or song[3], *measurements[name][1])
            for (disc, song), name in available.items() if measurements[name][1] is not None}
