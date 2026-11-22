"""Song and carousel metadata from the verified USA Just Dance 2 disc."""

from probe.lyn_previews import prepare_lyn_previews


# World key, original title and artist, media stem, carousel slot, and the
# slot field's byte offset in the decompressed fff00e6a.bin catalog.
SONGS = (
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


def catalog():
    return [(title, artist.strip(), key) for key, title, artist, _, _, _ in SONGS]


def prepare_previews(disc, destination, jobs):
    songs = {key: (f"{stem}.wav", "JaiHor.bik" if stem == "JaiHo" else f"{stem}.bik")
             for key, _, _, stem, _, _ in SONGS}
    archive = "WD2.bbf" if (disc / "files/WD2.bbf").is_file() else "WD2.bf"
    prepare_lyn_previews(disc, destination, songs, archive, jobs)
