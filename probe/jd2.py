"""Song and carousel metadata from the verified USA Just Dance 2 and Summer Party discs."""

from probe.lyn_previews import prepare_lyn_previews


# World key, original title and artist, media stem, carousel slot, and the
# slot field's byte offset in the decompressed fff00e6a.bin catalog.
JD2_SONGS = (
    ('90802070', 'Alright', 'Supergrass', 'Alright', 44, 15011),
    ('9080126b', 'A-Punk', 'Vampire Weekend', 'APunk', 3, 13593),
    ('90802576', 'Baby Girl', 'Reggaeton', 'BabyGirl', 25, 16283),
    ('90801930', 'Big Girl (You Are Beautiful)', 'MIKA', 'BigGirl', 15, 17470),
    ('90802adc', "Body Movin' (Fatboy Slim Remix)", 'Beastie Boys', 'BodyMoving', 42, 18657),
    ('90802021', 'Call Me', 'Blondie', 'CallMe', 20, 21339),
    ('908020bd', 'Cosmic Girl', 'Jamiroquai', 'CosmicGirl', 41, 22530),
    ('9080210b', 'Crazy In Love', 'Studio Musicians', 'CrazyInLove', 27, 23798),
    ('90802bc6', 'Dagomba', 'Sorcerer', 'ElectroTribal', 10, 26407),
    ('90802381', 'D.A.N.C.E.', 'Justice', 'Dance', 29, 25135),
    ('2dc01a9c', 'Funkytown', 'Sweat Invaders ', 'FunkyTown', 47, 27181),
    ('90802613', 'Girlfriend', 'Avril Lavigne', 'GirlFriend', 8, 28446),
    ('48800317', 'Hey Ya!', 'Outkast', 'HeyYa', 23, 29714),
    ('90802834', 'Holiday', 'The Hit Crew', 'Holiday', 19, 30905),
    ('90800c6d', 'Hot Stuff', 'Donna Summer', 'HotStuff', 14, 32254),
    ('90801a20', 'I Got You (I Feel Good)', 'James Brown', 'IGotYou', 4, 34798),
    ('908014e2', 'I Want You Back', 'The Jackson 5', 'IWantYouBack', 16, 38671),
    ('90801980', 'Idealistic', 'Digitalism', 'Idealistic', 7, 33526),
    ('90801444', 'Iko Iko', 'Mardi Gras', 'IkoIko', 17, 36135),
    ('90801493', "It's Raining Men", 'The Weather Girls', 'ItsRainingMen', 1, 37411),
    ('2dc01a40', 'Jai Ho! (You Are My Destiny)', 'A R Rahman, The Pussycat Dolls Featuring Nicole Scherzinger', 'JaiHo', 48, 39607),
    ('908021a8', 'Jump', 'Studio Allstars', 'Jump', 36, 40941),
    ('908026af', 'Jump In The Line', 'Harry Belafonte', 'JumpInTheLine', 32, 42290),
    ('908019d0', 'Jungle Boogie', 'Studio Musicians', 'JungleBoogie', 26, 43558),
    ('90802a8f', 'Katti Kalandal', 'Bollywood', 'Bollywood', 18, 20002),
    ('90800c43', 'Monster Mash', 'The Frighteners', 'MonsterMash', 37, 44668),
    ('90802243', 'Move Your Feet', 'Junior Senior', 'MoveYourFeet', 11, 45936),
    ('9080274b', 'Mugsy Baloney', 'Charleston', 'MugsyBaloney', 24, 47281),
    ('90801ac0', 'Proud Mary', 'Ike & Tina Turner', 'ProudMary', 13, 49817),
    ('93801d03', 'Rasputin', 'Boney M.', 'Rasputin', 31, 51085),
    ('90802798', 'Rockafeller Skank', 'Fatboy Slim', 'Rockafeller', 45, 52353),
    ('908027e6', 'Satisfaction (Isak Original Extended)', 'Benny Benassi presents "The Biz"', 'Satisfaction', 22, 53463),
    ('908022df', 'Should I Stay or Should I Go', 'The Clash', 'ShouldIStay', 46, 56068),
    ('488002c9', 'S.O.S.', 'Rihanna', 'SOS', 9, 57336),
    ('9080104a', 'Soul Bossa Nova', 'Quincy Jones & His Orchestra', 'SoulBossaNova', 28, 58592),
    ('90801531', 'Sway (Quien Sera)', 'Marine Band', 'Sway', 21, 59945),
    ('90801a70', 'Sympathy For The Devil (Fatboy Slim Remix)', 'The Rolling Stones', 'SympathyDevil', 30, 61059),
    ('9080232d', 'Take Me Out', 'Franz Ferdinand', 'TakeMeOut', 38, 62327),
    ('90802b2a', "That's Not My Name", 'The Ting Tings', 'ThatsNotMyName', 39, 63587),
    ('90800d15', 'The Power', 'Snap!', 'Power', 35, 48549),
    ('9080188b', "The Shoop Shoop Song (It's In His Kiss)", 'Cher', 'ShoopShoop', 40, 54877),
    ('90801713', 'TiK ToK', 'Ke$ha', 'TikTok', 2, 64863),
    ('90803105', 'Toxic', 'The Hit Crew', 'Toxic', 6, 72706),
    ('90802b78', 'Viva Las Vegas', 'Elvis Presley', 'VivaLasVegas', 43, 66204),
    ('90801580', 'Wake Me Up Before You Go Go', 'Wham!', 'WakeMeUp', 33, 67464),
    ('90800d45', 'Walk Like An Egyptian', 'Bangles', 'WalkLike', 34, 70081),
    ('90800cc1', 'When I Grow Up', 'The Pussycat Dolls', 'WhenIGrowUp', 5, 68740),
)


