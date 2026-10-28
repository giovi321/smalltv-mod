import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
spec = importlib.util.spec_from_file_location('theme_pack', ROOT / 'tools/theme_pack.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
import theme_native as native

class PackTests(unittest.TestCase):
    def test_pixels_and_alpha(self):
        image = Image.new('RGBA', (2, 1))
        image.putdata([(255, 0, 0, 255), (0, 255, 0, 0)])
        data = pack.encode_image(image)
        self.assertEqual(data[:12], b'STI1'+struct.pack('<HHB3x', 2, 1, 1))
        self.assertEqual(data[12:], b'\x00\xf8\xff\xe0\x07\x00')

    def test_package_deterministic_and_source_paths_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            manifest = {'spec': 1, 'theme': {'id': 'test', 'name': 'Test', 'author': 'Me', 'version': '1'},
                        'display': {'width': 240, 'height': 240, 'background': '#000000'},
                        'layers': [{'id': 'bg', 'type': 'image', 'x': 0, 'y': 0, 'source': 'bg.png'}]}
            (root/'theme.json').write_text(json.dumps(manifest))
            Image.new('RGB', (2, 2), 'red').save(root/'bg.png')
            data = pack.build(root)
            self.assertEqual(data, pack.build(root))
            self.assertEqual(data[:8], b'STH1\x02\x00\x00\x00')
            self.assertIn(b'bg.png.sti', data)
            self.assertIn(b'"source":"bg.png"', data)
            manifest['layers'][0]['source'] = '../escape.png'
            (root/'theme.json').write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                pack.build(root)

    def test_rejects_oversized_image(self):
        with self.assertRaises(ValueError):
            pack.encode_image(Image.new('RGB', (241, 240)))

    # STI2 as agreed in #31: the STI1 header with magic STI2 and the palette
    # size in byte 9, the palette, one u32 offset per row (from the end of
    # the table), then each row as runs: top two bits the kind (00
    # transparent, 01 repeat, 10 literal), low six the length minus one.
    def test_sti2_direct_runs(self):
        image = Image.new('RGBA', (4, 1))
        # Alpha 0 is transparent whatever the colour; a repeat from two equal
        # pixels; a single pixel is a literal, with its alpha byte.
        image.putdata([(9, 9, 9, 0), (255, 0, 0, 255), (255, 0, 0, 255), (0, 255, 0, 128)])
        self.assertEqual(pack.encode_sti2(image),
                         b'STI2' + struct.pack('<HHBB2x', 4, 1, 1, 0) + struct.pack('<I', 0)
                         + b'\x00' + b'\x41\x00\xf8\xff' + b'\x80\xe0\x07\x80')

    def test_sti2_long_runs_split_and_equal_rows_shared(self):
        image = Image.new('RGBA', (70, 3), (0, 0, 0, 0))
        image.putpixel((69, 2), (255, 0, 0, 255))
        # Rows 0 and 1 are the same 64 + 6 transparent pixels and share data.
        self.assertEqual(pack.encode_sti2(image),
                         b'STI2' + struct.pack('<HHBB2x', 70, 3, 1, 0) + struct.pack('<III', 0, 0, 2)
                         + b'\x3f\x05' + b'\x3f\x04\x80\x00\xf8\xff')

    def test_sti2_long_literal_split(self):
        image = Image.new('RGB', (66, 1))
        image.putdata([(255, 0, 0) if i % 2 == 0 else (0, 255, 0) for i in range(66)])
        pixels = b'\x00\xf8\xe0\x07' * 33
        # Opaque: alpha flag 0, no alpha bytes, never a transparent run.
        self.assertEqual(pack.encode_sti2(image),
                         b'STI2' + struct.pack('<HHBB2x', 66, 1, 0, 0) + struct.pack('<I', 0)
                         + b'\xbf' + pixels[:128] + b'\x81' + pixels[128:])

    def test_sti2_indexed_odd_literal_pads_with_zero(self):
        image = Image.new('RGB', (3, 1))
        image.putdata([(255, 0, 0), (0, 0, 255), (255, 0, 0)])
        # Palette in order of first use; two indices per byte, high nibble
        # first; the unused low nibble of an odd run is 0.
        self.assertEqual(pack.encode_sti2(image, indexed=True),
                         b'STI2' + struct.pack('<HHBB2x', 3, 1, 0, 2) + b'\x00\xf8\x1f\x00'
                         + struct.pack('<I', 0) + b'\x82\x01\x00')

    def test_sti2_indexed_repeat_and_alpha(self):
        image = Image.new('RGBA', (8, 1))
        image.putdata([(0, 0, 0, 0)] + [(255, 0, 0, 255)] * 5 + [(255, 0, 0, 128), (255, 0, 0, 255)])
        # With alpha every palette entry has its alpha byte; a repeat of 4 or
        # more is one index byte.
        self.assertEqual(pack.encode_sti2(image, indexed=True),
                         b'STI2' + struct.pack('<HHBB2x', 8, 1, 1, 2) + b'\x00\xf8\xff\x00\xf8\x80'
                         + struct.pack('<I', 0) + b'\x00' + b'\x44\x00' + b'\x81\x10')

    def test_sti2_indexed_three_equal_stay_literal(self):
        image = Image.new('RGB', (5, 1))
        image.putdata([(255, 0, 0)] + [(0, 0, 255)] * 3 + [(255, 0, 0)])
        # A repeat of 3 would cost 2 bytes plus a new literal's head byte
        # either side; inside the literal the 3 indices cost 1.5 bytes.
        self.assertEqual(pack.encode_sti2(image, indexed=True),
                         b'STI2' + struct.pack('<HHBB2x', 5, 1, 0, 2) + b'\x00\xf8\x1f\x00'
                         + struct.pack('<I', 0) + b'\x84\x01\x11\x00')

    def test_sti2_indexed_needs_at_most_16_colours(self):
        image = Image.new('RGB', (17, 1))
        image.putdata([(i * 8, 0, 0) for i in range(17)])
        self.assertIsNone(pack.encode_sti2(image, indexed=True))

    def test_encode_image_picks_the_smallest_form_only_when_asked(self):
        sparse = Image.new('RGBA', (20, 20), (0, 0, 0, 0))
        sparse.putpixel((3, 3), (255, 255, 255, 255))
        self.assertEqual(pack.encode_image(sparse)[:4], b'STI1')  # default unchanged
        small = pack.encode_image(sparse, sti2=True)
        self.assertEqual(small, min((pack.encode_sti2(sparse), pack.encode_sti2(sparse, indexed=True)), key=len))
        self.assertLess(len(small), len(pack.encode_image(sparse)))
        # Where STI2 is larger, STI1 stays: one opaque pixel is 14 bytes as STI1.
        tiny = Image.new('RGB', (1, 1), (1, 2, 3))
        self.assertEqual(pack.encode_image(tiny, sti2=True), pack.encode_image(tiny))

    def test_sti2_package_renders_like_sti1(self):
        # End to end: the firmware's own reader and renderer (theme_native)
        # must draw the same frames from an STI2 package as from STI1.
        def forms(package):
            found = []
            count, at = struct.unpack_from('<H', package, 4)[0], 8
            for _ in range(count):
                length, size = struct.unpack_from('<HI', package, at)
                payload = package[at + 6 + length:at + 6 + length + size]
                if payload[:3] == b'STI':
                    found.append('sti1' if payload[3:4] == b'1' else 'indexed' if payload[9] else 'direct')
                at += 6 + length + size
            return sorted(found)

        def frames(package, tmp):
            path, output = Path(tmp)/'theme.stheme', Path(tmp)/'frames.stp'
            path.write_bytes(package)
            native.run('preview', path, output, 1700000000, 8, 16)
            return output.read_bytes()

        with tempfile.TemporaryDirectory() as tmp:
            # One image for each form: many colours with no runs stays STI1,
            # many colours on a transparent field is STI2 direct, two
            # colours are STI2 indexed.
            root = Path(tmp)/'forms'
            (root/'images').mkdir(parents=True)
            photo = Image.new('RGB', (40, 40))
            photo.putdata([(x * 6, y * 6, (x + y) * 3) for y in range(40) for x in range(40)])
            photo.save(root/'images/photo.png')
            sprite = Image.new('RGBA', (40, 40), (0, 0, 0, 0))
            for x in range(20):
                for y in range(15, 25):
                    sprite.putpixel((10 + x, y), (x * 12, 255 - x * 12, 128, 255))
            sprite.save(root/'images/sprite.png')
            icon = Image.new('RGBA', (40, 40), (0, 0, 0, 0))
            for x in range(10, 30):
                for y in range(10, 30):
                    icon.putpixel((x, y), (255, 255, 255, 255) if (x + y) % 2 else (255, 0, 0, 128))
            icon.save(root/'images/icon.png')
            (root/'theme.json').write_text(json.dumps({
                'spec': 1, 'theme': {'id': 'forms', 'name': 'Forms', 'author': 'test', 'version': '1.0.0'},
                'display': {'width': 240, 'height': 240, 'background': '#102030'},
                'layers': [{'id': name, 'type': 'image', 'x': 40 + 50 * i, 'y': 100, 'source': 'images/%s.png' % name}
                           for i, name in enumerate(('photo', 'sprite', 'icon'))]}))
            for theme, expected in ((root, ['direct', 'indexed', 'sti1']), (ROOT/'examples/themes/pixel-room', None)):
                sti1, sti2 = pack.build(theme), pack.build(theme, sti2=True)
                if expected:
                    self.assertEqual(forms(sti2), expected)
                self.assertLess(len(sti2), len(sti1))
                self.assertEqual(frames(sti1, tmp), frames(sti2, tmp))

    def test_live_status_example_is_dynamic_and_reproducible(self):
        root = ROOT/'examples/themes/live-status'
        manifest = json.loads((root/'theme.json').read_text())
        layers = {layer['id']: layer for layer in manifest['layers']}
        self.assertEqual(layers['headline']['scroll']['mode'], 'loop')
        self.assertEqual(layers['headline-bounce']['scroll']['mode'], 'bounce')
        self.assertIn('width', layers['level-bar']['bind'])
        self.assertIn('fill', layers['level-bar']['bind'])
        self.assertGreater(layers['level-track']['cornerRadius'], 0)
        built = pack.build(root)
        self.assertEqual(built, (ROOT/'examples/themes/live-status.stheme').read_bytes())

if __name__ == '__main__':
    unittest.main()
