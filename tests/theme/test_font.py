import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
spec = importlib.util.spec_from_file_location('theme_pack', ROOT / 'tools/theme_pack.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
import theme_font as font_tool
import theme_native as native


def simple(contours):
    """A glyf entry from contours of (x, y, on-curve) points."""
    points = [p for contour in contours for p in contour]
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    data = struct.pack('>hhhhh', len(contours), min(xs), min(ys), max(xs), max(ys))
    end = -1
    for contour in contours:
        end += len(contour)
        data += struct.pack('>H', end)
    data += struct.pack('>H', 0) + bytes(int(on) for _, _, on in points)
    for axis in (0, 1):
        previous = 0
        for p in points:
            data += struct.pack('>h', p[axis] - previous)
            previous = p[axis]
    return data


def truetype(glyphs, cmap, advances, units=1000, range_offset=0):
    """A minimal .ttf: the tables theme_font reads, nothing else.
    `range_offset` goes in every segment's idRangeOffset but the last."""
    glyf, loca = b'', []
    for g in glyphs:
        loca.append(len(glyf))
        glyf += g + b'\0' * (len(g) % 2)
    loca.append(len(glyf))
    codes = sorted(cmap)
    segments = [(c, c, (cmap[c] - c) & 0xffff) for c in codes] + [(0xffff, 0xffff, 1)]
    n = len(segments)
    sub = struct.pack('>HHHHHHH', 4, 16 + 8 * n, 0, 2 * n, 0, 0, 0)
    sub += struct.pack('>%dH' % n, *[s[1] for s in segments]) + b'\0\0'
    sub += struct.pack('>%dH' % n, *[s[0] for s in segments])
    sub += struct.pack('>%dH' % n, *[s[2] for s in segments])
    sub += struct.pack('>%dH' % n, *([range_offset] * (n - 1) + [0]))
    tables = {
        'head': struct.pack('>IIIIHHqqhhhhHHhhh', 0x10000, 0, 0, 0x5f0f3cf5, 0, units, 0, 0, 0, 0, 0, 0, 0, 8, 2, 1, 0),
        'hhea': struct.pack('>IhhhH11hH', 0x10000, 800, -200, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, len(advances)),
        'maxp': struct.pack('>IH', 0x5000, len(glyphs)),
        'cmap': struct.pack('>HHHHI', 0, 1, 3, 1, 12) + sub,
        'hmtx': b''.join(struct.pack('>Hh', a, 0) for a in advances),
        'loca': struct.pack('>%dI' % len(loca), *loca),
        'glyf': glyf,
    }
    data = struct.pack('>IHHHH', 0x10000, len(tables), 0, 0, 0)
    offset = 12 + 16 * len(tables)
    body = b''
    for tag, table in tables.items():
        data += struct.pack('>4sIII', tag.encode(), 0, offset + len(body), len(table))
        body += table + b'\0' * (-len(table) % 4)
    return data + body


SQUARE = [[(100, 0, True), (100, 800, True), (500, 800, True), (500, 0, True)]]
# 'B' is the square moved right by half a pixel at 10 pixels per em.
SHIFTED = struct.pack('>hhhhhHHhh', -1, 150, 0, 550, 800, 0x3, 1, 50, 0)
ROUND = [[(300, 0, True), (100, 0, False), (100, 400, True), (100, 800, False), (300, 800, True),
          (500, 800, False), (500, 400, True), (500, 0, False)]]
# '-' is a bar 3 pixels wide on the fifth row at 10 pixels per em.
DASH = [[(100, 300, True), (100, 400, True), (400, 400, True), (400, 300, True)]]
FONT = truetype([b'', simple(SQUARE), SHIFTED, b'', simple(ROUND), simple(DASH)],
                {65: 1, 66: 2, 32: 3, 67: 4, 63: 4, 45: 5}, [500, 600, 600, 300, 600, 500])


def low(top):
    """A font whose 'A' and '-' lie below the baseline, the top of 'A' at `top`."""
    return truetype([b'', simple([[(100, -800, True), (100, top, True), (500, top, True), (500, -800, True)]]),
                     simple([[(100, -400, True), (100, -300, True), (400, -300, True), (400, -400, True)]])],
                    {65: 1, 45: 2}, [500, 600, 500])


class FontTests(unittest.TestCase):
    def test_known_glyphs(self):
        data = font_tool.compile_font(FONT, 10, ' AB')
        # Line height 8 (the square's ink), baseline 8, ' ' to 'B', '-' as fallback.
        self.assertEqual(data[:12], b'STF1' + bytes([8, 8, 32, 35, 45, 0, 0, 0]))
        records = data[12:12 + 35 * 8]
        self.assertEqual(records[:8], struct.pack('<HBBBBBx', 0, 3, 0, 0, 0, 0))   # a space
        self.assertEqual(records[8:13 * 8], bytes(12 * 8))                          # no '!' to ','
        self.assertEqual(records[13 * 8:14 * 8], struct.pack('<HBBBBBx', 0, 5, 1, 4, 3, 1))
        self.assertEqual(records[14 * 8:33 * 8], bytes(19 * 8))                     # no '.' to '@'
        self.assertEqual(records[33 * 8:34 * 8], struct.pack('<HBBBBBx', 2, 6, 1, 0, 4, 8))
        self.assertEqual(records[34 * 8:], struct.pack('<HBBBBBx', 18, 6, 1, 0, 5, 8))
        # Whole pixels are fully covered; the shifted edges cover half.
        self.assertEqual(data[12 + 35 * 8:], b'\xff\xf0' + b'\xff\xff' * 8 + b'\x8f\xff\x80' * 8)

    def test_dash_for_a_missing_value(self):
        # The firmware draws {HH}:{MM} as '--:--' before the clock is set, so
        # every font keeps '-' and, unless '?' is kept, falls back to it, not
        # to the first glyph kept (#31 review: Bebas Clock showed 00:00).
        data = font_tool.compile_font(FONT, 10, 'AB')
        self.assertEqual((data[6], data[8]), (ord('-'), ord('-')))
        self.assertEqual(data[12 + 2], 5)   # the advance of '-'
        self.assertEqual(font_tool.compile_font(FONT, 10, 'AB-'), data)
        self.assertEqual(font_tool.compile_font(FONT, 10, 'AB?')[8], ord('?'))
        self.assertEqual(font_tool.compile_font(FONT, 10, 'AB', 'A')[8], ord('A'))
        without = truetype([b'', simple(SQUARE)], {65: 1}, [500, 600])
        with self.assertRaisesRegex(ValueError, "no glyph for '-', which the firmware draws"):
            font_tool.compile_font(without, 10, 'A')

    def test_rejects_no_ink_above_the_baseline(self):
        # The firmware refuses a baseline of 0, and a negative one did not
        # fit in a byte (#31 review): the packer says why instead.
        for top, size, ascent in [(0, 10, 0), (0, 100, 0), (-200, 10, -2)]:
            with self.assertRaisesRegex(ValueError, f'ascent {ascent} px: no glyph kept reaches above the baseline'):
                font_tool.compile_font(low(top), size, 'A')
        # One pixel above it is enough.
        self.assertEqual(font_tool.compile_font(low(100), 10, 'A')[5], 1)

    def test_stable_bytes(self):
        first = font_tool.compile_font(FONT, 37, 'ABC ?')
        self.assertEqual(first, font_tool.compile_font(FONT, 37, 'ABC ?'))
        self.assertEqual(first[8], ord('?'))
        self.assertEqual(font_tool.compile_font(FONT, 37, 'ABC ?', 'A')[8], ord('A'))

    def test_curves_stay_inside_their_advance(self):
        data = font_tool.compile_font(FONT, 40, 'C')
        height, first, count = data[4], data[6], data[7]
        offset, advance, left, top, width, rows = struct.unpack_from('<HBBBBBx', data, 12 + 8 * (ord('C') - first))
        self.assertTrue(0 < width and left + width <= advance and top + rows <= height)
        bitmap = data[12 + 8 * count + offset:]   # 'C' is the last glyph
        self.assertEqual(len(bitmap), rows * ((width + 1) // 2))
        # Antialiased: the edge has partial coverage, the middle is solid.
        levels = {b >> 4 for b in bitmap} | {b & 15 for b in bitmap}
        self.assertIn(15, levels)
        self.assertTrue(levels - {0, 15})

    def test_rejects(self):
        for args in [(FONT, 10, 'Z'), (FONT, 10, 'A\n'), (FONT, 10, ''), (FONT, 3, 'A'), (FONT, 10, 'A', 'B'),
                     (FONT, 200, 'A'), (b'OTTO' + FONT[4:], 10, 'A'), (FONT[:100], 10, 'A')]:
            with self.assertRaises(ValueError, msg=repr(args[1:])):
                font_tool.compile_font(*args)

    def test_malformed_character_map(self):
        # An idRangeOffset past the end of the cmap table: a message, not a
        # struct.error traceback from the packer (#69 review).
        broken = truetype([b'', simple(SQUARE)], {45: 1, 65: 1}, [500, 600], range_offset=0xfff0)
        with self.assertRaisesRegex(ValueError, 'malformed character map'):
            font_tool.compile_font(broken, 10, 'A')

    def theme(self, root, spec, settings, value='AB'):
        manifest = {'spec': spec, 'theme': {'id': 'test', 'name': 'Test', 'author': 'Me', 'version': '1'},
                    'display': {'width': 240, 'height': 240, 'background': '#000000'},
                    'layers': [{'id': 't', 'type': 'text', 'x': 0, 'y': 0, 'value': value, 'font': 'f', 'color': '#ffffff'}]}
        (root/'theme.json').write_text(json.dumps(manifest))
        (root/'fonts').mkdir(exist_ok=True)
        (root/'fonts/f.ttf').write_bytes(FONT)
        if settings is not None:
            (root/'fonts/f.json').write_text(json.dumps(settings))

    def test_package(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self.theme(root, 2, {'file': 'f.ttf', 'size': 20, 'glyphs': 'AB'})
            data = pack.build(root)
            self.assertEqual(data, pack.build(root))
            stf = font_tool.compile_font(FONT, 20, 'AB')
            self.assertIn(b'fonts/f.stf' + stf, data)
            package = root/'test.stheme'
            package.write_bytes(data)
            self.assertEqual(native.validate_package(package)['id'], 'test')

    def test_package_checks_the_static_text(self):
        # Text outside {tokens} must have its glyphs; a token's value may
        # fall back.
        for value, error in [('AB', None), ('{HH}-{MM}', None), ('{HH}A', None), ('AC', "'C'"), ('DC{HH}', "'CD'")]:
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                self.theme(root, 2, {'file': 'f.ttf', 'size': 20, 'glyphs': 'AB'}, value)
                if error is None:
                    pack.build(root)
                    continue
                with self.assertRaisesRegex(ValueError, r'layers\[0\]\.value: font f has no glyph for ' + error):
                    pack.build(root)

    def test_package_rejects(self):
        for spec, settings in [(1, {'file': 'f.ttf', 'size': 20, 'glyphs': 'AB'}), (2, None),
                               (2, {'file': 'f.ttf', 'size': 20}), (2, {'file': 'f.ttf', 'size': 20, 'glyphs': 'AB', 'x': 1}),
                               (2, {'file': '../f.ttf', 'size': 20, 'glyphs': 'AB'}), (2, {'file': 'f.ttf', 'size': '20', 'glyphs': 'AB'})]:
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                self.theme(root, spec, settings)
                with self.assertRaises(ValueError, msg=repr((spec, settings))):
                    pack.build(root)


if __name__ == '__main__':
    unittest.main()
