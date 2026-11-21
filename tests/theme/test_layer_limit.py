"""The layer limit is one number for everything that knows it.

A firmware build sets it with -DTHEME_MAX_LAYERS=<n> (default 32). The three
tools that check a theme before it reaches a device (tools/theme_pack.py with
tools/theme_native.py, and the skill's stheme.py) read the same number from the
environment variable SMALLTV_THEME_MAX_LAYERS, default 32. These tests hold
them to each other and to the firmware validator (the native tool is built from
the firmware's own ThemeEngine.cpp with the same limit):

- the variable is read the same way by both readers (a number from 1 to 255,
  anything else is an error, never "32");
- the layers a theme may have, and the number the refusal names, are the same
  in the firmware validator, the packer and stheme.py, at 32 and at 40.

The C++ side (the limit itself, the message, memory and stack at the cap) is in
tests/theme/test_validation.cpp, test_memory.cpp, test_render.cpp and
check_layer_limit.sh; the packer's own check is test_pack.py::test_layer_limit.
"""
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'skills/smalltv-theme-clock/scripts'))
import stheme
import theme_native as native
from theme_pack import build as firmware_build

VARIABLE = 'SMALLTV_THEME_MAX_LAYERS'


def native_available():
    try:
        native.executable()
        return True, ''
    except ValueError as error:
        return False, str(error)


AVAILABLE, REASON = native_available()
# Locally a missing toolchain skips; in CI it must fail, not hide a mismatch.
if not AVAILABLE and os.environ.get('CI'):
    raise RuntimeError('Native theme tool unavailable in CI: '+REASON)
needs_native = unittest.skipUnless(AVAILABLE, 'native theme tool unavailable: '+REASON)


class Env:
    """The environment of a run: the variable unset (limit None), or set to `limit`."""
    def __init__(self, limit):
        self.limit = limit

    def __enter__(self):
        self.patch = mock.patch.dict(os.environ)
        self.patch.start()
        os.environ.pop(VARIABLE, None)
        if self.limit is not None:
            os.environ[VARIABLE] = str(self.limit)

    def __exit__(self, *exc):
        self.patch.stop()


def theme(count):
    return {'spec': 1, 'theme': {'id': 'test', 'name': 'Test', 'author': 'Me', 'version': '1'},
            'display': {'width': 240, 'height': 240, 'background': '#000000'},
            'layers': [{'id': f'l{i}', 'type': 'shape', 'shape': 'rectangle', 'x': 0, 'y': 0,
                        'width': 1, 'height': 1, 'fill': '#000000'} for i in range(count)]}


NAMED = re.compile(r'at most (\d+) layers')


class ReadingTheVariable(unittest.TestCase):
    """tools/theme_native.py and the skill's stheme.py read the variable alike."""
    READERS = {'theme_native.max_layers': lambda: native.max_layers(), 'stheme.max_layers': lambda: stheme.max_layers()}

    def test_default_is_32(self):
        for name, reader in self.READERS.items():
            with self.subTest(reader=name), Env(None):
                self.assertEqual(reader(), 32)

    def test_a_number_from_1_to_255(self):
        for name, reader in self.READERS.items():
            for value in ('1', '32', '33', '40', '255'):
                with self.subTest(reader=name, value=value), Env(value):
                    self.assertEqual(reader(), int(value))

    def test_anything_else_is_an_error_not_32(self):
        # 255 is the most a layer index can name: a show rule keeps its layer in one byte.
        for name, reader in self.READERS.items():
            for value in ('', '0', '256', '1000', '-1', '4.5', 'many', '0x28', ' 40', '4 0',
                          '٤٠'):   # Arabic-Indic "40": int() reads it, a limit should not
                with self.subTest(reader=name, value=value), Env(value):
                    with self.assertRaises(ValueError):
                        reader()


class OneValue(unittest.TestCase):
    """stheme.py alone: what it accepts and what it says, at the default and at 40."""
    def refusal(self, count):
        return stheme.validate(theme(count))

    def test_default(self):
        with Env(None):
            self.assertEqual(self.refusal(32), [])
            self.assertEqual(self.refusal(33), ['layers: expected an array with at most 32 layers'])

    def test_raised(self):
        with Env(40):
            self.assertEqual(self.refusal(32), [])
            self.assertEqual(self.refusal(33), [])
            self.assertEqual(self.refusal(40), [])
            self.assertEqual(self.refusal(41), ['layers: expected an array with at most 40 layers'])

    def test_lowered(self):
        with Env(10):
            self.assertEqual(self.refusal(10), [])
            self.assertEqual(self.refusal(11), ['layers: expected an array with at most 10 layers'])

    def test_a_bad_value_is_refused_by_validate(self):
        with Env('many'):
            with self.assertRaises(ValueError):
                stheme.validate(theme(1))


@needs_native
class AllFourAgree(unittest.TestCase):
    """The firmware validator (as the native tool), the packer and stheme.py give
    the same verdict at the limit and one over it, and all name the same number."""
    def verdicts(self, limit, count):
        """For a theme of `count` layers: what each says, as (accepted, number it names or None)."""
        out = {}
        with tempfile.TemporaryDirectory() as d, Env(limit):
            root = Path(d)
            (root/'theme.json').write_text(json.dumps(theme(count)))
            try:
                native.validate_manifest(root/'theme.json')
                out['firmware'] = (True, None)
            except ValueError as error:
                out['firmware'] = (False, int(NAMED.search(str(error)).group(1)))
            try:
                firmware_build(root)
                out['packer'] = (True, None)
            except ValueError as error:
                out['packer'] = (False, int(NAMED.search(str(error)).group(1)))
            errors = stheme.validate(theme(count))
            out['stheme'] = (False, int(NAMED.search(errors[0]).group(1))) if errors else (True, None)
        return out

    def test_at_the_limit_and_one_over(self):
        for limit in (None, 32, 40):
            number = limit or 32
            with self.subTest(limit=limit):
                self.assertEqual(self.verdicts(limit, number),
                                 {name: (True, None) for name in ('firmware', 'packer', 'stheme')})
                self.assertEqual(self.verdicts(limit, number + 1),
                                 {name: (False, number) for name in ('firmware', 'packer', 'stheme')})

    def test_the_native_tool_follows_the_variable_when_it_changes(self):
        # One process, two limits in a row: the host build of the firmware
        # validator is per limit, not the first one that was built.
        for limit in (40, 32, 40):
            with self.subTest(limit=limit):
                self.assertEqual(self.verdicts(limit, limit + 1)['firmware'], (False, limit))
                self.assertEqual(self.verdicts(limit, limit)['firmware'], (True, None))


if __name__ == '__main__':
    unittest.main()
