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


def song_identity(song):
    title, artist = song[:2]
    lead_artist = re.split(r"\s+(?:feat(?:uring)?|ft)\.?\s+", artist.casefold(), maxsplit=1)[0]
    return tuple("".join(char for char in normalize("NFKD", field.casefold()) if char.isalnum())
                 for field in (classic_title(title), lead_artist.strip().removeprefix("the ")))


def newest_dlc_songs(dlc, edition_order):
    release_order = {disc: index for index, disc in enumerate(edition_order)}
    # Later editions win. Within an edition, the standard routine wins over alternates.
    ordered = sorted(dlc, key=lambda song: (release_order[song[3]], song[0] == classic_title(song[0])))
    newest = {song_identity(song): song for song in ordered}
    return list(newest.values())


def write_catalog(staging, collection, dlc=(), levels=None):
    levels = levels or {}
    dlc = newest_dlc_songs(dlc, collection.keys())
    pages = [(edition, disc, f"songs_{disc}", songs)
             for disc, (edition, songs) in collection.items()]
    if dlc:
        pages.append(("DLC", None, "songs_dlc", sorted(dlc, key=song_order)))
    arrays, editions = [], []
    for edition, disc, name, songs in pages:
        entries = []
        for song in songs:
            fields = [json.dumps(field, ensure_ascii=False) for field in song]
            if (disc, song) in levels:
                preview, gameplay = levels[disc, song]
                fields.extend((f".preview_gain = {preview:.7f}f", f".gameplay_gain = {gameplay:.7f}f"))
            entries.append("  {" + ", ".join(fields) + "},")
        arrays.append("\n".join([f"static const Song {name}[] = {{", *entries, "};", ""]))
        source = json.dumps(disc) if disc else "NULL"
        editions.append(f'  {{"{edition}", {source}, {name}, sizeof {name} / sizeof {name}[0]}},')
    (staging / "collection_catalog.h").write_text(
        "\n".join([*arrays, "const Edition editions[] = {", *editions, "};", ""]),
        encoding="utf-8",
    )
