"""Classic album and choreography data from the verified USA Just Dance 4 disc."""

from probe.lyn_previews import prepare_lyn_previews


# World key, title, artist, coach stem, original album key, album offset
# within its decompressed block, choreography scene key, and audio stem.
SONGS = (
    ('2dc22c60', 'Asereje (The Ketchup Song)', 'Las Ketchup', 'KetchupSong', 767700064, 52179, 'fff22c7a', 'KetchupSong'),
    ('2dc29906', 'Beauty and a Beat', 'Justin Bieber featuring Nicki Minaj', 'Beauty', 767727878, 52595, 'fff298fe', 'Beauty'),
    ('2dc226a3', 'Beware Of The Boys (Mundian To Bach Ke)', 'Panjabi MC', 'BewareOf', 767698595, 53219, 'fff2269b', 'BewareOf'),
    ('2dc2ee54', 'Brand New Start', 'Anja', 'LetMeFeelYa', 767749716, 55923, 'fff2ee4c', 'LetMeFeelYa'),
    ('2dc2573f', 'Call Me Maybe', 'Carly Rae Jepsen', 'CallMeMaybe', 767711039, 51555, 'fff25737', 'CallMeMaybe'),
    ('2dc1d9e2', "Can't Take My Eyes Off You", 'Boys Town Gang', 'CantTakeMyEyes', 767678946, 54883, 'fff1d9da', 'CantTakeMyEyes'),
    ('2dc25d73', 'Crazy Little Thing', 'Anja', 'SexyLittleThing', 767712627, 52803, 'fff25d6b', 'SexyLittleThing'),
    ('2dc1d848', 'Crucified', 'Army Of Lovers', 'CrucifiedQUAT', 767678536, 54155, 'fff1d840', 'CrucifiedQUAT'),
    ('2dc1c05c', 'Disturbia', 'Rihanna', 'Disturbia', 767672412, 51867, 'fff1c054', 'Disturbia'),
    ('2dc1cc1c', 'Everybody Needs Somebody To Love', 'Dancing Bros.', 'EverybodyNeeds', 767675420, 52699, 'fff1cc14', 'EverybodyNeeds'),
    ('2dc21bcf', 'Good Feeling', 'Flo Rida', 'GoodFeeling', 767695823, 51659, 'fff21bbd', 'GoodFeeling'),
    ('2dc2ed4a', 'Good Girl', 'Carrie Underwood', 'GoodGirl', 767749450, 56027, 'fff2ed42', 'GoodGirl'),
    ('2dc1e867', "Hit 'Em Up Style (Oops!)", 'Blu Cantrell', 'HitEmUp', 767682663, 52283, 'fff1e85f', 'HitEmUp'),
    ('2dc229c8', 'Hot For Me', 'A.K.A', 'HotForMe', 767699400, 52075, 'fff229c0', 'HotForMe'),
    ('2dc22482', 'I Like It', 'The Blackout Allstars', 'ILikeItLike', 767698050, 53947, 'fff22478', 'ILikeItLike'),
    ('2dc25c63', 'Istanbul', 'They Might Be Giants', 'IstanbulQUAT', 767712355, 55299, 'fff25c62', 'IstanbulQUAT'),
    ('2dc1d108', "(I've Had) The Time Of My Life", 'Bill Medley & Jennifer Warnes', 'TheTimeOfMyLife', 767676680, 55507, 'fff1d100', 'TheTimeOfMyLife'),
    ('2dc22d6b', 'Jailhouse Rock', 'Elvis Presley', 'JailHouseQUAT', 767700331, 54675, 'fff22d85', 'JailHouseQUAT'),
    ('2dc25b5f', "Livin' La Vida Loca", 'Ricky Martin', 'LivinLaVida', 767712095, 53635, 'fff25b57', 'LivinLaVida'),
    ('2dc1f687', 'Love You Like A Love Song', 'Selena Gomez and the Scene', 'LoveYouLike', 767686279, 54051, 'fff1f67e', 'LoveYouLike'),
    ('2dc1e795', 'Maneater', 'Nelly Furtado', 'Maneater', 767682453, 53427, 'fff1e78d', 'Maneater'),
    ('2dc25847', 'Mas Que Nada', 'Sergio Mendes featuring The Black Eyed Peas', 'MasQueNada', 767711303, 54259, 'fff2583f', 'MasQueNada'),
    ('2dc21a02', 'Moves Like Jagger', 'Maroon 5 ft. Christina Aguilera', 'MovesLikeJag', 767695362, 54779, 'fff219f9', 'MovesLikeJag'),
    ('2dc23945', 'Mr. Saxobeat', 'Alexandra Stan', 'MrSaxoBeat', 767703365, 54571, 'fff2393d', 'MrSaxoBeat'),
    ('2dc21ade', 'Never Gonna Give You Up', 'Rick Astley', 'NeverGonna', 767695582, 53323, 'fff21acc', 'NeverGonna'),
    ('2dc25a57', 'Oh No!', 'Marina and The Diamonds', 'OhNo', 767711831, 53843, 'fff25a4f', 'OhNo'),
    ('2dc30cde', 'On The Floor', 'Jennifer Lopez featuring Pitbull', 'OnTheFloor', 767757534, 53115, 'fff30cd6', 'OnTheFloor'),
    ('2dc1daaf', 'Oops!... I Did It Again', 'The Girly Team', 'IDidItAgainQUAT', 767679151, 52491, 'fff1daa7', 'IDidItAgainQUAT'),
    ('2dc1dba7', 'Rock Lobster', "The B-52's", 'RockLobster', 767679399, 51763, 'fff1db9f', 'RockLobster'),
    ('2dc2594f', "Rock N' Roll (Will Take You To The Mountain)", 'Skrillex', 'RockNRoll', 767711567, 55611, 'fff25947', 'RockNRoll'),
    ('2dc22291', 'Run The Show', 'Kat DeLuna featuring Busta Rhymes', 'RunTheShow', 767697553, 53531, 'fff22287', 'RunTheShow'),
    ('2dc1fe57', 'So What', 'P!nk', 'SoWhat', 767688279, 55195, 'fff1fe4e', 'SoWhat'),
    ('2dc2eb36', "Some Catchin' Up To Do", 'Sammy', 'CatchinUp', 767748918, 54363, 'fff2eb2e', 'CatchinUp'),
    ('2dc1e41f', 'Super Bass', 'Nicki Minaj', 'SuperBass', 767681567, 52907, 'fff1e416', 'SuperBass'),
    ('2dc1fd79', 'Superstition', 'Stevie Wonder', 'Superstition', 767688057, 55403, 'fff1fd70', 'Superstition'),
    ('2dc227b2', 'The Final Countdown', 'Europe', 'TheFinalCountdown', 767698866, 54467, 'fff227aa', 'TheFinalCountdown'),
    ('2dc297f6', 'Time Warp', 'Halloween Thrills', 'TimeWarpQUAT', 767727606, 55715, 'fff297f5', 'TimeWarpQUAT'),
    ('2dc1f5a9', 'Tribal Dance', '2 Unlimited', 'TribalDance', 767686057, 53011, 'fff1f5a0', 'TribalDance'),
    ('2dc1d6ae', 'Umbrella', 'Rihanna', 'Umbrella', 767678126, 56131, 'fff1d6a6', 'Umbrella'),
    ('2dc1f882', 'We No Speak Americano', 'Hit The Electro Beat', 'Americano', 767686786, 52387, 'fff1f879', 'Americano'),
    ('2dc25e77', 'What Makes You Beautiful', 'One Direction', 'MakesYouBeautifulQUAT', 767712887, 51971, 'fff25e76', 'MakesYouBeautifulQUAT'),
    ('2dc1e5dd', 'Wild Wild West', 'Will Smith', 'WildWildWestQUAT', 767682013, 55091, 'fff1e5d4', 'WildWildWestQUAT'),
    ('2dc22597', 'You Make Me Feel...(feat. Sabi)', 'Cobra Starship', 'YouMakeMeFeel', 767698327, 55819, 'fff2258f', 'YouMakeMeFeel'),
    ('2dc1fb5e', "You're The First, The Last, My Everything", 'Barry White', 'YouReTheFirst', 767687518, 53739, 'fff1fb55', 'YouReTheFirst'),
)


def catalog():
    return [(title, artist, key) for key, title, artist, *_ in SONGS]


def prepare_previews(disc, destination, jobs):
    aliases = {key: (f"{audio}.wav", f"{video}_WII.mo")
               for key, _, _, video, _, _, _, audio in SONGS}
    prepare_lyn_previews(disc, destination, aliases, "JD4.bf", jobs, movie_extension="mo")
