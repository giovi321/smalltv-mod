"""Chalk grain for a packaged font: fonts/<name>.json may carry
"grain": {"seed", "density", "strength"}, which thins some pixels of every
glyph when the font is compiled. It is cosmetic and happens in the packer: the
output is an ordinary STF. The grain only lowers coverage inside the ink box
the plain font has, so no glyph moves, widens or changes its advance; with no
"grain" the bytes are exactly the plain font's. The whole grain step uses
integers only, so it gives the same bytes on every platform and Python.
"""
import ast
from fractions import Fraction
import hashlib
import inspect
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest

from test_font import DASH, FONT, ROOT, SQUARE, pack, font_tool, native, simple, truetype


def hash32(*values):
    """The rule the packer must follow, written out here on its own."""
    x = 0x9E3779B9
    for v in values:
        x ^= (v + 0x7F4A7C15 + (x << 6) + (x >> 2)) & 0xFFFFFFFF
        x = (x * 0x85EBCA6B) & 0xFFFFFFFF
        x ^= x >> 13
        x = (x * 0xC2B2AE35) & 0xFFFFFFFF
        x ^= x >> 16
    return x


def nibbles(data, records_end):
    out = []
    for b in data[records_end:]:
        out += [b >> 4, b & 15]
    return out


def records_end(data):
    return 12 + data[7] * 8


def glyph_rows(data):
    """{code: [row of levels, ...]} read back from compiled bytes."""
    first, count = data[6], data[7]
    base = 12 + count * 8
    glyphs = {}
    for i in range(count):
        offset, advance, left, top, width, rows = struct.unpack_from('<HBBBBBx', data, 12 + i * 8)
        if not width:
            continue
        stride = (width + 1) // 2
        picture = []
        for r in range(rows):
            line = data[base + offset + r * stride: base + offset + (r + 1) * stride]
            row = []
            for b in line:
                row += [b >> 4, b & 15]
            picture.append(row[:width])
        glyphs[first + i] = picture
    return glyphs


GRAIN = {'seed': 7, 'density': 50, 'strength': 80}
# A is a tall square and B a half-height one that stands on the baseline.
# Every packed font keeps '-' (the firmware draws it for a value it lacks), so
# the font has one; it is lower than A, so it does not change the line box.
HALF = [[(100, 0, True), (100, 400, True), (500, 400, True), (500, 0, True)]]
SMALL = truetype([b'', simple(SQUARE), simple(HALF), simple(DASH)], {65: 1, 66: 2, 45: 3}, [500, 600, 600, 500])

BEBAS = ROOT/'examples/themes/bebas-clock/fonts/BebasNeue-Regular.ttf'


def reference_level(grain, level, code, x, y):
    """The level (0-15) of a pixel of plain level `level` after the grain, in
    exact fractions: with probability density/100 (hash salt 1) it loses
    strength/100 * (0.5 + 0.5 * h) of its coverage (h from hash salt 2, in
    [0, 1)); the result is rounded half up."""
    if hash32(grain['seed'], code, x, y, 1) * 100 < grain['density'] * 4294967296:
        h = Fraction(hash32(grain['seed'], code, x, y, 2), 4294967296)
        level = level * (1 - Fraction(grain['strength'], 100) * (Fraction(1, 2) + h / 2))
        return math.floor(level + Fraction(1, 2))
    return level


def function_source(name):
    tree = ast.parse(Path(font_tool.__file__).read_text(encoding='utf-8'))
    return next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == name)


