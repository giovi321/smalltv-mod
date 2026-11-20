"""Tests for tools/make_oom_theme.py, the writer of the memory-test packages.

The packages are plain data (STH1 header, then path/payload pairs). These
tests read the package back and check what the tool promised: the layer count,
the text length, the optional data source, and the refusals.
"""
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('make_oom_theme', ROOT / 'tools/make_oom_theme.py')
oom = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oom)

PLACEHOLDER_URL = 'http://198.51.100.10:8765/d.json'


def unpack(data):
    """Reads a package back: {path: payload}."""
    assert data[:4] == b'STH1'
    count, reserved = struct.unpack('<HH', data[4:8])
    assert reserved == 0
    files, at = {}, 8
    for _ in range(count):
        name_len, payload_len = struct.unpack('<HI', data[at:at + 6])
        at += 6
        name = data[at:at + name_len].decode()
        at += name_len
        files[name] = data[at:at + payload_len]
        at += payload_len
    assert at == len(data)
    return files


def manifest_of(data):
    return json.loads(unpack(data)['theme.json'])


class DefaultPackage(unittest.TestCase):
    def test_default_is_32_layers_of_65_characters(self):
        # 32 x 65 is the measured crash point (issue #132). A smaller default
        # would let a memory margin pass the final device run for the wrong reason.
        m = manifest_of(oom.build())
        self.assertEqual(len(m['layers']), 32)
        self.assertEqual({len(l['value']) for l in m['layers']}, {65})
        self.assertEqual((oom.DEFAULT_LAYERS, oom.DEFAULT_CHARS), (32, 65))

    def test_text_layers_only_and_no_data_by_default(self):
        m = manifest_of(oom.build())
        self.assertEqual({l['type'] for l in m['layers']}, {'text'})
        self.assertNotIn('data', m)

    def test_theme_id_is_fixed(self):
        self.assertEqual(manifest_of(oom.build())['theme']['id'], 'oom-test')

    def test_layer_ids_are_unique(self):
        ids = [l['id'] for l in manifest_of(oom.build())['layers']]
        self.assertEqual(len(ids), len(set(ids)))

    def test_package_is_deterministic(self):
        self.assertEqual(oom.build(), oom.build())


class Sizes(unittest.TestCase):
    def test_layer_count_and_text_length_follow_the_arguments(self):
        for layers, chars in ((1, 1), (12, 128), (26, 12), (32, 40)):
            m = manifest_of(oom.build(layers, chars))
            self.assertEqual(len(m['layers']), layers)
            self.assertEqual({len(l['value']) for l in m['layers']}, {chars})

    def test_layer_count_limits(self):
        for bad in (0, 33, -1):
            with self.assertRaises(ValueError):
                oom.build(bad, 10)

    def test_text_length_limits(self):
        # the manifest's own limit for a text value is 128 characters
        for bad in (0, 129):
            with self.assertRaises(ValueError):
                oom.build(4, bad)


class DataSource(unittest.TestCase):
    def test_one_source_with_one_field(self):
        m = manifest_of(oom.build(8, 20, data_url=PLACEHOLDER_URL))
        self.assertEqual(len(m['data']), 1)
        source = m['data'][0]
        self.assertEqual(source['url'], PLACEHOLDER_URL)
        self.assertEqual(source['interval'], 10)
        self.assertEqual(len(source['fields']), 1)

    def test_one_layer_shows_the_value_and_is_one_of_the_layers(self):
        m = manifest_of(oom.build(8, 20, data_url=PLACEHOLDER_URL))
        self.assertEqual(len(m['layers']), 8)
        source = m['data'][0]['id']
        field = m['data'][0]['fields'][0]['id']
        token = '{%s.%s}' % (source, field)
        shown = [l for l in m['layers'] if token in l['value']]
        self.assertEqual(len(shown), 1)

    def test_the_first_layer_shows_the_value(self):
        # the usage text and the documentation both say "the first layer"
        m = manifest_of(oom.build(8, 20, data_url=PLACEHOLDER_URL))
        self.assertIn('{w.p}', m['layers'][0]['value'])
        for other in m['layers'][1:]:
            self.assertNotIn('{', other['value'])

    def test_text_length_is_kept_with_a_data_layer(self):
        m = manifest_of(oom.build(8, 20, data_url=PLACEHOLDER_URL))
        self.assertEqual({len(l['value']) for l in m['layers']}, {20})

    def test_interval_is_passed_on(self):
        m = manifest_of(oom.build(4, 20, data_url=PLACEHOLDER_URL, interval=60))
        self.assertEqual(m['data'][0]['interval'], 60)

    def test_interval_below_ten_seconds_is_refused(self):
        with self.assertRaises(ValueError):
            oom.build(4, 20, data_url=PLACEHOLDER_URL, interval=9)

    def test_interval_above_a_day_is_refused(self):
        with self.assertRaises(ValueError):
            oom.build(4, 20, data_url=PLACEHOLDER_URL, interval=86401)

    def test_url_must_be_http_or_https(self):
        with self.assertRaises(ValueError):
            oom.build(4, 20, data_url='ftp://198.51.100.10/d.json')

    def test_url_over_200_bytes_is_refused(self):
        with self.assertRaises(ValueError):
            oom.build(4, 20, data_url='http://198.51.100.10/' + 'a' * 200)

    def test_url_of_exactly_200_bytes_is_kept(self):
        url = 'http://198.51.100.10/' + 'a' * (200 - len('http://198.51.100.10/'))
        self.assertEqual(len(url), 200)
        self.assertEqual(manifest_of(oom.build(4, 20, data_url=url))['data'][0]['url'], url)

    def test_text_shorter_than_the_token_is_refused(self):
        # the token "{w.p}" would be cut
        with self.assertRaises(ValueError):
            oom.build(4, 3, data_url=PLACEHOLDER_URL)

    def test_the_default_url_is_a_documentation_address(self):
        self.assertEqual(oom.DEFAULT_DATA_URL, PLACEHOLDER_URL)


class CommandLine(unittest.TestCase):
    def run_main(self, *args):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / 'p.stheme'
            code = oom.main(['make_oom_theme.py', str(out)] + list(args))
            return code, (out.read_bytes() if out.exists() else None)

    def test_defaults(self):
        code, data = self.run_main()
        self.assertEqual(code, 0)
        self.assertEqual(data, oom.build())

    def test_positional_layers_and_chars(self):
        code, data = self.run_main('12', '128')
        self.assertEqual(code, 0)
        self.assertEqual(data, oom.build(12, 128))

    def test_data_option_uses_the_placeholder_url(self):
        code, data = self.run_main('8', '20', '--data')
        self.assertEqual(code, 0)
        self.assertEqual(data, oom.build(8, 20, data_url=oom.DEFAULT_DATA_URL))

    def test_data_url_and_interval_options(self):
        code, data = self.run_main('8', '20', '--data-url', 'http://203.0.113.5:9/x.json', '--interval', '30')
        self.assertEqual(code, 0)
        self.assertEqual(data, oom.build(8, 20, data_url='http://203.0.113.5:9/x.json', interval=30))

    def test_bad_values_give_a_nonzero_code_and_no_file(self):
        code, data = self.run_main('33')
        self.assertNotEqual(code, 0)
        self.assertIsNone(data)


if __name__ == '__main__':
    unittest.main()
