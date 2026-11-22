#include <string.h>

#include "song_grid.h"

// Disc song descriptors with their own local audio, from SJOE41 and SE3E41.
const Song songs[] = {
  {"99 Luftballons", "Rutschen Planeten", "Luftballons"},
  {"Applause", "Lady Gaga", "LimaGolf1"},
  {"Aquarius/Let The Sunshine In", "The Sunlight Shakers", "Aquarius"},
  {"Blame It On The Boogie", "Mick Jackson", "BlameIt"},
  {"Blurred Lines", "Robin Thicke Ft. Pharrell Williams", "BlurredLines"},
  {"Candy", "Robbie Williams", "Candy"},
  {"Careless Whisper", "George Michael", "CarelessWhisper"},
  {"C'mon", "Ke$ha", "Cmon"},
  {"Could You Be Loved", "Bob Marley", "CouldYouBeLoved"},
  {"Dançando", "Ivete Sangalo", "Dancando"},
  {"Feel So Right", "Imposs Ft. Konshens", "FeelSoRight"},
  {"Feel This Moment", "Pitbull Ft. Christina Aguilera", "FeelThisMoment"},
  {"Fine China", "Chris Brown", "FineChina"},
  {"Flashdance... What A Feeling", "The Girly Team", "WhatAFeeling"},
  {"Follow the Leader", "Wisin & Yandel Ft. Jennifer Lopez", "FollowTheLeader"},
  {"Gentleman", "PSY", "Gentleman"},
  {"Get Lucky", "Daft Punk", "GetLucky"},
  {"Ghostbusters", "Ray Parker Jr.", "Ghostbusters"},
  {"Gimme! Gimme! Gimme! (A Man After Midnight)", "ABBA", "GimmeGimme"},
  {"I Kissed a Girl", "Katy Perry", "IKissed"},
  {"I Will Survive", "Gloria Gaynor", "IWillSurvive"},
  {"In The Summertime", "Mungo Jerry", "Summertime"},
  {"Isidora", "Bog Bog Orkestar", "Zagreb"},
  {"It's You", "Duck Sauce", "ItsYou"},
  {"Just A Gigolo", "Louis Prima", "Gigolo"},
  {"Just Dance", "Lady Gaga", "JustDance"},
  {"Kiss You", "One Direction", "KissYou"},
  {"Limbo", "Daddy Yankee", "Limbo"},
  {"Love Boat", "Frankie Bostello", "LoveBoat"},
  {"Maria", "Ricky Martin", "Maria"},
  {"Miss Understood", "Sammie", "Misunderstood"},
  {"Moskau", "Dancing Bros.", "Moskau"},
  {"Nitro Bot", "Sentai Express", "RobotRock"},
  {"Pound The Alarm", "Nicki Minaj", "PoundTheAlarm"},
  {"Prince Ali", "Disney's Aladdin", "PrinceAli"},
  {"Rich Girl", "Gwen Stefani Ft. Eve", "RichGirl"},
  {"Safe And Sound", "Capital Cities", "SafeAndSound"},
  {"She Wolf (Falling to Pieces)", "David Guetta Ft. SIA", "SheWolf"},
  {"Starships", "Nicki Minaj", "Starships"},
  {"#thatPOWER", "will.i.am Ft. Justin Bieber", "ThatPower"},
  {"The Other Side", "Jason Derulo", "OtherSide"},
  {"The Way", "Ariana Grande Ft. Mac Miller", "TheWay"},
  {"Troublemaker", "Olly Murs Ft. Flo Rida", "Troublemaker"},
  {"Turn up the Love", "Far East Movement Ft. Cover Drive", "TurnUpTheLove"},
  {"Waking Up In Vegas", "Katy Perry", "WakingUp"},
  {"Where Have You Been", "Rihanna", "WhereHaveYou"},
  {"Wild", "Jessie J Ft. Big Sean", "Wild"},
  {"Y.M.C.A.", "Village People", "YMCA"},
};

