"""Cache coach portraits from the Wii menu assets for local song selection."""

from collections import defaultdict
from pathlib import Path
import re
import struct
import tempfile

from probe.previews import read_entry


PORTRAIT = re.compile(rb"([a-z0-9_]+)_coach_([1-4])\.tga\.ckd")


def rgb565(value):
    red, green, blue = value >> 11, (value >> 5) & 63, value & 31
    return ((red << 3) | (red >> 2), (green << 2) | (green >> 4), (blue << 3) | (blue >> 2))


def cmpr_colors(first, second):
    left, right = rgb565(first), rgb565(second)
    if first > second:
        near_left = tuple((5 * a + 3 * b) >> 3 for a, b in zip(left, right))
        near_right = tuple((3 * a + 5 * b) >> 3 for a, b in zip(left, right))
        return left, right, near_left, near_right
    middle = tuple((a + b) // 2 for a, b in zip(left, right))
    return left, right, middle, middle


def portrait_bmp(data):
    # The Wii CMPA texture stores two tiled CMPR planes, one for color and one for alpha.
    header, = struct.unpack_from(">I", data, 8)
    if data[4:8] != b"TEX\0" or data[header:header + 4] != b" SDD" or data[header + 84:header + 88] != b"APMC":
        raise ValueError("Expected a Wii CMPA coach texture")
    height, width = struct.unpack_from(">II", data, header + 12)
    start = header + 128
    plane = width * height // 2
    if not (0 < width <= 1024 and 0 < height <= 1024 and width % 8 == height % 8 == 0 and
            len(data) == start + 2 * plane):
        raise ValueError("Invalid Wii coach texture dimensions")

    pixels = bytearray(width * height * 4)
    for top in range(0, height, 8):
        for left in range(0, width, 8):
            for dx, dy in ((0, 0), (4, 0), (0, 4), (4, 4)):
                rgb = cmpr_colors(*struct.unpack_from(">HH", data, start))
                alpha = cmpr_colors(*struct.unpack_from(">HH", data, start + plane))
                for row in range(4):
                    rgb_bits = data[start + 4 + row]
                    alpha_bits = data[start + plane + 4 + row]
                    row_offset = ((top + dy + row) * width + left + dx) * 4
                    for column in range(4):
                        shift = 6 - column * 2
                        intensity = alpha[(alpha_bits >> shift) & 3][0]
                        rgba = (*rgb[(rgb_bits >> shift) & 3], intensity)
                        at = row_offset + column * 4
                        pixels[at:at + 4] = bytes(rgba)
                start += 8

    # A top-down BMP with explicit channel masks preserves transparency in SDL.
    header = struct.pack("<IiiHHIIiiII4I", 108, width, -height, 1, 32, 3, len(pixels), 0, 0, 0, 0,
                         0xff, 0xff00, 0xff0000, 0xff000000).ljust(108, b"\0")
    return struct.pack("<2sIHHI", b"BM", 122 + len(pixels), 0, 0, 122) + header + pixels


def prepare_coach_art(entries, destination, maps):
    keys = {key.lower().encode("ascii") for key in maps}
    portraits = defaultdict(dict)
    for folder, name, entry in entries:
        match = PORTRAIT.fullmatch(name) if folder.endswith(b"/menuart/textures/") else None
        if match and match[1] in keys:
            portraits[match[1].decode("ascii")][int(match[2])] = entry

    counts = {}
    for key, coaches in portraits.items():
        if len(coaches) < 2 or set(coaches) != set(range(1, len(coaches) + 1)):
            continue
        try:
            for index, entry in coaches.items():
                path = destination / f"{key}.coach-{index}.bmp"
                if not path.is_file():
                    destination.mkdir(parents=True, exist_ok=True)
                    image = portrait_bmp(read_entry(entry))
                    with tempfile.NamedTemporaryFile(dir=destination, delete_on_close=False) as temporary:
                        temporary.write(image)
                        temporary.flush()
                        Path(temporary.name).replace(path)
        except (ValueError, struct.error) as error:
            print(f"preview: {key}: {error}", flush=True)
            continue
        counts[key] = len(coaches)
    return counts
