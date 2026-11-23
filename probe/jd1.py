"""Original USA Just Dance song IDs and local coach and music streams."""

from array import array
import struct
import sys

from probe.previews import PREVIEW_SECONDS, fade_preview, has_preview_video, write_preview


# Song ID, title, artist, coach stem and sound bank from the SDNE41 Global_AddSong calls.
SONGS = (
    ('29', 'A Little Less Conversation (JXL Radio Edit Remix)', 'Elvis vs. JXL', 'Conversation', 0x0abc),
    ('36', 'Acceptable in the 80s', 'Calvin Harris', 'Acceptable', 0x0ac0),
    ('21', 'Bebe', 'Divine Brown', 'Bebe', 0x0a31),
    ('28', "Can't Get You Out of My Head", 'Kylie Minogue', 'CantGetYou', 0x0ac4),
    ('25', 'Cotton Eye Joe', 'Rednex', 'Cotton', 0x0ac8),
    ('18', 'DARE', 'Gorillaz', 'Dare', 0x0fb4),
    ('24', 'Eye of the Tiger', 'Survivor', 'Eye', 0x0acc),
    ('3', 'Fame', 'In the style of Irene Cara', 'fame', 0x07e9),
    ('9', 'Funplex (CSS Remix)', 'The B-52s', 'Funplex', 0x0928),
    ('20', 'Girls And Boys', 'Blur', 'G&B', 0x0a2d),
    ('0', 'Girls Just Want To Have Fun', 'Cyndi Lauper', 'girls', 0x000e),
    ('12', 'Groove Is in the Heart', 'Deee-Lite', 'delite', 0x0fa3),
    ('35', 'Heart of Glass', 'Blondie', 'Heartofglass', 0x0b11),
    ('27', 'Hot N Cold (Chick Version)', 'Katy Perry', 'HotnCold', 0x0ad0),
    ('19', 'I Get Around', 'The Beach Boys', 'Get around', 0x0a25),
    ('23', 'I Like to Move It (Radio Mix)', 'Reel 2 Real feat. The Mad Stuntman', 'I like to move it', 0x0a21),
    ('22', 'Jerk It Out', 'Caesars', 'Jerk it out', 0x0fbb),
    ('16', 'Jin Go Lo Ba', 'Fatboy Slim', 'jingoloba', 0x0fb3),
    ('11', 'Kids in America', 'Kim Wilde', 'Kids', 0x092c),
    ('34', 'Le Freak', 'Chic', 'le freak', 0x0b0d),
    ('10', 'Louie Louie', 'Iggy Pop', 'Louie', 0x0924),
    ('17', 'Lump', 'The Presidents of the United States of America', 'Lump', 0x0a51),
    ('8', 'Mashed Potato Time', 'Dee Dee Sharp', 'Potatoe', 0x0920),
    ('13', 'Pump Up the Jam', 'Technotronic', 'Pump', 0x09d8),
    ('5', 'Ring My Bell', 'Anita Ward', 'Ring', 0x0914),
    ('31', 'Step By Step', 'New Kids On The Block', 'Stepbystep', 0x0ad4),
    ('1', "Surfin' Bird", 'The Trashmen', 'surfingbird', 0x0018),
    ('15', "That's the Way (I Like It)", 'KC and the Sunshine Band', 'thatstheway', 0x0fb2),
    ('32', "U Can't Touch This", 'MC Hammer', 'UCanttouch', 0x0ad8),
    ('26', 'Wanna Be', 'Spice Girls', 'Wanabe', 0x0adc),
    ('30', 'Who Let the Dogs Out', 'Baha Men', 'Dogs', 0x0ae0),
    ('2', 'Womanizer', 'The Gym All-Stars', 'womanizer', 0x07e3),
)


def catalog():
    return [(title, artist, key) for key, title, artist, _, _ in SONGS]


def music_preview(packages, bank, cue):
    path = packages / f"Music_0x{bank:08x}.pk"
    package = path.read_bytes()
    if package[:4] != b"\x01\x22\x00\x0d":
        raise ValueError(f"Unexpected Just Dance sound bank {path}")
    index_size, streams = struct.unpack_from("<II", package, 4)
    count, names_size = struct.unpack_from("<II", package, streams)
    names = package[streams + 8:streams + 8 + names_size]
    if count != 1 or not names.endswith(b".spk\0"):
        raise ValueError(f"Expected one external stream in {path}")
    stream_id, name_offset, stream_offset, stream_size = struct.unpack_from(
        "<IIII", package, streams + 8 + names_size
    )
    if name_offset or len(names) != 29 or any(c not in b"0123456789abcdef" for c in names[:-5]):
        raise ValueError(f"Unexpected Just Dance stream name in {path}")

    offset = 64 + index_size
    for _, bao_size in struct.iter_unpack("<II", package[64:offset]):
        header = offset + 0x28
        if bao_size >= 0xa4:
            kind, size = struct.unpack_from(">II", package, header + 4)
            resource, = struct.unpack_from(">I", package, header + 0x1c)
            if kind == 1 and resource == stream_id:
                channels, rate, frames, encoding = struct.unpack_from(">I4xI4xI12xI", package, header + 0x44)
                if (channels, rate, encoding) != (2, 32000, 1) or frames * 4 != size:
                    break
                length = PREVIEW_SECONDS * rate
                first = min(round(cue * rate), frames - length)
                with (packages / names[:-1].decode("ascii")).open("rb") as stream:
                    stream.seek(stream_offset)
                    version, header_size = struct.unpack(">4sI", stream.read(8))
                    if (version != b"\x02\x22\x00\x0d" or header_size != 0x28
                            or first < 0 or header_size + size > stream_size):
                        break
                    stream.seek(stream_offset + header_size + first * 4)
                    pcm = stream.read(length * 4)
                if len(pcm) != length * 4:
                    break
                samples = array("h", pcm)
                if sys.byteorder != "little":
                    samples.byteswap()
                return rate, fade_preview(samples, rate)
        offset += bao_size
    raise ValueError(f"Expected a stereo PCM track in {path}")


def prepare_previews(disc, destination):
    destination.mkdir(parents=True, exist_ok=True)
    videos = disc / "files" / "videos"
    packages = disc / "files" / "sound/bin/Wii/Packages"
    for key, _, _, stem, bank in SONGS:
        needs_video = not has_preview_video(destination, key)
        needs_audio = not (destination / f"{key}.wav").is_file()
        if not (needs_video or needs_audio):
            continue
        with (videos / f"{stem}169.bik").open("rb") as source:
            header = source.read(36)
            movie = header + source.read() if needs_video else None
        frame_count, = struct.unpack_from("<I", header, 8)
        rate, divisor = struct.unpack_from("<II", header, 28)
        cue = max(0, (frame_count * divisor / rate - PREVIEW_SECONDS) / 2)
        sample_rate, samples = music_preview(packages, bank, cue) if needs_audio else (None, None)
        write_preview(key, destination, cue, sample_rate, samples, movie)
