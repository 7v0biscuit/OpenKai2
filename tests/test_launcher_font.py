# SPDX-License-Identifier: GPL-3.0-only
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def tables(path):
    data = path.read_bytes()
    count = struct.unpack_from('>H', data, 4)[0]
    result = {}
    for index in range(count):
        name, _, offset, length = struct.unpack_from('>4sIII', data, 12 + index * 16)
        result[name] = data[offset:offset + length]
    return result


class LauncherFont(unittest.TestCase):
    def test_display_font_preserves_lettering_and_uses_typographic_line_spacing(self):
        source = tables(ROOT / 'third_party/bangers/Bangers-Regular.ttf')
        display = tables(ROOT / 'third_party/openkai2-display/OpenKai2Display-Regular.ttf')
        for table in (b'glyf', b'cmap', b'hmtx'):
            self.assertEqual(display[table], source[table], table)
        metrics = display[b'OS/2']
        ascent, descent = struct.unpack_from('>hh', metrics, 68)
        win_ascent, win_descent = struct.unpack_from('>HH', metrics, 74)
        self.assertEqual((win_ascent, win_descent), (ascent, -descent))
        self.assertTrue(struct.unpack_from('>H', metrics, 62)[0] & 0x80)
        self.assertIn('OpenKai2 Display'.encode('utf-16-be'), display[b'name'])
        self.assertEqual((ROOT / 'third_party/openkai2-display/OFL.txt').read_bytes(),
                         (ROOT / 'third_party/bangers/OFL.txt').read_bytes())
