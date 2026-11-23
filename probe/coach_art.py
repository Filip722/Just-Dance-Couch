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

    return cmpr_bmp(width, height, data[start:start + plane], data[start + plane:])


def cmpr_rgb(width, height, plane):
    pixels = bytearray(width * height * 3)
    offset = 0
    for top in range(0, height, 8):
        for left in range(0, width, 8):
            for dx, dy in ((0, 0), (4, 0), (0, 4), (4, 4)):
                palette = tuple(bytes(color) for color in cmpr_colors(*struct.unpack_from(">HH", plane, offset)))
                for row in range(4):
                    bits = plane[offset + 4 + row]
                    start = ((top + dy + row) * width + left + dx) * 3
                    pixels[start:start + 12] = b"".join(palette[(bits >> shift) & 3] for shift in (6, 4, 2, 0))
                offset += 8
    return pixels


def cmpr_bmp(width, height, color, mask, *, mask_size=None, flip=False):
    rgb = cmpr_rgb(width, height, color)
    mask_width, mask_height = mask_size or (width, height)
    alpha = cmpr_rgb(mask_width, mask_height, mask)[::3]
    if (mask_width, mask_height) != (width, height):
        # Some LyN lobby materials pair a high-resolution color map with a smaller mask.
        alpha = bytes(alpha[y * mask_height // height * mask_width + x * mask_width // width]
                      for y in range(height) for x in range(width))
    pixels = bytearray(width * height * 4)
    for channel in range(3):
        pixels[channel::4] = rgb[channel::3]
    pixels[3::4] = alpha
    if flip:
        row = width * 4
        pixels = b"".join(pixels[y * row:(y + 1) * row] for y in range(height - 1, -1, -1))

    # A top-down BMP with explicit channel masks preserves transparency in SDL.
    header = struct.pack("<IiiHHIIiiII4I", 108, width, -height, 1, 32, 3, len(pixels), 0, 0, 0, 0,
                         0xff, 0xff00, 0xff0000, 0xff000000).ljust(108, b"\0")
    return struct.pack("<2sIHHI", b"BM", 122 + len(pixels), 0, 0, 122) + header + pixels


def write_portrait(path, image):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete_on_close=False) as temporary:
        temporary.write(image)
        temporary.flush()
        Path(temporary.name).replace(path)


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
                    write_portrait(path, portrait_bmp(read_entry(entry)))
        except (ValueError, struct.error) as error:
            print(f"preview: {key}: {error}", flush=True)
            continue
        counts[key] = len(coaches)
    return counts
