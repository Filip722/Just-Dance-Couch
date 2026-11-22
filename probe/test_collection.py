from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from probe.collection import newest_dlc_songs, write_catalog


class CatalogTest(unittest.TestCase):
    def test_keeps_disc_songs_and_only_deduplicates_dlc(self):
        collection = {
            "SD2Y41": ("Just Dance 2", [("Rasputin", "Boney M.", "93801d03")]),
            "SJHE41": ("Just Dance Greatest Hits", [("Rasputin", "Boney M.", "76c06ae0")]),
            "SJXE41": ("Just Dance 4", []),
            "SJOE41": ("Just Dance 2014", [("Just Dance", "Lady Gaga", "JustDance")]),
            "SE3E41": ("Just Dance 2015", []),
        }
        dlc = [
            ("Gangnam Style", "PSY", "GangnamStyleDLC", "SE3E41", "jd2015", 2),
            ("Just Dance", "Lady Gaga Ft. Colby O'Donis", "JustDanceDLC", "SE3E41", "jd2015", 9),
            ("Gangnam Style", "Psy", "76c1b9c7", "SJXE41", "jd4", 7),
        ]

        with TemporaryDirectory() as staging:
            write_catalog(Path(staging), collection, dlc)
            catalog = (Path(staging) / "collection_catalog.h").read_text(encoding="utf-8")

        for key in ("93801d03", "76c06ae0", "JustDance", "JustDanceDLC", "GangnamStyleDLC"):
            self.assertIn(f'"{key}"', catalog)
        self.assertNotIn('"76c1b9c7"', catalog)

    def test_prefers_the_standard_dlc_routine_in_the_same_edition(self):
        standard = ("Let It Go", "Disney Frozen", "LetItGoDLC", "SE3E41", "jd2015", 23)
        alternate = ("Let It Go (Alternate)", "Disney Frozen", "Alternate", "SE3E41", "jd2015", 4)

        self.assertEqual(newest_dlc_songs([standard, alternate], ["SE3E41"]), [standard])
