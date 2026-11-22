"""Read locally available Classic routines from newer UbiArt discs."""

import json
import struct
from unicodedata import normalize

from probe.previews import read_entry, song_assets


EDITIONS = {
    "SJNE41": "Just Dance 2016",
    "SZ7E41": "Just Dance 2017",
    "SE8E41": "Just Dance 2018",
    "S5EE41": "Just Dance 2019",
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
    return sorted(songs, key=lambda song: "".join(
        char for char in normalize("NFKD", song[0].casefold()) if char.isalnum() or char.isspace()
    ))


def write_catalog(staging, collection):
    arrays, editions = [], []
    for disc, (edition, songs) in collection.items():
        name = f"songs_{disc}"
        entries = "".join(
            "  {" + ", ".join(json.dumps(field, ensure_ascii=False) for field in song) + "},\n"
            for song in songs
        )
        arrays.append(f"static const Song {name}[] = {{\n{entries}}};\n")
        editions.append(f'  {{"{edition}", "{disc}", {name}, sizeof {name} / sizeof {name}[0]}},')
    (staging / "collection_catalog.h").write_text(
        "\n".join(arrays) + "#define COLLECTION_EDITIONS " + " ".join(editions) + "\n"
    )
