from math import log10
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
import wave

from probe.loudness import GAMEPLAY_LUFS, PREVIEW_LUFS, measure, playback_gains, prepare_loudness


class LoudnessTest(unittest.TestCase):
    def test_gains_account_for_game_volume_and_preview_peaks(self):
        preview, gameplay = playback_gains("SD2Y41", -11.5, -2.4)
        self.assertAlmostEqual(-11.5 + 20 * log10(preview), PREVIEW_LUFS)
        self.assertAlmostEqual(-11.5 - 6.0 + 20 * log10(gameplay), GAMEPLAY_LUFS)
        preview, _ = playback_gains("SE3E41", -18.0, 0.0)
        self.assertAlmostEqual(20 * log10(preview), -1.0)

    @unittest.skipUnless(shutil.which("ffmpeg"), "needs FFmpeg")
    def test_preview_levels_update_when_the_clip_changes(self):
        song = ("Test song", "Test artist", "Map")
        collection = {"SD2Y41": ("Just Dance 2", [song])}
        with TemporaryDirectory() as root, patch("probe.loudness.measure", wraps=measure) as read:
            cache = Path(root)
            clip = cache / "previews/SD2Y41/map.wav"
            clip.parent.mkdir(parents=True)
            # Short square waves with different levels and file sizes.
            def write_clip(amplitude, cycles):
                with wave.open(str(clip), "wb") as file:
                    file.setparams((2, 2, 32000, 0, "NONE", "not compressed"))
                    cycle = b"".join(value.to_bytes(2, "little", signed=True) * 16 * 2
                                     for value in (amplitude, -amplitude))
                    file.writeframes(cycle * cycles)

            write_clip(7000, 1000)
            first = prepare_loudness(cache, collection, (), 1)["SD2Y41", song]
            self.assertEqual(prepare_loudness(cache, collection, (), 1)["SD2Y41", song], first)
            self.assertEqual(read.call_count, 1)
            write_clip(3500, 1050)
            second = prepare_loudness(cache, collection, (), 1)["SD2Y41", song]
            self.assertEqual(read.call_count, 2)
            self.assertGreater(second[1], first[1])
