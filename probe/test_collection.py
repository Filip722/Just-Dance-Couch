import unittest

from probe.collection import newest_songs


class NewestSongsTest(unittest.TestCase):
    def test_keeps_the_latest_source_across_disc_and_dlc(self):
        just_dance = ("Just Dance", "Lady Gaga", "JustDance")
        new_just_dance = (
            "Just Dance", "Lady Gaga Ft. Colby O'Donis", "JustDanceDLC", "SE3E41", "jd2015", 2,
        )
        old_gangnam = ("Gangnam Style", "Psy", "76c1b9c7", "SJXE41", "jd4", 7)
        new_gangnam = ("Gangnam Style", "PSY", "GangnamStyleDLC", "SE3E41", "jd2015", 4)
        summer = ("Summer", "Calvin Harris", "Summer")
        other_summer = ("Summer", "Another Artist", "OtherSummer")
        collection = {
            "SJXE41": ("Just Dance 4", [other_summer]),
            "SJOE41": ("Just Dance 2014", [just_dance]),
            "SE3E41": ("Just Dance 2015", [summer]),
        }

        discs, dlc = newest_songs(collection, [new_gangnam, new_just_dance, old_gangnam])

        self.assertEqual(discs["SJOE41"][1], [])
        self.assertEqual(discs["SJXE41"][1], [other_summer])
        self.assertEqual(discs["SE3E41"][1], [summer])
        self.assertEqual(dlc, [new_gangnam, new_just_dance])

    def test_prefers_the_standard_disc_routine_in_the_same_edition(self):
        disc_song = ("Let It Go", "Disney's Frozen", "LetItGo")
        dlc_song = ("''Let It Go''", "Disney Frozen", "LetItGoDLC", "SE3E41", "jd2015", 3)
        alternate = ("Let It Go (Alternate)", "Disney's Frozen", "Alternate", "SE3E41", "jd2015", 4)
        collection = {"SE3E41": ("Just Dance 2015", [disc_song])}

        self.assertEqual(newest_songs(collection, [dlc_song, alternate]), (collection, []))
