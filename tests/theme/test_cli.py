import json
import base64
import io
import re
from PIL import Image
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / 'tools/smalltv_theme.py'
EXAMPLE = ROOT / 'examples/themes/pixel-room'

class ThemeCliTests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(CLI), *map(str, args)], cwd=ROOT,
                              capture_output=True, text=True)

    def test_validates_source_and_package(self):
        for source in (EXAMPLE, EXAMPLE.with_suffix('.stheme')):
            with self.subTest(source=source):
                result = self.run_cli('validate', source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('Valid', result.stdout)

    def test_rejects_color_before_creating_output(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)
            manifest = json.loads((EXAMPLE/'theme.json').read_text())
            manifest['layers'] = [manifest['layers'][1]]
            manifest['layers'][0]['color'] = 'red'
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli('build', source, source/'bad.stheme')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('layers[0].color:', result.stderr)
            self.assertFalse((source/'bad.stheme').exists())

    def test_preview_is_standalone_and_escapes_metadata(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d)/'preview.html'
            result = self.run_cli('preview', EXAMPLE, output, '--seconds', 2,
                                  '--fps', 8, '--time', '2026-09-18T23:59:59')
            self.assertEqual(result.returncode, 0, result.stderr)
            html = output.read_text()
            self.assertIn('data:image/png;base64,', html)
            self.assertIn('id="timeline"', html)
            self.assertIn('id="play"', html)
            self.assertNotIn('<script src=', html)
            self.assertIn('2026-09-18T23:59:59', html)

    def preview_data(self, path):
        text = path.read_text()
        return json.loads(re.search(r'<script id="preview-data" type="application/json">(.*?)</script>', text, re.S)[1])

    def frame(self, data, index):
        encoded = data['images'][data['frames'][index]].split(',', 1)[1]
        return Image.open(io.BytesIO(base64.b64decode(encoded))).convert('RGB')

    def test_preview_clock_rollover_and_transparent_sprite(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)
            (source/'sprite').mkdir()
            for frame, color in enumerate([(255, 0, 0, 255), (0, 255, 0, 255)]):
                image = Image.new('RGBA', (2, 1))
                image.putdata([color, (255, 255, 255, 0)])
                image.save(source/f'sprite/{frame:03d}.png')
            manifest = {'spec': 1, 'theme': {'id': 'test', 'name': 'Test', 'author': 'Me', 'version': '1'},
                        'display': {'width': 240, 'height': 240, 'background': '#0000ff'},
                        'layers': [
                          {'id': 'clock', 'type': 'text', 'x': 0, 'y': 0, 'value': '{YYYY} {MON} {DD} {HH}:{MM}:{SS}', 'size': 8, 'color': '#ffffff'},
                          {'id': 'sprite', 'type': 'animation', 'x': 30, 'y': 30, 'source': 'sprite', 'width': 2, 'height': 1, 'frames': 2, 'fps': 8, 'loop': True}]}
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli('preview', source, source/'clock.html', '--seconds', 2, '--fps', 8, '--time', '2026-12-31T23:59:59')
            self.assertEqual(result.returncode, 0, result.stderr)
            data = self.preview_data(source/'clock.html')
            self.assertEqual(len(data['frames']), 16)
            self.assertEqual(self.frame(data, 0).getpixel((30, 30)), (255, 0, 0))
            self.assertEqual(self.frame(data, 1).getpixel((30, 30)), (0, 255, 0))
            self.assertEqual(self.frame(data, 1).getpixel((31, 30)), (0, 0, 255))
            self.assertNotEqual(self.frame(data, 0).crop((0, 0, 240, 8)).tobytes(), self.frame(data, 8).crop((0, 0, 240, 8)).tobytes())
            # Compare the rollover clock with the known literal date/time, through the same font.
            manifest['layers'][1]['fps'] = 15
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli('preview', source, source/'timing.html', '--seconds', 1, '--fps', 15)
            self.assertEqual(result.returncode, 0, result.stderr)
            timing = self.preview_data(source/'timing.html')
            for i in range(15):
                self.assertEqual(self.frame(timing, i).getpixel((30, 30)), (0, 255, 0) if i % 2 else (255, 0, 0), f'frame {i}')
            manifest['layers'][0]['value'] = '2027 Jan 01 00:00:00'
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli('preview', source, source/'literal.html', '--seconds', 1, '--fps', 1)
            self.assertEqual(result.returncode, 0, result.stderr)
            expected = self.preview_data(source/'literal.html')
            self.assertEqual(self.frame(data, 8).crop((0, 0, 240, 8)).tobytes(), self.frame(expected, 0).crop((0, 0, 240, 8)).tobytes())

    def test_invalid_source_assets_and_unknown_variables(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)
            manifest = json.loads((EXAMPLE/'theme.json').read_text())
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli('validate', source)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('layers[0].source:', result.stderr)
            manifest['layers'] = [manifest['layers'][1]]
            manifest['layers'][0]['value'] = '{UNKNOWN}'
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli('validate', source)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('layers[0].value:', result.stderr)

    def test_preview_metadata_is_text_and_limits_are_enforced(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)
            manifest = json.loads((EXAMPLE/'theme.json').read_text())
            manifest['layers'] = []
            manifest['theme']['name'] = '<script>alert(1)</script> __DATA__'
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli('preview', source, source/'preview.html', '--seconds', 1, '--fps', 1)
            self.assertEqual(result.returncode, 0, result.stderr)
            html = (source/'preview.html').read_text()
            self.assertNotIn('<script>alert(1)</script>', html)
            self.assertIn('&lt;script&gt;alert(1)&lt;/script&gt; __DATA__', html)
            for args in (['--seconds', 61], ['--fps', 16], ['--time', 'bad'], ['--time', '2026-09-18T10:00:00+02:00']):
                result = self.run_cli('preview', source, source/'preview.html', *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn('Traceback', result.stderr)

    def test_preview_injects_declared_data_values(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)
            manifest = {
                'spec': 1,
                'theme': {'id': 'dynamictest', 'name': 'Dynamic Test', 'author': 'Test', 'version': '1'},
                'display': {'width': 240, 'height': 240, 'background': '#000000'},
                'data': [{'id': 'sensor', 'url': 'https://example.com/data', 'interval': 60,
                          'insecureTls': True,
                          'fields': [{'id': 'label', 'path': 'label'}, {'id': 'level', 'path': 'level'}]}],
                'layers': [
                    {'id': 'scroller', 'type': 'text', 'x': 0, 'y': 0, 'value': '{sensor.label}', 'size': 8,
                     'color': '#ffffff', 'scroll': {'width': 100, 'speed': 40, 'mode': 'loop', 'gap': 10}},
                    {'id': 'bar', 'type': 'shape', 'shape': 'rectangle', 'x': 0, 'y': 100, 'width': 10, 'height': 10,
                     'fill': '#00ff00',
                     'bind': {'width': {'source': 'sensor.level', 'input': [0, 100], 'output': [1, 200]}}}]}
            (source/'theme.json').write_text(json.dumps(manifest))
            result = self.run_cli(
                'preview', source, source/'dynamic.html', '--seconds', 2, '--fps', 10,
                '--time', '2026-09-18T10:00:00',
                '--data', 'sensor.label=ABCDEFGHIJKLMNOPQRSTUVWXYZ',
                '--data', 'sensor.level=75')
            self.assertEqual(result.returncode, 0, result.stderr)
            data = self.preview_data(source/'dynamic.html')
            self.assertGreater(len(set(data['frames'])), 1)  # scroll moves
            self.assertEqual(data['values']['sensor.level'], '75')

            for args in (
                ['--data', 'unknown.field=1'],
                ['--data', 'sensor.level'],
                ['--data', '=1'],
                ['--data', 'sensor.level=1', '--data', 'sensor.level=2'],
                [a for i in range(33) for a in ('--data', f'sensor.level={i}')],
            ):
                result = self.run_cli('preview', source, source/'bad.html', '--seconds', 1, '--fps', 1, *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn('Traceback', result.stderr)

    def test_documentation_covers_scrolling_and_dynamic_properties(self):
        documentation = (ROOT/'docs/src/content/docs/features/themes.md').read_text()
        for token in ('"mode": "loop"', '"mode": "bounce"', 'cornerRadius',
                      'scroll.width', 'scroll.speed', 'color stops',
                      '--data', 'live-status'):
            self.assertIn(token, documentation)
        for row in ('| text ', '| rectangle ', '| circle ', '| line ', '| image ', '| animation '):
            self.assertIn(row, documentation)

    def test_corrupt_package_validation(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)/'broken.stheme'
            source.write_bytes(b'broken')
            result = self.run_cli('validate', source)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('package', result.stderr)
            self.assertNotIn('Traceback', result.stderr)

if __name__ == '__main__':
    unittest.main()
