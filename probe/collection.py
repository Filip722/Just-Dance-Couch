"""Read Classic routines from local Wii discs and build the picker catalog."""

import json
import re
import struct
from unicodedata import normalize

from probe.previews import read_entry, song_assets


EDITIONS = {
    "SJNE41": "Just Dance 2016",
    "SZ7E41": "Just Dance 2017",
    "SE8E41": "Just Dance 2018",
    "S5EE41": "Just Dance 2019",
}


# Reissues with alternate song names or credits in the original game metadata.
SONG_ALIASES = {
    ("SDNE41", "27"): ("Hot'n Cold", "Katy Perry"),
    ("SDNE41", "30"): ("Who Let the Dogs Out", "The Sunlight Shakers"),
    ("SDNE41", "32"): ("U Can't Touch This", "Groove Century"),
    ("SD2Y41", "908026af"): ("Jump In The Line", "The Sunlight Shakers"),
    ("SJ9E41", "11300929"): ("Mambo No. 5 (A Little Bit Of Monika)", "Lou Bega"),
    ("SJ9E41", "11300fa9"): ("Moving On Up", "M People"),
    ("SJ9E41", "1430031f"): ("Professor Pumplestickle", "Nick Phoenix and Thomas Bergersen"),
    ("SJDE41", "76c06a66"): ("I Like to Move It (Radio Mix)", "Reel 2 Real"),
    ("SE3E41", "LetItGoDLC"): ("Let It Go", "Disney's Frozen"),
}


def string_at(data, offset):
    size, = struct.unpack_from(">I", data, offset)
    start = offset + 4
    end = start + size
    if end > len(data):
        raise ValueError("Truncated song descriptor string")
    return data[start:end].decode("utf-8"), end


def descriptor(entry):
    data = read_entry(entry)
    kind, = struct.unpack_from(">I", data, 48)
    if kind != 0x8AC2B5C6:
        raise ValueError("Unexpected UbiArt song descriptor")
    key, end = string_at(data, 56)
    mode, = struct.unpack_from(">I", data, end + 8)
    if mode:
        return None
    artist, end = string_at(data, end + 68)
    _, end = string_at(data, end)  # Original dancer name.
    title, _ = string_at(data, end)
    return title, artist, key


def song_order(song):
    return "".join(char for char in normalize("NFKD", song[0].casefold()) if char.isalnum() or char.isspace())


def catalog(disc):
    songs = []
    for key, assets in song_assets(disc).items():
        if not {"descriptor", "audio", "track"} <= assets.keys():
            continue
        song = descriptor(assets["descriptor"])
        if song:
            if not song[0] or song[2].lower().encode() != key:
                raise ValueError(f"Invalid song descriptor in {disc.name} for {key!r}")
            songs.append(song)
    return sorted(songs, key=song_order)


def classic_title(title):
    return title.removesuffix(" (Alternate)").removesuffix(" (Sweat)")


def song_identity(disc, song):
    title, artist = SONG_ALIASES.get((disc, song[2]), song[:2])
    lead_artist = re.split(r"\s+(?:feat(?:uring)?|ft)\.?\s+", artist.casefold(), maxsplit=1)[0]
    return tuple("".join(char for char in normalize("NFKD", field.casefold()) if char.isalnum())
                 for field in (classic_title(title), lead_artist.strip().removeprefix("the ")))


def newest_songs(collection, dlc):
    add_ons = {disc: [] for disc in collection}
    for song in dlc:
        add_ons[song[3]].append(song)

    newest = {}
    for disc, (_, songs) in collection.items():
        # Later entries win. Each edition prefers standard routines, with disc copies
        # after DLC copies when both offer the same routine.
        candidates = [*add_ons[disc], *songs]
        for song in sorted(candidates, key=lambda song: song[0] == classic_title(song[0])):
            newest[song_identity(disc, song)] = (disc, song)
    keep = set(newest.values())
    return (
        {disc: (edition, [song for song in songs if (disc, song) in keep])
         for disc, (edition, songs) in collection.items()},
        [song for song in dlc if (song[3], song) in keep],
    )


def write_catalog(staging, collection, dlc=()):
    collection, dlc = newest_songs(collection, dlc)
    pages = [(edition, f'"{disc}"', f"songs_{disc}", songs)
             for disc, (edition, songs) in collection.items()]
    if dlc:
        pages.append(("DLC", "NULL", "songs_dlc", sorted(dlc, key=song_order)))
    arrays, editions = [], []
    for edition, disc, name, songs in pages:
        entries = "".join(
            "  {" + ", ".join(json.dumps(field, ensure_ascii=False) for field in song) + "},\n"
            for song in songs
        )
        arrays.append(f"static const Song {name}[] = {{\n{entries}}};\n")
        editions.append(f'  {{"{edition}", {disc}, {name}, sizeof {name} / sizeof {name}[0]}},')
    (staging / "collection_catalog.h").write_text(
        "\n".join([*arrays, "const Edition editions[] = {", *editions, "};", ""]),
        encoding="utf-8",
    )
