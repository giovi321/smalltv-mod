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

if __name__ == '__main__':
    unittest.main()
