"""skills/smalltv-theme-clock/scripts/stheme.py is a third implementation of the
theme format, after tools/ and SmallTV Studio. These tests hold it to the
firmware: the same package bytes as tools/theme_pack.py, packages the firmware
validator accepts, preview frames equal to the native renderer's, and the same
accept/reject verdict as the firmware's manifest validator.

A change to the theme format must update stheme.py in the same PR."""
import contextlib
import copy
import datetime as dt
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'skills/smalltv-theme-clock/scripts'))
import stheme
import theme_native as native
from theme_pack import build as firmware_build

EXAMPLES = ROOT/'examples/themes'
ANALOG = Path(__file__).resolve().parent/'skill_parity_analog.json'
FRAME = 240*240*3


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


def make_analog(root):
    """The spec 2 test theme: ticks, arcs with colour stops, the two-hand AM/PM
    hour, a moon-bound radius, localized text, an animation and a scroll."""
    (root/'images').mkdir(parents=True)
    (root/'animations/dot').mkdir(parents=True)
    shutil.copy(ANALOG, root/'theme.json')
    im = Image.new('RGBA', (240, 240), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for r in range(118, 0, -6):
        d.ellipse((120-r, 120-r, 120+r, 120+r), fill=(11+r//8, 20+r//6, 32+r//4, 255))
    im.save(root/'images/bg.png')
    badge = Image.new('RGBA', (40, 20), (0, 0, 0, 0))
    ImageDraw.Draw(badge).rectangle((2, 2, 37, 17), fill=(71, 194, 255, 140))
    badge.save(root/'images/badge.png')
    for i in range(4):
        f = Image.new('RGBA', (12, 12), (0, 0, 0, 0))
        ImageDraw.Draw(f).ellipse((i, i, 11-i, 11-i), fill=(255, 90, 71, 255))
        f.save(root/f'animations/dot/{i:03d}.png')


@needs_native
class PackageParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        analog = Path(cls.tmp.name)/'analog'
        make_analog(analog)
        cls.sources = {name: EXAMPLES/name for name in ('pixel-room', 'terminal-ops', 'live-status')}
        cls.sources['analog'] = analog
        cls.packages = {}
        for name, source in cls.sources.items():
            theme, images = stheme.load_source(source)
            cls.packages[name] = (theme, stheme.pack(theme, images))

    def test_spec_resolution(self):
        self.assertEqual(self.packages['live-status'][0]['spec'], 1)
        self.assertEqual(self.packages['analog'][0]['spec'], 2)   # "spec": "auto" in the source

    def test_bytes_equal_firmware_packer_and_firmware_accepts(self):
        for name, source in self.sources.items():
            with self.subTest(theme=name), tempfile.TemporaryDirectory() as d:
                theme, data = self.packages[name]
                # The firmware packer needs a number, as stheme.py build writes back.
                copy_ = Path(d)/'src'
                shutil.copytree(source, copy_)
                stheme.write_json(copy_/'theme.json', theme)
                self.assertTrue(data == firmware_build(copy_), f'{name}: package differs from tools/theme_pack.py')
                package = Path(d)/'p.stheme'
                package.write_bytes(data)
                native.validate_package(package)
                if (EXAMPLES/f'{name}.stheme').is_file():
                    self.assertTrue(data == (EXAMPLES/f'{name}.stheme').read_bytes(), f'{name}: differs from {name}.stheme')

    def assert_frames_match(self, name, start, fps, count, values=None, lang=None):
        values = values or {}
        theme, data = self.packages[name]
        t0 = dt.datetime.fromisoformat(start)
        epoch = int(t0.replace(tzinfo=dt.timezone.utc).timestamp())
        with tempfile.TemporaryDirectory() as d:
            package, raw = Path(d)/'p.stheme', Path(d)/'f.rgb'
            package.write_bytes(data)
            native.run('preview', package, raw, epoch, fps, count,
                       *(['@lang='+lang] if lang else []), *(f'{k}={v}' for k, v in values.items()))
            frames = raw.read_bytes()[12:]
        self.assertEqual(len(frames), FRAME*count)
        th, assets = stheme.unpack(data)
        renderer = stheme.Renderer(th, assets, lang or 'en', values)
        for i in range(count):
            ms = -(-i*1000//fps)
            pixels = renderer.frame(t0+dt.timedelta(milliseconds=ms), ms)
            mine = b''.join(bytes(stheme.rgb888(c)) for c in pixels)
            theirs = frames[i*FRAME:(i+1)*FRAME]
            if mine != theirs:
                diff = sum(mine[k:k+3] != theirs[k:k+3] for k in range(0, FRAME, 3))
                self.fail(f'{name} frame {i} at {fps} fps: {diff} pixels differ from the native renderer')

    def test_preview_frames_equal_native_renderer(self):
        runs = [
            ('pixel-room', '2026-11-06T10:09:55', 10, 30, None, None),
            ('terminal-ops', '2026-09-23T23:58:58', 1, 4, None, None),
            ('live-status', '2026-11-06T10:09:55', 10, 20, None, None),
            ('live-status', '2026-11-06T10:09:55', 15, 30,
             {'status.label': 'Build pipeline is green again', 'status.level': '73', 'status.state': 'OK'}, None),
            ('analog', '2026-11-06T10:09:55', 10, 20, {'device.temperature': '21.5'}, None),
            ('analog', '2026-11-06T15:42:07', 15, 30, {'device.temperature': '-4'}, 'de'),   # PM hour hand
            ('analog', '2026-03-29T11:59:59', 1, 4, None, 'pt'),   # AM/PM hand hand-over
            # `show`: hidden with no value, shown in the second range (open end), hidden between ranges
            ('analog', '2026-11-06T10:09:55', 10, 2, {'w.code': '95'}, None),
            ('analog', '2026-11-06T10:09:55', 10, 2, {'w.code': '60'}, None),
            ('analog', '2026-11-06T10:09:55', 10, 2, {'w.code': '70'}, None),
        ]
        for name, start, fps, count, values, lang in runs:
            with self.subTest(theme=name, start=start, fps=fps, lang=lang):
                self.assert_frames_match(name, start, fps, count, values, lang)


BASE = {'spec': 2, 'theme': {'id': 't', 'name': 'T', 'author': 'a', 'version': '1'},
        'display': {'width': 240, 'height': 240, 'background': '#000000'},
        'layers': [{'id': 'c', 'type': 'text', 'x': 0, 'y': 0, 'value': '{HH}', 'size': 16, 'color': '#ffffff'}]}
TEXT = BASE['layers'][0]


def layer(**kw):
    t = copy.deepcopy(BASE)
    t['layers'][0].update(kw)
    return t


def manifest(**kw):
    t = copy.deepcopy(BASE)
    t.update(kw)
    return t


def shape(**kw):
    return dict(id='s', type='shape', x=120, y=120, stroke='#ffffff', **kw)


def arc():
    return shape(shape='arc', radius=10, start=0, sweep=10)


SECOND = {'source': 'time.second', 'input': [0, 60], 'output': [0, 360]}
SOURCE = {'id': 'w', 'url': 'http://a/b', 'interval': 10, 'fields': [{'id': 'x', 'path': 'x'}]}
SHOW = {'source': 'w.x', 'in': [[0, 10]]}
TIME_SHOW = {'source': 'time.second', 'in': [[0, 4]]}


def show_layers(rules):
    """Text layers with 4 `show` rules each, the last with what is left of `rules`."""
    return [dict(TEXT, id=f'l{i}', show=[SHOW] * min(4, rules - 4 * i)) for i in range(-(-rules // 4))]

# Both validators must give the same verdict. Keep cases away from the edge of
# the per-language translation budget: stheme.py counts the device's 28 bytes
# per entry, the host build of the firmware validator 40.
MANIFESTS = {
    'ok': BASE,
    'spec1 H': manifest(spec=1, layers=[dict(TEXT, value='{H}')]),
    'spec1 arc': manifest(spec=1, layers=[arc()]),
    'spec2 arc ok': manifest(layers=[arc()]),
    'arc fill': manifest(layers=[dict(arc(), fill='#000000')]),
    'arc no stroke': manifest(layers=[{k: v for k, v in arc().items() if k != 'stroke'}]),
    'hand ok no tail': manifest(layers=[shape(shape='hand', length=10, angle=360)]),
    'hand angle 400': manifest(layers=[shape(shape='hand', length=10, angle=400)]),
    'ticks count 61': manifest(layers=[shape(shape='ticks', radius=50, count=61, length=3)]),
    'ticks bind sweep': manifest(layers=[dict(shape(shape='ticks', radius=50, count=12, length=3), bind={'sweep': SECOND})]),
    'hand bind angle ok': manifest(layers=[dict(shape(shape='hand', length=50), angle=0, bind={'angle': SECOND})]),
    'bad time name': manifest(layers=[dict(shape(shape='hand', length=50), angle=0,
                                           bind={'angle': dict(SECOND, source='time.millis')})]),
    'spec1 time bind': manifest(spec=1, layers=[dict(TEXT, bind={'x': {'source': 'time.second', 'input': [0, 60], 'output': [0, 100]}})]),
    'lang ok': layer(value={'en': 'a', 'de': 'b'}),
    'lang no en': layer(value={'de': 'b'}),
    'lang bad key': layer(value={'en': 'a', 'nl': 'b'}),
    'lang spec1': manifest(spec=1, layers=[dict(TEXT, value={'en': 'a'})]),
    'lang budget': manifest(layers=[dict(TEXT, id=f'l{i}', value={'en': 'a', **{k: 'x'*100 for k in ('pt', 'es', 'fr', 'de', 'it')}})
                                    for i in range(2)]),
    'device token': layer(value='{device.humidity}'),
    'device bad': layer(value='{device.pressure}'),
    'week token': layer(value='KW {WW}'),
    'spec1 week token': manifest(spec=1, layers=[dict(TEXT, value='{WW}')]),
    'week token near miss': layer(value='{WWW}'),
    'data id time': manifest(data=[dict(SOURCE, id='time')]),
    'https no insecure': manifest(data=[dict(SOURCE, url='https://a/b')]),
    'interval 5': manifest(data=[dict(SOURCE, interval=5)]),
    'bounce gap': layer(scroll={'width': 50, 'mode': 'bounce', 'speed': 10, 'gap': 3}),
    'size 97': layer(size=97),
    'color short': layer(color='#fff'),
    'unknown field': layer(foo=1),
    'rect radius 121': manifest(layers=[dict(id='r', type='shape', shape='rectangle', x=0, y=0, width=10, height=10,
                                             cornerRadius=121, fill='#000000')]),
    'circle no paint': manifest(layers=[dict(id='r', type='shape', shape='circle', x=0, y=0, radius=10)]),
    'unmatched brace': layer(value='a}'),
    'spec 3': manifest(spec=3),
    '33 layers': manifest(layers=[dict(TEXT, id=f'x{i}') for i in range(33)]),
    'dup id': manifest(layers=[TEXT, TEXT]),
    'show ok': manifest(data=[SOURCE], layers=[dict(TEXT, show=[SHOW])]),
    'show spec1': manifest(spec=1, data=[SOURCE], layers=[dict(TEXT, show=[SHOW])]),
    'show age': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, source='w.age')])]),
    'show age spec1': manifest(spec=1, data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, source='w.age')])]),
    'show time source': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, source='time.second')])]),
    'show time every': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, every=10)])]),
    'show time every 1': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, every=1)])]),
    'show time every 2.5': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, every=2.5)])]),
    'show time every true': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, every=True)])]),
    'show time every null': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, every=None)])]),
    'show time every 10.0': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, every=10.0)])]),
    'show time every no source': manifest(layers=[dict(TEXT, show=[{'every': 10, 'in': [[0, 4]]}])]),
    'show time every number source': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, source=5, every=10)])]),
    'show time unknown': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, source='time.millis')])]),
    'show time spec1': manifest(spec=1, layers=[dict(TEXT, show=[TIME_SHOW])]),
    'show device source': manifest(layers=[dict(TEXT, show=[dict(TIME_SHOW, source='device.humidity')])]),
    'show every on data': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, every=10)])]),
    'show open end': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[None, 5]], 'missing': True})])]),
    'show both null': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[None, None]]})])]),
    'show low above high': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[5, 1]]})])]),
    'show too big': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[0, 1e39]]})])]),
    'show 5 rules': manifest(data=[SOURCE], layers=[dict(TEXT, show=[SHOW] * 5)]),
    'show no ranges': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': []})])]),
    'show 36 rules': manifest(data=[SOURCE], layers=show_layers(36)),
    'show 37 rules': manifest(data=[SOURCE], layers=show_layers(37)),
    'stops not increasing': layer(bind={'color': {'source': 'time.second',
                                                  'stops': [{'at': 5, 'value': '#000000'}, {'at': 5, 'value': '#ffffff'}]}}),
}