static const Song songs_2015[] = {
  {"4x4", "Miley Cyrus", "4x4"},
  {"Addicted To You", "Avicii", "AddictedToYou"},
  {"Ain't No Mountain High Enough", "Marvin Gaye and Tammi Terrell", "NoMountain"},
  {"Bad Romance", "Lady Gaga", "BadRomance"},
  {"Bailando", "Enrique Iglesias Ft. Descemer Bueno & Gente de Zona", "Bailando"},
  {"Bang Bang", "Jessie J, Ariana Grande and Nicki Minaj", "Balance"},
  {"Best Song Ever", "One Direction", "BestSongEver"},
  {"Birthday", "Katy Perry", "Birthday"},
  {"Black Widow", "Iggy Azalea Ft. Rita Ora", "BlackWidow"},
  {"Built For This", "Becky G", "BuiltForThis"},
  {"Burn", "Ellie Goulding", "Burn"},
  {"Dark Horse", "Katy Perry", "DarkHorse"},
  {"Diamonds", "Rihanna", "Diamonds"},
  {"Don't Worry Be Happy", "The Bench Men", "DontWorry"},
  {"Epic Sirtaki", "The Bouzouki's", "Sirtaki"},
  {"Fatima", "Cheb Salama", "Fatima"},
  {"Get Low", "Dillon Francis & DJ Snake", "GetLow"},
  {"Happy", "Pharrell Williams", "Happy"},
  {"Holding Out For A Hero", "Bonnie Tyler", "HoldingOut"},
  {"I Love It", "Icona Pop Ft. Charli XCX", "ILoveIt"},
  {"It's My Birthday", "will.i.am Ft. Cody Wise", "ItsMyBirthday"},
  {"Kiss Kiss", "Prince Royce", "KissKiss"},
  {"Let It Go", "Disney's Frozen", "LetItGo"},
  {"Love Is All", "The Sunlight Shakers", "LoveIsAll"},
  {"Love Me Again", "John Newman", "LoveMeAgain"},
  {"Macarena", "The Girly Team", "Macarena"},
  {"Mahna Mahna", "Frankie Bostello", "MahNa"},
  {"Maps", "Maroon 5", "Mad"},
  {"Me And My Broken Heart", "Rixton", "BrokenHeart"},
  {"Never Can Say Goodbye", "Gloria Gaynor", "NeverCanSay"},
  {"Only You (And You Alone)", "Love Letter", "OnlyYou"},
  {"Problem", "Ariana Grande Ft. Iggy Azalea and Big Sean", "Problem"},
  {"She Looks So Perfect", "5 Seconds of Summer", "SoPerfect"},
  {"Speedy Gonzalez", "Los Pimientos Locos", "Speedy"},
  {"Summer", "Calvin Harris", "Summer"},
  {"Tetris", "Dancing Bros.", "Tetris"},
  {"The Fox (What Does the Fox Say?)", "Ylvis", "TheFox"},
  {"Till I Find You", "Austin Mahone", "FindYou"},
  {"Walk This Way", "Run-DMC & Aerosmith", "WalkThisWay"},
  {"XMas Tree", "Bollywood Santa", "BollywoodXmas"},
  {"You Spin Me Round (Like A Record)", "Dead Or Alive", "SpinMeRound"},
  {"You're On My Mind", "Imposs Ft. J. Perry", "OnMyMind"},
};

#include "collection_catalog.h"

// Wii release order, including Summer Party and Greatest Hits.
const Edition editions[] = {
  EDITION_SDNE41,
  EDITION_SD2Y41,
  EDITION_SJ9E41,
  EDITION_SJDE41,
  EDITION_SJHE41,
  EDITION_SJXE41,
  {"Just Dance 2014", "SJOE41", songs, sizeof songs / sizeof songs[0]},
  {"Just Dance 2015", "SE3E41", songs_2015, sizeof songs_2015 / sizeof songs_2015[0]},
  EDITION_SJNE41,
  EDITION_SZ7E41,
  EDITION_SE8E41,
  EDITION_S5EE41,
#ifdef EDITION_DLC
  EDITION_DLC,
#endif
};
const int edition_count = sizeof editions / sizeof editions[0];

const Edition *edition_for_disc(const char *disc) {
  for (int i = 0; i < edition_count; i++)
    if (editions[i].disc && !strcmp(editions[i].disc, disc)) return &editions[i];
  return NULL;
}
