"""skills/smalltv-theme-clock/scripts/stheme.py is a third implementation of the
theme format, after tools/ and SmallTV Studio. These tests hold it to the
firmware: the same package bytes as tools/theme_pack.py, packages the firmware
validator accepts, preview frames equal to the native renderer's, and the same
accept/reject verdict as the firmware's manifest validator.

A change to the theme format must update stheme.py in the same PR."""
import copy
import datetime as dt
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
    'show open end': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[None, 5]], 'missing': True})])]),
    'show both null': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[None, None]]})])]),
    'show low above high': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[5, 1]]})])]),
    'show too big': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': [[0, 1e39]]})])]),
    'show 5 rules': manifest(data=[SOURCE], layers=[dict(TEXT, show=[SHOW] * 5)]),
    'show no ranges': manifest(data=[SOURCE], layers=[dict(TEXT, show=[dict(SHOW, **{'in': []})])]),
    'show 24 rules': manifest(data=[SOURCE], layers=[dict(TEXT, id=f'l{i}', show=[SHOW] * 4) for i in range(6)]),
    'show 28 rules': manifest(data=[SOURCE], layers=[dict(TEXT, id=f'l{i}', show=[SHOW] * 4) for i in range(7)]),
    'stops not increasing': layer(bind={'color': {'source': 'time.second',
                                                  'stops': [{'at': 5, 'value': '#000000'}, {'at': 5, 'value': '#ffffff'}]}}),
}


@needs_native
class ManifestParityTests(unittest.TestCase):
    def test_validators_agree(self):
        self.assertEqual(len(MANIFESTS), 47)
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
        self.assertEqual(stheme.validate(self.theme([SHOW], spec=1)), ['layers[0]: show rules needs "spec": 2 (or "spec": "auto")'])
        bad = {
            'time.second': 'layers[0].show[0].source: expected a declared data field',
            'w.other': 'layers[0].show[0].source: expected a declared data field',
        }
        for source, message in bad.items():
            self.assertEqual(stheme.validate(self.theme([dict(SHOW, source=source)])), [message])
        self.assertEqual(stheme.validate(self.theme([])), ['layers[0].show: expected 1 to 4 rules'])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, **{'in': [[None, None]]})])),
                         ['layers[0].show[0].in[0]: at most one end may be null'])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, **{'in': [[0, 1e39]]})])),
                         ['layers[0].show[0].in[0]: expected numbers from -3.4e38 to 3.4e38, or null'])
        self.assertEqual(stheme.validate(self.theme([dict(SHOW, **{'in': [[3, 2]]})])),
                         ['layers[0].show[0].in[0]: low must not be above high'])
        seven = manifest(data=[SOURCE], layers=[dict(TEXT, id=f'l{i}', show=[SHOW] * 4) for i in range(7)])
        self.assertEqual(stheme.validate(seven), ['layers[6].show: expected at most 24 rules in a theme'])

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
        self.assertFalse(stheme.shown(both, data(w_x='10.5')))            # between the ranges
        self.assertFalse(stheme.shown(both, data(w_x='5', w_y='5.5')))    # the second rule fails
        self.assertFalse(stheme.shown(both, data(w_x='abc')))
        self.assertTrue(stheme.shown(TEXT, {}))                           # no rules
        # Bounds are floats: 9.49 is the single value of [9.49, 9.49].
        precise = dict(TEXT, show=[dict(SHOW, **{'in': [[9.49, 9.49]]})])
        self.assertTrue(stheme.shown(precise, data(w_x='9.49')))
        self.assertFalse(stheme.shown(precise, data(w_x='9.5')) or stheme.shown(precise, data(w_x='9.48')))


if __name__ == '__main__':
    unittest.main()