# Summer Party uses ordered node references in place of explicit carousel slots.
# The fifth and sixth values are the node key and its offset in fff00e6a.bin.
SUMMER_SONGS = (
    ('1430099b', 'American Boy', 'Estelle featuring Kanye West', 'AmericanBoy', 254822752, 738),
    ('64304abc', 'Barbie Girl', "Countdown Dee's Hit Explosion", 'BarbieGirl', 1697664782, 858),
    ('11300c47', 'Born To Be Wild', 'Steppenwolf', 'BornToBeWild', 288363272, 834),
    ('113012c1', 'Chicken Payback', 'A Band of Bees', 'ChickenPayback', 288363221, 930),
    ('64304979', 'Crying Blood', 'V V Brown', 'CryingBlood', 1697664735, 942),
    ('0f304108', 'Down By The Riverside', 'The Reverend Horatio Duncan & Amos Sweets', 'RiverSide', 254822580, 918),
    ('0f303d64', 'Firework', 'Katy Perry', 'Firework', 254822546, 702),
    ('113014a5', 'Funkytown', 'Sweat Invaders', 'FunkyTown', 288364587, 822),
    ('643003d9', 'Futebol Crazy', 'The World Cup Girls', 'Futebol', 254822744, 750),
    ('64304c42', 'Here Comes the Hotstepper', 'The Hit Crew', 'HotStep', 1697664799, 870),
    ('11301816', 'Jai Ho! (You Are My Destiny)', 'A R Rahman, The Pussycat Dolls Featuring Nicole Scherzinger', 'JaiHo', 288364607, 714),
    ('0f303f9e', 'Kung Fu Fighting (Dave Ruffy//Mark Wallis Remix)', 'Carl Douglas', 'KungFu', 254822563, 894),
    ('11300929', 'Mambo No. 5 (A Little Bit Of Monika)', 'The Lemon Cubes', 'Mambo5', 288363238, 954),
    ('64300298', 'Maniac', 'Studio Allstars', 'Maniac', 254822667, 762),
    ('11300fa9', 'Moving On Up', 'The Lemon Cubes', 'MovingOnUp', 288363289, 966),
    ('0f3042bc', 'Nine In The Afternoon', 'Panic At The Disco', 'NineAfternoon', 254822597, 798),
    ('64304716', 'Pon De Replay', 'Rihanna', 'Pon', 1697664601, 726),
    ('1430031f', 'Professeur Pumplestickle', 'Nick Phoenix and Thomas Bergersen', 'Professeur', 254822746, 846),
    ('64304870', 'Pump Up The Volume', 'M//A//R//R//S', 'PumpUp', 1697664688, 810),
    ('0f30464c', 'Skin-To-Skin', 'Sweat Invaders', 'SkinToSkin', 254822632, 774),
    ('1130119b', 'Song 2', 'Blur', 'Song2', 288363306, 786),
    ('0f304599', 'Why Oh Why', 'Love Letter', 'WhyOWhy', 254822614, 882),
    ('11300afb', "You Can't Hurry Love", 'The Supremes', 'CantHurryLove', 288363255, 906),
)

EDITIONS = {
    "SD2Y41": ("Just Dance 2", JD2_SONGS),
    "SJ9E41": ("Just Dance Summer Party", SUMMER_SONGS),
}


def catalog(disc_id):
    name, songs = EDITIONS[disc_id]
    return name, [(title, artist.strip(), key) for key, title, artist, _, _, _ in songs]


def prepare_previews(disc_id, disc, destination, jobs):
    if disc_id == "SJ9E41":
        video_aliases = {"JaiHo": "JaiHo2", "ChickenPayback": "ChikenPayback"}
    else:
        video_aliases = {"JaiHo": "JaiHor"}
    songs = {key: (f"{stem}.wav", f"{video_aliases.get(stem, stem)}.bik")
             for key, _, _, stem, _, _ in EDITIONS[disc_id][1]}
    archive = "WD2.bbf" if (disc / "files/WD2.bbf").is_file() else "WD2.bf"
    prepare_lyn_previews(disc, destination, songs, archive, jobs)
