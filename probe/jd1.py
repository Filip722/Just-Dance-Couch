"""Original USA Just Dance song IDs and local Bink assets."""

from probe.previews import PREVIEW_SECONDS, write_preview


# Observed in Global_AddSong on the verified SDNE41 executable.
SONGS = (
    ('29', 'A Little Less Conversation (JXL Radio Edit Remix)', 'Elvis vs. JXL', 'Conversation'),
    ('36', 'Acceptable in the 80s', 'Calvin Harris', 'Acceptable'),
    ('21', 'Bebe', 'Divine Brown', 'Bebe'),
    ('28', "Can't Get You Out of My Head", 'Kylie Minogue', 'CantGetYou'),
    ('25', 'Cotton Eye Joe', 'Rednex', 'Cotton'),
    ('18', 'DARE', 'Gorillaz', 'Dare'),
    ('24', 'Eye of the Tiger', 'Survivor', 'Eye'),
    ('3', 'Fame', 'In the style of Irene Cara', 'fame'),
    ('9', 'Funplex (CSS Remix)', 'The B-52s', 'Funplex'),
    ('20', 'Girls And Boys', 'Blur', 'G&B'),
    ('0', 'Girls Just Want To Have Fun', 'Cyndi Lauper', 'girls'),
    ('12', 'Groove Is in the Heart', 'Deee-Lite', 'delite'),
    ('35', 'Heart of Glass', 'Blondie', 'Heartofglass'),
    ('27', 'Hot N Cold (Chick Version)', 'Katy Perry', 'HotnCold'),
    ('19', 'I Get Around', 'The Beach Boys', 'Get around'),
    ('23', 'I Like to Move It (Radio Mix)', 'Reel 2 Real feat. The Mad Stuntman', 'I like to move it'),
    ('22', 'Jerk It Out', 'Caesars', 'Jerk it out'),
    ('16', 'Jin Go Lo Ba', 'Fatboy Slim', 'jingoloba'),
    ('11', 'Kids in America', 'Kim Wilde', 'Kids'),
    ('34', 'Le Freak', 'Chic', 'le freak'),
    ('10', 'Louie Louie', 'Iggy Pop', 'Louie'),
    ('17', 'Lump', 'The Presidents of the United States of America', 'Lump'),
    ('8', 'Mashed Potato Time', 'Dee Dee Sharp', 'Potatoe'),
    ('13', 'Pump Up the Jam', 'Technotronic', 'Pump'),
    ('5', 'Ring My Bell', 'Anita Ward', 'Ring'),
    ('31', 'Step By Step', 'New Kids On The Block', 'Stepbystep'),
    ('1', "Surfin' Bird", 'The Trashmen', 'surfingbird'),
    ('15', "That's the Way (I Like It)", 'KC and the Sunshine Band', 'thatstheway'),
    ('32', "U Can't Touch This", 'MC Hammer', 'UCanttouch'),
    ('26', 'Wanna Be', 'Spice Girls', 'Wanabe'),
    ('30', 'Who Let the Dogs Out', 'Baha Men', 'Dogs'),
    ('2', 'Womanizer', 'The Gym All-Stars', 'womanizer'),
)


def catalog():
    return [(title, artist, key) for key, title, artist, _ in SONGS]


def prepare_previews(disc, destination):
    destination.mkdir(parents=True, exist_ok=True)
    videos = disc / "files" / "videos"
    for key, _, _, stem in SONGS:
        if (destination / f"{key}.rgb").is_file():
            continue
        # The Wii's Bink files carry the coach image. Music is streamed from a separate bank.
        movie = (videos / f"{stem}169.bik").read_bytes()
        frame_count = int.from_bytes(movie[8:12], "little")
        rate = int.from_bytes(movie[28:32], "little")
        rate_divisor = int.from_bytes(movie[32:36], "little")
        cue = max(0, (frame_count * rate_divisor / rate - PREVIEW_SECONDS) / 2)
        write_preview(key, destination, cue, None, None, movie)