class GrainTests(unittest.TestCase):
    def plain(self, size=20, glyphs='ABC '):
        return font_tool.compile_font(FONT, size, glyphs)

    def grained(self, grain=GRAIN, size=20, glyphs='ABC '):
        return font_tool.compile_font(FONT, size, glyphs, grain=grain)

    def test_off_is_the_plain_font(self):
        self.assertEqual(self.grained(None), self.plain())
        self.assertEqual(font_tool.compile_font(FONT, 20, 'ABC ', None, None), self.plain())

    def test_off_is_the_font_main_builds(self):
        # The digests come from the packer BEFORE the grain existed (main at
        # 3.7.0): the two fonts of the Bebas Clock example, without a grain.
        data = BEBAS.read_bytes()
        self.assertEqual(hashlib.sha256(font_tool.compile_font(data, 80, '0123456789:')).hexdigest(),
                         'f858e83e5a1e3430dbda98b76c21419021bdbef969a7b7c6d24ed4214f900d87')
        self.assertEqual(hashlib.sha256(font_tool.compile_font(data, 30, ' 0123456789ADFJMNOSTWabcdeghilnoprtuvy?')).hexdigest(),
                         '159ad0b77c36c9bacf208c6e6f1274d939ecaf6f214223e264b839df746fed5d')

    def test_on_is_stable_and_differs(self):
        first = self.grained()
        self.assertEqual(first, self.grained())
        self.assertNotEqual(first, self.plain())
        self.assertEqual(len(first), len(self.plain()))
        self.assertNotEqual(first, self.grained({**GRAIN, 'seed': 8}))
        self.assertNotEqual(first, self.grained({**GRAIN, 'density': 60}))
        self.assertNotEqual(first, self.grained({**GRAIN, 'strength': 90}))

    def test_hash_is_pinned(self):
        # Fixed inputs and outputs: the hash may never change by accident.
        for values, want in (((0,), 2225714928), ((1,), 3832156839), ((0, 0, 0, 0, 1), 3445235507),
                             ((7, 65, 3, 4, 1), 2520813873), ((7, 65, 3, 4, 2), 3247427226),
                             ((65535, 126, 200, 200, 1), 3287432116), ((65535, 126, 200, 200, 2), 2523892935),
                             ((123, 48, 0, 0, 1), 4219899682), ((1, 2, 3, 4, 5, 6), 3686672058)):
            self.assertEqual(font_tool.hash32(*values), want, values)
            self.assertEqual(hash32(*values), want, values)

    def test_grain_step_uses_integers_only(self):
        # No float constant, no true division and no float() anywhere in the
        # grain functions: their result cannot depend on the platform.
        for name in ('hash32', 'check_grain', 'grained'):
            for node in ast.walk(function_source(name)):
                if isinstance(node, ast.Constant):
                    self.assertNotIsInstance(node.value, float, name)
                if isinstance(node, ast.BinOp):
                    self.assertNotIsInstance(node.op, ast.Div, name)
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                    self.assertNotIn(node.func.id, ('float', 'round'), name)
        self.assertIsInstance(font_tool.grained(15, GRAIN, 65, 3, 4), int)

    def test_header_and_records_are_the_plain_fonts(self):
        plain, grained = self.plain(), self.grained()
        self.assertEqual(grained[:records_end(plain)], plain[:records_end(plain)])

    def test_strongest_grain_keeps_ink_box_and_advances(self):
        # Strength 100 and density 100 can wipe a whole edge row or column to
        # zero. The header and the records (ink box, advances) stay the plain
        # font's anyway, and the packaged font still passes the validator.
        grain = {'seed': 5, 'density': 100, 'strength': 100}
        for ttf, size, glyphs in ((FONT, 10, 'ABC '), (FONT, 8, 'AB'), (SMALL, 10, 'AB')):
            plain, grained = font_tool.compile_font(ttf, size, glyphs), font_tool.compile_font(ttf, size, glyphs, grain=grain)
            self.assertEqual(grained[:records_end(plain)], plain[:records_end(plain)])
            self.assertEqual(len(grained), len(plain))
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self.theme(root, {'file': 'f.ttf', 'size': 8, 'glyphs': 'AB', 'grain': grain})
            package = root/'test.stheme'
            package.write_bytes(pack.build(root))
            self.assertEqual(native.validate_package(package)['id'], 'test')

    def test_coverage_only_goes_down(self):
        plain, grained = self.plain(), self.grained()
        p, g = nibbles(plain, records_end(plain)), nibbles(grained, records_end(grained))
        self.assertEqual(len(p), len(g))
        self.assertTrue(all(b <= a for a, b in zip(p, g)))
        self.assertTrue(any(b < a for a, b in zip(p, g)))

    def test_follows_the_rule_pixel_by_pixel(self):
        # Every pixel of the plain font has a known result: the rule written
        # out in reference_level, in exact fractions. x, y count in the glyph's
        # raster before it is trimmed, so a glyph whose ink starts lower (B in
        # SMALL, 4 rows down) uses y = 4 for its first row, not 0.
        for grain in (GRAIN, {'seed': 0, 'density': 100, 'strength': 100}, {'seed': 65535, 'density': 1, 'strength': 1},
                      {'seed': 123, 'density': 100, 'strength': 40}):
            for ttf, glyphs, size in ((FONT, 'A', 10), (SMALL, 'AB', 10), (FONT, 'A', 14)):
                plain = font_tool.compile_font(ttf, size, glyphs)
                grained = font_tool.compile_font(ttf, size, glyphs, grain=grain)
                plain_rows, grain_rows = glyph_rows(plain), glyph_rows(grained)
                self.assertEqual(plain_rows.keys(), grain_rows.keys())
                first = plain[6]
                pixels = solid = 0
                for code, picture in plain_rows.items():
                    top = struct.unpack_from('<HBBBBBx', plain, 12 + (code - first) * 8)[3]
                    for y, row in enumerate(picture):
                        for x, level in enumerate(row):
                            solid += level == 15
                            want = reference_level(grain, level, code, x, y + top)
                            self.assertEqual(grain_rows[code][y][x], want, (grain, code, x, y, level))
                self.assertGreater(solid, 10)
        # B sits 4 rows down in SMALL, so a rule on the trimmed picture would differ.
        small = font_tool.compile_font(SMALL, 10, 'AB')
        self.assertEqual(small[6], ord('-'))
        self.assertEqual(struct.unpack_from('<HBBBBBx', small, 12 + (ord('B') - small[6]) * 8)[3], 4)

    def test_dimming_amount_is_between_half_and_all_of_strength(self):
        # strength 100, density 100: every pixel keeps at most half of its coverage.
        plain, grained = self.plain(40, 'A'), self.grained({'seed': 3, 'density': 100, 'strength': 100}, 40, 'A')
        for a, b in zip(nibbles(plain, records_end(plain)), nibbles(grained, records_end(grained))):
            self.assertLessEqual(b, (a + 1) // 2)
        # strength 10 takes at most a tenth: a solid 15 never falls under 13.
        light = self.grained({'seed': 3, 'density': 100, 'strength': 10}, 40, 'A')
        for a, b in zip(nibbles(plain, records_end(plain)), nibbles(light, records_end(light))):
            self.assertGreaterEqual(b, math.floor(a * 0.9 + 0.5) - 1)

    def test_density_selects_about_that_share(self):
        plain = self.plain(60, 'AB')
        base = [n for n in nibbles(plain, records_end(plain))]
        solid = [i for i, n in enumerate(base) if n == 15]
        self.assertGreater(len(solid), 400)
        for density in (10, 50, 90):
            out = self.grained({'seed': 1, 'density': density, 'strength': 100}, 60, 'AB')
            got = nibbles(out, records_end(out))
            hit = sum(1 for i in solid if got[i] < 15)
            self.assertAlmostEqual(hit / len(solid), density / 100, delta=0.08, msg=str(density))

    def test_pinned_bytes(self):
        # A fingerprint of one grained font, so a change to the hash or the
        # rule is a visible decision. The loss step is integer arithmetic, so
        # the digest is the same on every platform.
        digest = hashlib.sha256(self.grained(GRAIN, 40, 'ABC ?')).hexdigest()
        self.assertEqual(digest, PINNED)

    def test_rejects_and_names_the_field(self):
        seed, density, strength = 'seed', 'density', 'strength'
        shape = 'grain: expected "seed", "density" and "strength"'
        cases = [([], shape), ('chalk', shape), ({}, shape), ({'seed': 1, 'density': 50}, shape),
                 ({'seed': 1, 'density': 50, 'strength': 50, 'x': 1}, shape), (5, shape)]   # None is "no grain" here; the package refuses a null
        for key, low, high in ((seed, 0, 65535), (density, 1, 100), (strength, 1, 100)):
            message = f'grain.{key}: expected an integer from {low} to {high}'
            for bad in (True, 1.0, 50.5, '1', None, [1], low - 1, high + 1):
                cases.append(({'seed': 1, 'density': 50, 'strength': 50, key: bad}, message))
        for grain, message in cases:
            with self.assertRaises(ValueError, msg=repr(grain)) as raised:
                font_tool.compile_font(FONT, 20, 'AB', grain=grain)
            self.assertEqual(str(raised.exception), message, repr(grain))
        # The edges are allowed.
        for grain in ({'seed': 0, 'density': 1, 'strength': 1}, {'seed': 65535, 'density': 100, 'strength': 100}):
            font_tool.compile_font(FONT, 20, 'AB', grain=grain)

    def theme(self, root, settings):
        manifest = {'spec': 2, 'theme': {'id': 'test', 'name': 'Test', 'author': 'Me', 'version': '1'},
                    'display': {'width': 240, 'height': 240, 'background': '#000000'},
                    'layers': [{'id': 't', 'type': 'text', 'x': 0, 'y': 0, 'value': 'AB', 'font': 'f', 'color': '#ffffff'}]}
        (root/'theme.json').write_text(json.dumps(manifest))
        (root/'fonts').mkdir(exist_ok=True)
        (root/'fonts/f.ttf').write_bytes(FONT)
        (root/'fonts/f.json').write_text(json.dumps(settings))

    def test_package_takes_the_key(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            base = {'file': 'f.ttf', 'size': 20, 'glyphs': 'AB'}
            self.theme(root, {**base, 'grain': GRAIN})
            data = pack.build(root)
            self.assertEqual(data, pack.build(root))
            stf = font_tool.compile_font(FONT, 20, 'AB', grain=GRAIN)
            self.assertIn(b'fonts/f.stf' + stf, data)
            self.assertNotIn(b'fonts/f.stf' + font_tool.compile_font(FONT, 20, 'AB'), data)
            package = root/'test.stheme'
            package.write_bytes(data)
            self.assertEqual(native.validate_package(package)['id'], 'test')
            # Without the key the package is the plain font's.
            self.theme(root, base)
            self.assertIn(b'fonts/f.stf' + font_tool.compile_font(FONT, 20, 'AB'), pack.build(root))

    def test_package_rejects_a_bad_grain_and_names_file_and_field(self):
        shape = 'fonts/f.json: grain: expected "seed", "density" and "strength"'
        for grain, message in (({'seed': 1, 'density': 0, 'strength': 50}, 'fonts/f.json: grain.density: expected an integer from 1 to 100'),
                               ({'seed': 1.5, 'density': 5, 'strength': 50}, 'fonts/f.json: grain.seed: expected an integer from 0 to 65535'),
                               ({'seed': 1}, shape), (5, shape), (None, shape)):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                self.theme(root, {'file': 'f.ttf', 'size': 20, 'glyphs': 'AB', 'grain': grain})
                with self.assertRaises(ValueError, msg=repr(grain)) as raised:
                    pack.build(root)
                # The packer puts the layer and font in front ("layers[0].font: f: ...").
                self.assertTrue(str(raised.exception).endswith(': ' + message), str(raised.exception))


PINNED = '8b9546621c229493add55f3828c6b9acc956f391b8e302c516c0c107dca29491'

if __name__ == '__main__':
    unittest.main()
