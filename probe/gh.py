"""Native song, catalog and media keys from the verified USA Greatest Hits disc."""

from probe.lyn_previews import prepare_lyn_previews


# World key, title, artist, media stem, catalog node, reference offset, coach key.
SONGS = (
    ('76c04fb0', 'Acceptable in the 80s', 'Calvin Harris', 'Acceptable', 590353008, 866, '76c04f40'),
    ('2dc124cb', 'Airplanes', 'B.o.B (feat. Hayley Williams of Paramore)', 'Airplanes', 2445283183, 926, '2dc124b0'),
    ('76c05371', 'Alright', 'Supergrass', 'Alright', 590354721, 1070, '76c05301'),
    ('2dc144c4', "Baby Don't Stop Now", 'Anja', 'LoveMe', 590354665, 794, '2dc1445e'),
    ('76c053e8', 'Baby Girl', 'Reggaeton', 'BabyGirl', 590354631, 950, '76c05378'),
    ('76c09c03', 'Barbie Girl', "Countdown Dee's Hit Explosion", 'BarbieGirl', 590354621, 1142, '76c09b8b'),
    ('76c0d92b', "Body Movin' (Fatboy Slim Remix)", 'Beastie Boys', 'BodyMoving', 875562149, 998, '76c0d8bb'),
    ('76c0d9a2', 'Crazy In Love', 'Studio Musicians', 'CrazyInLove', 875562107, 914, '76c0d932'),
    ('76c06291', 'Dagomba', 'Sorcerer', 'Dagomba', 590354599, 1022, '76c06221'),
    ('76c074d0', 'DARE', 'Gorillaz', 'Dare', 875561465, 1046, '76c07460'),
    ('76c07931', 'Eye of the Tiger', 'Survivor', 'EyeOfTheTiger', 590352555, 1130, '76c078c1'),
    ('76c04661', 'Fame', 'In The Style of Irene Cara', 'Fame', 590350991, 1082, '76c045f1'),
    ('76c0814b', 'Firework', 'Katy Perry', 'Firework', 590354617, 722, '76c080db'),
    ('76c09b83', 'Girlfriend', 'Avril Lavigne', 'GirlFriend', 875562072, 746, '76c09b0b'),
    ('76c06b57', 'Hey Ya!', 'Outkast', 'HeyYa', 590354641, 938, '76c06ae7'),
    ('76c0da99', "Hot'n Cold", 'Katy Perry', 'HotNCold', 875562147, 734, '76c0da29'),
    ('76c06a69', 'I Like to Move It (Radio Mix)', 'Reel 2 Real feat. The Mad Stuntman', 'ILikeToMoveIt', 590354649, 1010, '76c069f9'),
    ('76c0697b', "It's Raining Men", 'The Weather Girls', 'ItsRainingMen', 590354651, 890, '76c0690b'),
    ('76c09cf1', 'Jai Ho! (You Are My Destiny)', 'A R Rahman, The Pussycat Dolls Featuring Nicole Scherzinger', 'JaiHo', 590354605, 818, '76c09cf2'),
    ('76c045ea', 'Jin Go Lo Ba', 'Fatboy Slim', 'JinGoLoBa', 875561470, 842, '76c0457a'),
    ('76c047c4', 'Jump', 'Studio Allstars', 'Jump', 875561803, 974, '76c0474c'),
    ('76c04932', 'Jump In The Line', 'The Sunlight Shakers', 'JumpInTheLine', 875561482, 1034, '76c048ba'),
    ('76c05471', 'Katti Kalandal', 'Bollywood', 'KattiKalandal', 590353976, 830, '76c053f9'),
    ('76c06d45', 'Kung Fu Fighting (Dave Ruffy // Mark Wallis Remix)', 'Carl Douglas', 'KungFu', 590354625, 1106, '76c06ccd'),
    ('2dc0e373', 'Only Girl (In the World)', 'Rihanna', 'OnlyGirl', 2344622440, 770, '2dc0e367'),
    ('76c0da22', 'Pon De Replay', 'Rihanna', 'Pon', 875562103, 782, '76c0d9b2'),
    ('76c044fc', 'Proud Mary', 'Ike & Tina Turner', 'ProudMary', 875561480, 902, '76c0448c'),
    ('76c06ae0', 'Rasputin', 'Boney M.', 'Rasputin', 590351583, 1154, '76c06a70'),
    ('76c0783a', 'Ring My Bell', 'Anita Ward', 'RingMyBell', 590354719, 878, '76c077ca'),
    ('76c048b2', 'Satisfaction (Isak Original Extended)', 'Benny Benassi presents "The Biz"', 'Satisfaction', 590351340, 854, '76c04842'),
    ('76c0520c', 'Step by Step', 'New Kids on the Block', 'StepByStep', 875561474, 1058, '76c0519c'),
    ('76c06e33', 'The Power', 'Snap!', 'Power', 590354709, 986, '76c06dc3'),
    ('76c0ddf3', 'TiK ToK', 'Ke$ha', 'TikTok', 875562092, 758, '76c0dc97'),
    ('76c0483b', 'Toxic', 'The Hit Crew', 'Toxic', 590352344, 806, '76c047cb'),
    ('76c04573', "U Can't Touch This", 'Groove Century', 'CantTouchThis', 590354645, 962, '76c04503'),
    ('76c077c3', 'Viva Las Vegas', 'Elvis Presley', 'VivaLasVegas', 875561476, 1118, '76c07753'),
    ('76c06dbc', 'Who Let the Dogs Out', 'The Sunlight Shakers', 'DogsOut', 590354715, 1094, '76c06d4c'),
)


def catalog():
    return [(title, artist, key) for key, title, artist, *_ in SONGS]


def prepare_previews(disc, destination, jobs):
    aliases = {key: (f"{stem}.wav", "JaiHo2.bik" if stem == "JaiHo" else f"{stem}.bik")
               for key, _, _, stem, *_ in SONGS}
    prepare_lyn_previews(disc, destination, aliases, "JD3SP2.bf", jobs)
