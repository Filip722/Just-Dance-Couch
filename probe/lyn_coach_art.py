"""Read coach portraits in native lobby order from local LyN Wii archives."""

import ctypes
from functools import cache
import re
import struct

from probe.coach_art import cmpr_bmp, write_portrait
from probe.previews import read_entry


# Some Just Dance 4 DLC duets ship unused portraits for players 3 and 4.
JD4_PLACEHOLDERS = {0x2dc2e9ea, 0x2dc2e9ef, 0x2dc33eac, 0x2dc33eb1}


@cache
def lzo_decoder():
    library = ctypes.CDLL("liblzo2.so.2")
    decoder = library.lzo1x_decompress_safe
    decoder.argtypes = (
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p,
    )
    return decoder


def object_data(entry):
    archive, offset, length = entry
    with archive.open("rb") as source:
        source.seek(offset - 32)
        encoded, decoded, _, kind = struct.unpack("<4I", source.read(16))
        source.seek(offset)
        data = source.read(encoded)
    if encoded > length or len(data) != encoded:
        raise ValueError("Truncated LyN object")
    if kind != 4:
        return data

    version, chunks = struct.unpack_from("<2I", data)
    if version != 1 or not 0 < chunks <= (len(data) - 8) // 4:
        raise ValueError("Invalid LyN compressed object")
    sizes = struct.unpack_from(f"<{chunks}I", data, 8)
    cursor = 8 + 4 * chunks
    output = bytearray()
    decompress = lzo_decoder()
    for size in sizes:
        if not 0 < size <= len(data) - cursor or len(output) >= decoded:
            raise ValueError("Invalid LyN LZO block size")
        # The asset packer uses one LZO block per 256 KiB minus a size word.
        limit = ctypes.c_size_t(min(0x3fffc, decoded - len(output)))
        chunk = ctypes.create_string_buffer(limit.value)
        if decompress(data[cursor:cursor + size], size, chunk, ctypes.byref(limit), None):
            raise ValueError("Invalid LyN LZO block")
        output.extend(chunk.raw[:limit.value])
        cursor += size
    if len(output) != decoded or cursor != len(data):
        raise ValueError("Invalid LyN decompressed size")
    return bytes(output)


def texture_bank(data):
    count, = struct.unpack_from("<H", data)
    offset = 2
    textures = {}
    for _ in range(count):
        key, _, _, size = struct.unpack_from("<4I", data, offset)
        end = offset + 12 + size
        # The embedded bank keeps the texture ID in the record header.
        textures[key] = data[offset + 16:offset + 24] + data[offset:offset + 4] + data[offset + 24:end]
        offset = end
    return textures


def texture_plane(data, format_code):
    version, kind, _, width, height = struct.unpack_from("<IIIHH", data)
    if (version != 2 or kind & 0xff != format_code or not width or not height or
            width % 8 or height % 8 or len(data) < 16 + width * height // 2):
        raise ValueError("Invalid LyN coach texture")
    return width, height, data[16:16 + width * height // 2]


def prepare_lyn_coach_art(entries, destination, maps):
    counts = {}
    for key, stem in maps.items():
        world = entries.get(f"lobby_{stem.lower().removesuffix('_wii')}.wog".encode("ascii"))
        if not world:
            continue
        try:
            # The last loaded object holds the routine's coach materials. Earlier
            # world objects supply shared lobby decorations.
            objects = list(struct.iter_unpack("<III", read_entry(world)))
            lobby, _, _ = objects[-1]
            suffix = f"{lobby & 0x7ffff:05x}.bin".encode("ascii")
            model = object_data(entries[b"fff" + suffix])
            materials = []
            for type_name in re.finditer(rb"K3D_material_WiiSimpleTex\0", model):
                marker, color, alpha = struct.unpack_from("<3I", model, type_name.end() + 66)
                if marker == 0x00020507:
                    materials.append((color, alpha))
            if not 2 <= len(materials) <= 4:
                continue
            # Material references name texture objects. The object's last
            # word names its embedded or streamed texture data.
            resources = {}
            pattern = rb"(.{4})([\x2c\x2d])\0{3}[\x3d-\x3f]\0{3}\xde\xc0\xde\xc0"
            for record in re.finditer(pattern, model, re.DOTALL):
                source, = struct.unpack("<I", record[1])
                data_key, = struct.unpack_from("<I", model, record.start() + 4 + record[2][0])
                resources[source] = data_key
            bank = entries.get(b"fef" + suffix)
            if not bank:
                textures = {index: read_entry(entries[f"{index:08x}.tdt".encode("ascii")])
                            for index in (resources[ref] for pair in materials for ref in pair)}
                materials = [pair for pair in materials
                             if struct.unpack_from("<I", textures[resources[pair[0]]], 8)[0] not in JD4_PLACEHOLDERS]
                if len(materials) < 2:
                    continue
            paths = [destination / f"{key}.coach-{i}.bmp" for i in range(1, len(materials) + 1)]
            if not all(path.is_file() for path in paths):
                if bank:
                    textures = texture_bank(object_data(bank))
                for path, (rgb, mask) in zip(paths, materials, strict=True):
                    if path.is_file():
                        continue
                    width, height, color = texture_plane(textures[resources[rgb]], 9)
                    mask_width, mask_height, alpha = texture_plane(textures[resources[mask]], 27)
                    write_portrait(path, cmpr_bmp(width, height, color, alpha,
                                                  mask_size=(mask_width, mask_height), flip=True))
            counts[key] = len(materials)
        except (KeyError, IndexError, ValueError, struct.error) as error:
            print(f"preview: {key}: {error}", flush=True)
    return counts
