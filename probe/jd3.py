"""Classic routine and media keys from the verified USA Just Dance 3 revision."""

from probe.lyn_previews import prepare_lyn_previews


# World key, title, artist, coach stem, catalog node, reference offset,
# coach key, and the audio stem shared by some choreography variants.
SONGS = (
    ('2dc0e950', 'Apache (Jump On It)', 'The Sugarhill Gang', 'Apache', 2344626793, 1250, '2dc0e943', 'Apache'),
    ('8dc00480', 'Are You Gonna Go My Way', 'Lenny Kravitz', 'GonnaGoMyWay', 2344626801, 1082, '8dc00473', 'GonnaGoMyWay'),
    ('2dc120b8', 'Baby One More Time', 'The Girly Team', 'BabyOneMoreQUAT', 2344634114, 782, '2dc120aa', 'BabyOneMore'),
    ('2dc08999', 'Baby Zouk', 'Dr. Creole', 'BabyZouk', 2344617379, 1490, '2dc08988', 'BabyZouk'),
    ('2dc0d790', 'Barbra Streisand', 'Duck Sauce', 'BarbraStreisand', 2344617381, 830, '2dc0d780', 'BarbraStreisand'),
    ('2dc0e2b6', 'Beautiful Liar', 'Countdown Mix Masters', 'BeautifulLiar', 2344622278, 1238, '2dc0e2aa', 'BeautifulLiar'),
    ('2dc0da3c', 'Boogie Wonderland', 'Groove Century', 'BoogieWonderQUAT', 2344621532, 974, '2dc0da2f', 'BoogieWonder'),
    ('2dc087e0', 'Boom', 'Reggaeton Storm', 'Boom', 2344617383, 1010, '2dc087cf', 'Boom'),
    ('2dc10692', 'California Gurls', 'Katy Perry featuring Snoop Dogg', 'CaliforniaGurls', 1925303886, 722, '2dc10685', 'CaliforniaGurls'),
    ('2dc0de42', 'Crazy Little Thing Called Love', 'Queen', 'CrazyLittle', 2344622296, 950, '2dc0de1a', 'CrazyLittle'),
    ('2dc0e990', 'Da Funk', 'Daft Punk', 'DaFunk', 2344626795, 1022, '2dc0e983', 'DaFunk'),
    ('8dc00440', 'Dance All Nite', 'Anja', 'GiveMeMore', 2344626799, 1046, '8dc00433', 'GiveMeMore'),
    ('2dc10eb2', 'Dynamite', 'Taio Cruz', 'DynamiteQUAT', 1036001782, 866, '2dc10ea4', 'Dynamite'),
    ('2dc1246c', 'Forget You', 'Cee Lo Green', 'ForgetYou', 1707085347, 902, '2dc12451', 'ForgetYou'),
    ('2dc0c049', 'Giddy On Up (Giddy On Out)', 'Laura Bell Bundy', 'GiddyOnUp', 2344622313, 1190, '2dc0c03d', 'GiddyOnUp'),
    ('2dc0bf4c', 'Gonna Make You Sweat (Everybody Dance Now)', 'Sweat Invaders', 'GonnaMake', 2344626813, 926, '2dc0bf40', 'GonnaMake'),
    ('2dc0e7b2', 'Hey Boy Hey Girl', 'The Chemical Brothers', 'HeyBoy', 2344626803, 986, '2dc0e7a5', 'HeyBoy'),
    ('2dc0f7b7', 'Hungarian Dance No. 5', 'Brahms by Just Dance Classical Orchestra', 'HungarianDance', 2344626805, 1478, '2dc0f7aa', 'HungarianDance'),
    ('2dc0e9d0', "I Don't Feel Like Dancin'", 'Scissor Sisters', 'FeelLikeDancing', 2344626797, 806, '2dc0e9c3', 'FeelLikeDancing'),
    ('2dc0bdee', 'I Feel Love', 'Donna Summer', 'IFeelLove', 2344622356, 1034, '2dc064e9', 'IFeelLove'),
    ('2dc0bece', "I Was Made For Lovin' You", 'Kiss', 'IWasMadeQUAT', 2344621454, 1202, '2dc0bec1', 'IWasMade'),
    ('2dc0e430', "I'm So Excited", 'Pointer Sisters', 'SoExcited', 2344622456, 1154, '2dc0e424', 'SoExcited'),
    ('2dc11f92', 'Jamaican Dance', 'Konshens', 'JamaicanDance', 3145745215, 1118, '2dc11f85', 'JamaicanDance'),
    ('2dc0e238', 'Jump (For My Love)', 'Girls Aloud', 'JumpGA', 2344622381, 914, '2dc0e210', 'JumpGA'),
    ('2dc08727', 'Kurio ko uddah le jana', 'Bollywood Rainbow', 'Kurio', 2344617387, 1178, '2dc08716', 'Kurio'),
    ('2dc0e910', 'Land Of 1000 Dances', 'Wilson Pickett', '1000Dances', 2344626637, 1094, '2dc0e903', '1000Dances'),
    ('8dc003c0', "Let's Go To The Mall", 'Robin Sparkles', 'ToTheMall', 2344626811, 1130, '8dc003b3', 'ToTheMall'),
    ('2dc0bf0d', 'Lollipop', 'MIKA', 'Lollipop', 2344622405, 758, '2dc0bf01', 'Lollipop'),
    ('2dc08a17', 'Mamasita', 'Latino Sunset', 'Mamasita', 2344617389, 1382, '2dc08a06', 'Mamasita'),
    ('2dc0d97e', 'Night Boat To Cairo', 'Madness', 'NightBoatQUAT', 2344622424, 1142, '2dc0d971', 'NightBoat'),
    ('2dc0e334', 'No Limit', '2 Unlimited', 'NoLimit', 2344622220, 854, '2dc0e328', 'NoLimit'),
    ('2dc0e3b2', 'Party Rock Anthem', 'LMFAO featuring Lauren Bennett and GoonRock', 'PartyRock', 2344622172, 1070, '2dc0e3a6', 'PartyRock'),
    ('2dc11f3c', 'Pata Pata', 'African Ladies', 'PataPata', 3766493362, 1262, '2dc11f2f', 'PataPata'),
    ('2dc11ee6', 'Price Tag', 'Jessie J featuring B.o.B', 'PriceTag', 3502256475, 794, '2dc11ed9', 'PriceTag'),
    ('8dc004c0', 'Promiscuous', 'Nelly Furtado featuring Timbaland', 'Promiscuous', 2344626807, 770, '8dc004b3', 'Promiscuous'),
    ('2dc0e3f1', 'Pump It', 'The Black Eyed Peas', 'PumpIt', 2344622188, 746, '2dc0e3e5', 'PumpIt'),
    ('2dc0e2f5', "She's Got Me Dancing", 'Tommy Sparks', 'GotMeDancing', 2344622331, 1106, '2dc0e2e9', 'GotMeDancing'),
    ('2dc0f776', "Somethin' Stupid", 'Robbie Williams and Nicole Kidman', 'SomethinStupid', 2344626829, 1286, '2dc0f769', 'SomethinStupid'),
    ('2dc10b17', 'Spectronizer', 'Sentai Express', 'SpectronizerQUAT', 1707085305, 1058, '2dc10b09', 'Spectronizer'),
    ('2dc0bf8b', 'Take On Me', 'a-ha', 'TakeOnMe', 2344621514, 734, '2dc0bf7f', 'TakeOnMe'),
    ('2dc0895a', 'The Master Blaster', 'Inspector Marceau', 'MasterBlaster', 2344617391, 1358, '2dc08949', 'MasterBlaster'),
    ('2dc088dc', 'Think', 'The London Theatre Orchestra & Cast', 'Think', 2344617393, 962, '2dc088cb', 'Think'),
    ('2dc11ff9', 'This is Halloween', 'Danny Elfman', 'HalloweenQUAT', 1036001814, 1298, '2dc11feb', 'Halloween'),
    ('2dc0bfca', 'Tightrope (Solo Version)', 'Janelle Monae', 'Tightrope', 2344622472, 1214, '2dc0bfbe', 'Tightrope'),
    ('2dc0e46f', 'Venus', 'Bananarama', 'VenusB', 2344622488, 842, '2dc0e463', 'VenusB'),
    ('2dc0c221', 'Video Killed The Radio Star', 'The Buggles', 'VideoKilled', 2344622505, 1166, '2dc0c246', 'VideoKilled'),
    ('2dc0c009', 'What You Waiting For', 'Gwen Stefani', 'WhatYouWait', 2344621470, 938, '2dc0bffd', 'WhatYouWait'),
)


def catalog():
    return [(title, artist, key) for key, title, artist, *_ in SONGS]


def prepare_previews(disc, destination, jobs):
    aliases = {key: (f"{audio}.wav", f"{video}.bik")
               for key, _, _, video, _, _, _, audio in SONGS}
    prepare_lyn_previews(disc, destination, aliases, "JD3.bf", jobs)
