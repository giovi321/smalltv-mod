"""Review tests for the font packer (tools/theme_font.py, theme_pack.py): what
tests/theme/test_font.py does not cover. A failing test here is a gap or a bug
in the code under review, named in its docstring."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_font as base

font_tool = base.font_tool
pack = base.pack
native = base.native


def rect(x0, y0, x1, y1):
    return [[(x0, y0, True), (x0, y1, True), (x1, y1, True), (x1, y0, True)]]


# '_' lies wholly below the baseline, 'T' touches the baseline from above, '.' sits on it.
LOW = base.truetype([b'', base.simple(rect(100, -200, 500, -100)), base.simple(rect(100, 0, 500, 800)),
                     base.simple(rect(100, -200, 300, 0))],
                    {95: 1, 84: 2, 46: 3, 32: 3}, [600, 600, 600, 300])


def firmware_accepts(stf):
    """The header rules of Package::checkFont() that the packer can break."""
    height, baseline, first, count, fallback = stf[4], stf[5], stf[6], stf[7], stf[8]
    return 6 <= height <= 96 and 0 < baseline <= height and 32 <= first and count and first + count <= 127 \
        and first <= fallback < first + count


class PackerEdgeTests(unittest.TestCase):
    def test_glyphs_below_the_baseline_give_a_font_the_device_accepts_or_a_clear_error(self):
        """tools/theme_font.py:296: the baseline is the highest ink, which is
        0 or negative when every kept glyph hangs below the baseline."""
        for glyphs in ('_', '.', '_.'):
            try:
                stf = font_tool.compile_font(LOW, 100, glyphs)
            except ValueError as error:
                self.assertNotIn('bytes must be in range', str(error), glyphs)
                continue
            self.assertTrue(firmware_accepts(stf), f'{glyphs!r}: header {stf[:12]!r}')

    def test_baseline_zero_font_validates_as_a_package(self):
        """'.' ends exactly on the baseline (ascent 0). The packer writes the
        font; the firmware's check refuses a baseline of 0, so the package
        would not install."""
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            manifest = {'spec': 2, 'theme': {'id': 'low', 'name': 'Low', 'author': 'Me', 'version': '1'},
                        'display': {'width': 240, 'height': 240, 'background': '#000000'},
                        'layers': [{'id': 't', 'type': 'text', 'x': 0, 'y': 0, 'value': '.', 'font': 'f', 'color': '#ffffff'}]}
            (root/'theme.json').write_text(json.dumps(manifest))
            (root/'fonts').mkdir()
            (root/'fonts/f.ttf').write_bytes(LOW)
            (root/'fonts/f.json').write_text(json.dumps({'file': 'f.ttf', 'size': 100, 'glyphs': '.'}))
            data = pack.build(root)
            package = root/'low.stheme'
            package.write_bytes(data)
            self.assertEqual(native.validate_package(package)['id'], 'low')

    def pack_text(self, value, glyphs):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            manifest = {'spec': 2, 'theme': {'id': 'sub', 'name': 'Sub', 'author': 'Me', 'version': '1'},
                        'display': {'width': 240, 'height': 240, 'background': '#000000'},
                        'layers': [{'id': 't', 'type': 'text', 'x': 0, 'y': 0, 'value': value, 'font': 'f', 'color': '#ffffff'}]}
            (root/'theme.json').write_text(json.dumps(manifest))
            (root/'fonts').mkdir()
            (root/'fonts/f.ttf').write_bytes(base.FONT)
            (root/'fonts/f.json').write_text(json.dumps({'file': 'f.ttf', 'size': 20, 'glyphs': glyphs}))
            return pack.build(root)

    def test_static_text_outside_the_glyph_subset_is_reported(self):
        """tools/theme_pack.py build_font(): the packer does not compare the
        layer's static text with the kept glyphs, so 'AB' with only 'A'
        kept builds, and the device draws the fallback for 'B'."""
        with self.assertRaises(ValueError):
            self.pack_text('AB', 'A')


if __name__ == '__main__':
    unittest.main()
