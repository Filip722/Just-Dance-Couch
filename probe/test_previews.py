import struct
import unittest
from unittest.mock import patch

from probe.previews import decode_preview


class PreviewTest(unittest.TestCase):
    def test_reads_audio_after_optional_wii_chunks(self):
        data = bytearray(320)
        data[:16] = b"RAKI\0\0\0\x09Wii adpc"
        chunks = (
            (b"fmt ", 104, 18), (b"dspL", 122, 96), (b"dspR", 218, 96),
            (b"MARK", 314, 1), (b"STRG", 315, 1), (b"datS", 316, 4),
        )
        struct.pack_into(">I", data, 24, len(chunks))
        for index, chunk in enumerate(chunks):
            struct.pack_into(">4sII", data, 32 + 12 * index, *chunk)
        struct.pack_into(">HHI", data, 104, 2, 2, 32000)
        data[316:] = b"test"

        with patch("probe.previews.decode_dsp_clip") as decode:
            rate, _ = decode_preview(data, 3)
            self.assertEqual(rate, 32000)
            self.assertEqual(bytes(decode.call_args.args[0]), b"test")
