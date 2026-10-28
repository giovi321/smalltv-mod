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