@needs_native
class ManifestParityTests(unittest.TestCase):
    def test_validators_agree(self):
        self.assertEqual(len(MANIFESTS), 59)
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/'theme.json'
            for name, theme in MANIFESTS.items():
                with self.subTest(case=name):
                    path.write_text(json.dumps(theme))
                    try:
                        native.validate_manifest(path)
                        firmware = None
                    except ValueError as error:
                        firmware = str(error).strip().splitlines()[-1]
                    errors = stheme.validate(theme)
                    self.assertEqual(firmware is None, not errors,
                                     f'firmware: {firmware or "accepted"}; stheme: {errors[:1] or "accepted"}')


class ShowTests(unittest.TestCase):
    """`show` in stheme.py: what it accepts, the messages, and which layers it hides."""
    def theme(self, rules, **kw):
        return manifest(data=[SOURCE], layers=[dict(TEXT, show=rules)], **kw)

    def test_accepts_and_refuses_like_the_firmware(self):
        self.assertEqual(stheme.validate(self.theme([SHOW])), [])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, source='w.age', missing=True)])), [])
        self.assertEqual(stheme.validate(self.theme([SHOW], spec=1)), ['layers[0].show: needs "spec": 2'])
        bad = {
            'device.humidity': 'layers[0].show[0].source: expected a declared data field or a time.* source',
            'w.other': 'layers[0].show[0].source: expected a declared data field or a time.* source',
            'time.millis': 'layers[0].show[0].source: unknown time.* name',
            'time.Second': 'layers[0].show[0].source: unknown time.* name',
        }
        for source, message in bad.items():
            self.assertEqual(stheme.validate(self.theme([dict(SHOW, source=source)])), [message])
        self.assertEqual(stheme.validate(self.theme([])), ['layers[0].show: expected 1 to 4 rules'])
        self.assertEqual(stheme.validate(self.theme([SHOW] * 5)), ['layers[0].show: expected 1 to 4 rules'])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, **{'in': [[3, 3], [None, 1], [5, None]]})])), [])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, **{'in': [[None, None]]})])),
                         ['layers[0].show[0].in[0]: at most one end may be null'])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, **{'in': [[0, 1e39]]})])),
                         ['layers[0].show[0].in[0]: expected numbers from -3.4e38 to 3.4e38, or null'])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, **{'in': [[3, 2]]})])),
                         ['layers[0].show[0].in[0]: low must not be above high'])
        # 36 rules are accepted; one more is refused, at the layer that crosses the cap.
        self.assertEqual(stheme.validate(manifest(data=[SOURCE], layers=show_layers(36))), [])
        self.assertEqual(stheme.validate(manifest(data=[SOURCE], layers=show_layers(37))),
                         ['layers[9].show: expected at most 36 rules in a theme'])
        self.assertEqual(stheme.validate(manifest(data=[SOURCE], layers=show_layers(38))),
                         ['layers[9].show: expected at most 36 rules in a theme'])

    def test_time_sources_and_every_like_the_firmware(self):
        every = lambda value: self.theme([dict(TIME_SHOW, every=value)])
        nothing = self.theme([TIME_SHOW])
        self.assertEqual(stheme.validate(nothing), [])
        for name in stheme.TIME_NAMES:
            self.assertEqual(stheme.validate(self.theme([dict(TIME_SHOW, source=f'time.{name}')])), [], name)
        for ok in (2, 3, 3599, 3600):
            self.assertEqual(stheme.validate(every(ok)), [], ok)
        for bad in (0, 1, 3601, -10, 2.5, 10.0, '10', None, True):
            self.assertEqual(stheme.validate(every(bad)),
                             ['layers[0].show[0].every: expected an integer from 2 to 3600'], bad)
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, every=10)])),
                         ['layers[0].show[0].every: only for a time.* source'])
        # A time rule needs no declared data, and counts toward the rule cap like any rule.
        self.assertEqual(stheme.validate(manifest(layers=[dict(TEXT, show=[TIME_SHOW])])), [])
        self.assertEqual(stheme.validate(manifest(spec=1, layers=[dict(TEXT, show=[TIME_SHOW])])),
                         ['layers[0].show: needs "spec": 2'])
        self.assertEqual(stheme.needed_spec(manifest(layers=[dict(TEXT, show=[TIME_SHOW])])), 2)

    def test_time_rules_follow_the_clock(self):
        turn = lambda low, high, **kw: dict(TEXT, show=[dict(TIME_SHOW, every=10, **{'in': [[low, high]]}, **kw)])
        at = lambda second, minute=0: dt.datetime(2026, 9, 18, 15, minute, second)
        first, second = turn(0, 4), turn(5, 9)
        for s in range(0, 120):
            t = at(s % 60, s // 60)
            self.assertEqual(stheme.shown(first, {}, t), s % 10 < 5, s)
            self.assertEqual(stheme.shown(second, {}, t), s % 10 >= 5, s)
        # No `every`: the plain value, and the hour wraps with a modulus too.
        late = dict(TEXT, show=[{'source': 'time.hour', 'in': [[22, 23], [0, 5]]}])
        self.assertTrue(stheme.shown(late, {}, dt.datetime(2026, 9, 18, 23, 0, 0)))
        self.assertFalse(stheme.shown(late, {}, dt.datetime(2026, 9, 18, 12, 0, 0)))
        half = dict(TEXT, show=[{'source': 'time.hour', 'every': 12, 'in': [[0, 5]]}])
        self.assertTrue(stheme.shown(half, {}, dt.datetime(2026, 9, 18, 12, 30, 0)))
        self.assertFalse(stheme.shown(half, {}, dt.datetime(2026, 9, 18, 18, 0, 0)))
        # No clock: the rule holds only with "missing".
        self.assertFalse(stheme.shown(first, {}, None))
        self.assertTrue(stheme.shown(turn(0, 4, missing=True), {}, None))
        # A data field named like a time source does not override the clock.
        self.assertFalse(stheme.shown(first, {'time.second': '0'}, None))
        # A data rule next to a time rule: both must hold.
        both = dict(TEXT, show=[SHOW, dict(TIME_SHOW, every=10)])
        self.assertTrue(stheme.shown(both, {'w.x': '5'}, at(3)))
        self.assertFalse(stheme.shown(both, {'w.x': '50'}, at(3)))
        self.assertFalse(stheme.shown(both, {'w.x': '5'}, at(7)))

    def test_preview_draws_the_layer_of_the_second(self):
        box = dict(id='box', type='shape', shape='rectangle', x=10, y=10, width=5, height=5, fill='#ff0000',
                   show=[dict(TIME_SHOW, every=10)])
        theme = manifest(layers=[box])
        bg, red = stheme.color565('#000000'), stheme.color565('#ff0000')
        pixel = lambda s: stheme.Renderer(theme, {}, 'en', {}).frame(dt.datetime(2026, 9, 18, 15, 4, s), 0)[12 * 240 + 12]
        self.assertEqual([pixel(s) for s in (10, 12, 14, 15, 17, 19)], [red] * 3 + [bg] * 3)

    def test_a_missing_or_non_string_source_says_what_the_firmware_says(self):
        # The firmware reads "source" first, so every other field of the rule waits for it.
        for source in (None, 5, True, ['time.second'], {}):
            for extra in ({}, {'every': 10}, {'every': 1}):
                rule = dict(TIME_SHOW, source=source, **extra)
                if source is None:
                    del rule['source']
                self.assertEqual(stheme.validate(self.theme([rule])),
                                 ['layers[0].show[0].source: expected a string'], (source, extra))

    def test_every_as_a_float_is_refused_by_both(self):
        # 10.0 is a number to JSON, but not an integer to the firmware: both refuse it.
        self.assertEqual(stheme.validate(self.theme([dict(TIME_SHOW, every=10.0)])),
                         ['layers[0].show[0].every: expected an integer from 2 to 3600'])

    def test_warns_about_a_range_that_can_never_match(self):
        # The values of each time.* source (ThemeEngine.cpp timeValue(), and moonAge() for 0 to 29).
        ranges = {'second': (0, 59), 'minute': (0, 59), 'hour': (0, 23), 'hour12': (0, 11), 'secondOfDay': (0, 86399),
                  'day': (1, 31), 'month': (1, 12), 'weekday': (0, 6), 'moon': (0, 29)}
        self.assertEqual(stheme.TIME_RANGES, ranges)
        for name, (low, high) in ranges.items():
            rule = lambda *ends, **kw: self.theme([dict(TIME_SHOW, source=f'time.{name}', **kw, **{'in': [list(ends)]})])
            where = f'layers[0].show[0].in[0]'
            for ends in ((low, high), (low - 5, low), (high, high + 5), (None, low), (high, None), (low, low), (high, high)):
                self.assertEqual(stheme.warnings(rule(*ends)), [], (name, ends))
            for ends in ((high + 1, high + 5), (low - 5, low - 1), (high + 1, None), (None, low - 1)):
                warning = stheme.warnings(rule(*ends))
                self.assertEqual(len(warning), 1, (name, ends))
                self.assertTrue(warning[0].startswith(f'{where}: can never match: time.{name} is {low} to {high}'), warning)
                self.assertEqual(stheme.validate(rule(*ends)), [], 'a warning does not refuse the theme')
        # hour12 is 0 to 11, unlike the {hh} token: noon is 0.
        self.assertEqual(len(stheme.warnings(self.theme([{'source': 'time.hour12', 'in': [[12, 12]]}]))), 1)
        self.assertEqual(len(stheme.warnings(self.theme([{'source': 'time.weekday', 'in': [[7, 7]]}]))), 1)

    def test_warns_by_the_values_of_every(self):
        turn = lambda every, *ends, source='time.second': self.theme([dict(TIME_SHOW, source=source, every=every, **{'in': [list(ends)]})])
        self.assertEqual(len(stheme.warnings(turn(10, 10, 20))), 1)
        self.assertIn('every 10 gives 0 to 9', stheme.warnings(turn(10, 10, 20))[0])
        self.assertEqual(stheme.warnings(turn(10, 9, 20)), [])
        self.assertEqual(stheme.warnings(turn(10, 0, 4)), [])
        # The values reach N-1 only when the source reaches it: time.weekday every 10 is still 0 to 6.
        self.assertEqual(len(stheme.warnings(turn(10, 7, 9, source='time.weekday'))), 1)
        self.assertEqual(stheme.warnings(turn(10, 6, 9, source='time.weekday')), [])
        # With a big modulus the source's own range is the limit (hour 0-23 stays below 3600).
        self.assertEqual(len(stheme.warnings(turn(3600, 24, 100, source='time.hour'))), 1)
        # A modulus right at the source's top: every 59 on seconds gives 0 to 58, every 60 gives 0 to 59.
        self.assertEqual(len(stheme.warnings(turn(59, 59, 59))), 1)
        self.assertEqual(stheme.warnings(turn(60, 59, 59)), [])
        self.assertEqual(len(stheme.warnings(turn(61, 60, 60))), 1)
        self.assertNotIn('every', stheme.warnings(turn(61, 60, 60))[0])
        # Closest to the edge: every 2 gives 0 and 1.
        self.assertEqual(len(stheme.warnings(turn(2, 2, 2))), 1)
        self.assertEqual(stheme.warnings(turn(2, 1, 2)), [])

    def test_warnings_ignore_data_rules_and_invalid_rules(self):
        self.assertEqual(stheme.warnings(self.theme([dict(SHOW, **{'in': [[500, 600]]})])), [])
        self.assertEqual(stheme.warnings(self.theme([dict(TIME_SHOW, every=1, **{'in': [[500, 600]]})])), [])
        self.assertEqual(stheme.warnings(self.theme([dict(TIME_SHOW, source='time.nope', **{'in': [[500, 600]]})])), [])
        self.assertEqual(stheme.warnings(self.theme([dict(TIME_SHOW, **{'in': [[5, 1]]})])), [])
        self.assertEqual(stheme.warnings(self.theme([dict(TIME_SHOW, **{'in': 7})])), [])
        self.assertEqual(stheme.warnings({'layers': 3}), [])
        self.assertEqual(stheme.warnings(self.theme([dict(TIME_SHOW, **{'in': [['a', 1]]})])), [])
        self.assertEqual(stheme.warnings(self.theme([dict(TIME_SHOW, **{'in': [[0, 'b']]})])), [])
        # One bad range among good ones is named by its own index.
        two = self.theme([dict(TIME_SHOW, **{'in': [[0, 4], [90, 99]]})])
        self.assertEqual([w.split(':')[0] for w in stheme.warnings(two)], ['layers[0].show[0].in[1]'])
        # And so is its rule, and its layer.
        never = dict(TIME_SHOW, **{'in': [[90, 99]]})
        later = manifest(layers=[dict(TEXT), dict(TEXT, show=[TIME_SHOW, never])])
        self.assertEqual([w.split(':')[0] for w in stheme.warnings(later)], ['layers[1].show[1].in[0]'])

    def test_the_command_line_prints_the_warnings_and_goes_on(self):
        def run(*args):
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                code = stheme.main([str(a) for a in args])
            return code, out.getvalue(), err.getvalue()
        text = 'warning: layers[0].show[0].in[0]: can never match: time.hour12 is 0 to 11'
        with tempfile.TemporaryDirectory() as d:
            source = Path(d)
            (source/'theme.json').write_text(json.dumps(self.theme([{'source': 'time.hour12', 'in': [[12, 12]]}])))
            for args in (('validate', source), ('build', source, source/'out.stheme')):
                code, out, err = run(*args)
                self.assertEqual((code, err.strip()), (0, text), args)
            self.assertTrue((source/'out.stheme').exists())
            (source/'theme.json').write_text(json.dumps(self.theme([{'source': 'time.hour12', 'in': [[0, 11]]}])))
            for args in (('validate', source), ('build', source, source/'out.stheme')):
                self.assertEqual(run(*args)[::2], (0, ''), args)

    def test_warnings_agree_with_the_clock(self):
        # The table is the set of values stheme's own clock (the one the firmware matches) produces.
        seen = {n: [10 ** 9, -10 ** 9] for n in stheme.TIME_NAMES}
        start = dt.datetime(2026, 1, 1)
        stamps = [start + dt.timedelta(seconds=61 * i) for i in range(57000)]   # about 40 days: every moon age too
        stamps += [dt.datetime(2026, 1, 1, 23, 59, 59), dt.datetime(2026, 1, 31)]
        stamps += [dt.datetime(2026, month, 1) for month in range(1, 13)]
        for t in stamps:
            for name, bounds in seen.items():
                value = stheme.time_value(name, t)
                bounds[0], bounds[1] = min(bounds[0], value), max(bounds[1], value)
        self.assertEqual({n: tuple(b) for n, b in seen.items()}, stheme.TIME_RANGES)

    def test_spec_1_text_of_other_features_is_unchanged(self):
        # Only the show message follows the firmware's text; the rest keep the skill's own hint.
        self.assertEqual(stheme.validate(manifest(spec=1, layers=[arc()])),
                         ['layers[0]: arc shape needs "spec": 2 (or "spec": "auto")'])

    def test_auto_spec_becomes_2(self):
        self.assertEqual(stheme.needed_spec(self.theme([SHOW])), 2)
        self.assertEqual(stheme.needed_spec(manifest(data=[SOURCE])), 1)

    def test_hides_a_layer_until_every_rule_holds(self):
        both = dict(TEXT, show=[dict(SHOW, **{'in': [[0, 10], [20, None]]}),
                                {'source': 'w.y', 'in': [[None, 5]], 'missing': True}])
        data = lambda **kw: {k.replace('_', '.'): v for k, v in kw.items()}
        self.assertFalse(stheme.shown(both, {}))                          # no w.x: missing is false there
        self.assertTrue(stheme.shown(both, data(w_x='10')))               # w.y missing holds
        self.assertTrue(stheme.shown(both, data(w_x='0')))
        self.assertTrue(stheme.shown(both, data(w_x='1000', w_y='5')))    # open end, bound included
        self.assertTrue(stheme.shown(both, data(w_x='1e300')))            # too big for a float: still past 20
        self.assertFalse(stheme.shown(both, data(w_x='10.5')))            # between the ranges
        self.assertFalse(stheme.shown(both, data(w_x='5', w_y='5.5')))    # the second rule fails
        self.assertFalse(stheme.shown(both, data(w_x='abc')))
        self.assertTrue(stheme.shown(TEXT, {}))                           # no rules
        # Bounds are floats: 9.49 is the single value of [9.49, 9.49].
        precise = dict(TEXT, show=[dict(SHOW, **{'in': [[9.49, 9.49]]})])
        self.assertTrue(stheme.shown(precise, data(w_x='9.49')))
        self.assertFalse(stheme.shown(precise, data(w_x='9.5')) or stheme.shown(precise, data(w_x='9.48')))

    def test_preview_draws_only_shown_layers(self):
        box = dict(id='box', type='shape', shape='rectangle', x=10, y=10, width=5, height=5, fill='#ff0000',
                   show=[dict(SHOW, **{'in': [[0, 10]]})])
        theme = manifest(data=[SOURCE], layers=[box])
        bg, red = stheme.color565('#000000'), stheme.color565('#ff0000')
        at = lambda values: stheme.Renderer(theme, {}, 'en', values).frame(None, 0)[12 * 240 + 12]
        self.assertEqual(at({'w.x': '5'}), red)
        self.assertEqual(at({'w.x': '50'}), bg)
        self.assertEqual(at({}), bg)


class WeekTokenTests(unittest.TestCase):
    """{WW} in the skill's own preview and spec detection (no native tool needed)."""

    def test_preview_shows_the_iso_week(self):
        # Boundary dates from the firmware test, then every day of 2020-2030
        # against Python's own ISO calendar.
        for date, week in ((dt.datetime(2027, 1, 1), '53'), (dt.datetime(2024, 12, 30), '01'),
                           (dt.datetime(2023, 1, 1), '52'), (dt.datetime(2026, 10, 23), '43')):
            self.assertEqual(stheme.expand('{WW}', date, {}, 0), week, date)
        day = dt.datetime(2020, 1, 1)
        while day.year < 2031:
            self.assertEqual(stheme.expand('KW {WW}', day, {}, 0), f'KW {day.isocalendar()[1]:02d}', day)
            day += dt.timedelta(days=1)
        self.assertEqual(stheme.expand('KW {WW}', None, {}, 0), 'KW --')

    def test_week_token_needs_spec_2(self):
        theme = copy.deepcopy(BASE)
        theme['layers'][0]['value'] = 'KW {WW}'
        self.assertTrue(any('{WW}' in reason for reason in stheme.spec2_reasons(theme)))
        theme['spec'] = 1
        errors = stheme.validate(theme)
        # Refused twice, like the firmware's unknown variable: by the spec check
        # and by the text check (so {WW} is not in the spec 1 token list).
        self.assertTrue(any('needs "spec": 2' in e for e in errors), errors)
        self.assertTrue(any('declared clock/data variables only' in e for e in errors), errors)


if __name__ == '__main__':
    unittest.main()
