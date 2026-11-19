"""Frame by value (`bind.frame`, with optional `group`) in
skills/smalltv-theme-clock/scripts/stheme.py: the validator, the spec it needs,
and the frame the preview shows. The firmware side is tests/theme/test_frame.cpp;
test_skill_parity.py holds the two to the same verdicts and the same pixels."""
import copy
import datetime as dt
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'skills/smalltv-theme-clock/scripts'))
import stheme

# One flat colour per frame, so a pixel tells which frame was drawn.
COLORS = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255), (255, 255, 255), (128, 128, 128)]
DATA = [{'id': 'w', 'url': 'http://example.test/a.json', 'interval': 60,
         'fields': [{'id': 'moon', 'path': 'm'}, {'id': 'day', 'path': 'd'}]}]


def theme(frames, bind, spec=2, loop=True, fps=2):
    return {'spec': spec, 'theme': {'id': 't', 'name': 'T', 'author': 'a', 'version': '1'},
            'display': {'width': 240, 'height': 240, 'background': '#000000'}, 'data': copy.deepcopy(DATA),
            'layers': [{'id': 'a', 'type': 'animation', 'x': 0, 'y': 0, 'width': 2, 'height': 2, 'source': 'anim',
                        'frames': frames, 'fps': fps, 'loop': loop, 'bind': {'frame': bind}}]}


def linear(source='w.moon', input=(0, 29), output=(0, 7), **extra):
    return {'source': source, 'input': list(input), 'output': list(output), **extra}


def stops(pairs, source='w.moon', **extra):
    return {'source': source, 'stops': [{'at': at, 'frame': frame} for at, frame in pairs], **extra}


def errors(manifest):
    return stheme.validate(manifest)


class Preview:
    """A packed theme, and the frame its preview draws at a given moment."""

    def __init__(self, manifest, values=None):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        (root/'anim').mkdir()
        for i in range(manifest['layers'][0]['frames']):
            Image.new('RGBA', (2, 2), COLORS[i % len(COLORS)] + (255,)).save(root/'anim'/f'{i:03d}.png')
        (root/'theme.json').write_bytes(stheme.manifest_json(manifest))
        loaded, images = stheme.load_source(root)
        loaded_theme, assets = stheme.unpack(stheme.pack(loaded, images))
        self.renderer = stheme.Renderer(loaded_theme, assets, 'en', values or {})

    def frame(self, elapsed=0, clock=None):
        pixel = self.renderer.frame(clock, elapsed)[0]
        for i, color in enumerate(COLORS):
            if pixel == stheme.rgb565(*color):
                return i
        raise AssertionError(f'pixel {pixel:#06x} is no frame colour')

    def close(self):
        self.tmp.cleanup()


class FrameBindingTests(unittest.TestCase):
    def preview(self, manifest, values=None):
        shown = Preview(manifest, values)
        self.addCleanup(shown.close)
        return shown

    def frames(self, bind, value, frames=8):
        return self.preview(theme(frames, bind), {'w.moon': value} if value is not None else {}).frame()

    # ---- validation: what is accepted
    def test_accepted(self):
        for name, manifest in {
            'linear': theme(8, linear()),
            'reversed output': theme(8, linear(output=(7, 0))),
            'clamp off': theme(8, linear(clamp=False)),
            'stops': theme(3, stops([(0, 0), (4, 1), (11, 2)])),
            'one stop': theme(1, stops([(0, 0)])),
            'a built-in source': theme(2, linear('time.hour', (0, 23), (0, 1))),
            'group, linear': theme(6, linear(input=(0, 1), output=(0, 1), group=3)),
            'group, stops': theme(6, stops([(0, 0), (1, 1)], group=3)),
            'groups of one frame': theme(4, linear(input=(0, 3), output=(0, 3), group=1)),
            'one group': theme(3, linear(input=(0, 1), output=(0, 0), group=3)),
        }.items():
            with self.subTest(case=name):
                self.assertEqual(errors(manifest), [])
        sixteen = [(i, i % 4) for i in range(16)]
        self.assertEqual(errors(theme(4, stops(sixteen))), [])

    # ---- validation: what is refused, and the message
    def refused(self, manifest, expected):
        found = errors(manifest)
        self.assertTrue(any(expected in e for e in found), f'{expected!r} not in {found}')

    def test_refusals(self):
        self.refused(theme(8, linear(), spec=1), 'layers[0].bind.frame: needs "spec": 2')
        self.refused(theme(8, stops([(0, 0)]), spec=1), 'layers[0].bind.frame: needs "spec": 2')
        text = theme(8, linear())
        text['layers'][0] = {'id': 'a', 'type': 'text', 'x': 0, 'y': 0, 'value': 'x', 'size': 16, 'color': '#ffffff',
                             'bind': {'frame': linear()}}
        self.refused(text, 'layers[0].bind.frame: binding is not applicable to this layer')
        self.refused(theme(8, linear(output=(0, 8))), 'layers[0].bind.frame.output: expected exactly two integers from 0 to 7')
        self.refused(theme(8, linear(output=(-1, 7))), 'layers[0].bind.frame.output')
        self.refused(theme(8, linear(input=(5, 5))), 'layers[0].bind.frame.input: endpoints must differ')
        self.refused(theme(8, linear(clamp='yes')), 'layers[0].bind.frame.clamp')
        self.refused(theme(3, stops([(0, 3)])), 'layers[0].bind.frame.stops[0].frame')
        self.refused(theme(3, stops([(0, -1)])), 'layers[0].bind.frame.stops[0].frame')
        self.refused(theme(3, stops([(1, 0), (1, 1)])), 'layers[0].bind.frame.stops[1].at: stops must be strictly increasing')
        self.refused(theme(3, stops([(2, 0), (1, 1)])), 'stops must be strictly increasing')
        self.refused(theme(3, stops([])), 'layers[0].bind.frame.stops: expected 1 to 16 stops')
        self.refused(theme(4, stops([(i, i % 4) for i in range(17)])), 'layers[0].bind.frame.stops: expected 1 to 16 stops')
        both = linear()
        both['stops'] = [{'at': 0, 'frame': 0}]
        self.refused(theme(8, both), 'layers[0].bind.frame.input: unknown field')
        self.refused(theme(8, dict(linear(), source='w.nothing')), 'layers[0].bind.frame.source')

    def test_group_refusals(self):
        self.refused(theme(6, linear(output=(0, 0), group=4)), 'layers[0].bind.frame.group: frames must be a multiple of group')
        self.refused(theme(6, linear(output=(0, 0), group=0)), 'layers[0].bind.frame.group')
        self.refused(theme(6, linear(output=(0, 0), group=7)), 'layers[0].bind.frame.group')
        self.refused(theme(6, linear(output=(0, 0), group=1.5)), 'layers[0].bind.frame.group')
        # With a group, output and stops count groups: 6 frames of 3 are 2 groups.
        self.refused(theme(6, linear(input=(0, 1), output=(0, 2), group=3)), 'layers[0].bind.frame.output: expected exactly two integers from 0 to 1')
        self.refused(theme(6, stops([(0, 2)], group=3)), 'layers[0].bind.frame.stops[0].frame')

    # ---- the spec the build chooses
    def test_a_frame_binding_needs_spec_2(self):
        manifest = theme(8, linear())
        self.assertEqual(stheme.needed_spec(manifest), 2)
        self.assertTrue(any('frame' in reason for reason in stheme.spec2_reasons(manifest)))
        plain = theme(8, linear())
        del plain['layers'][0]['bind']
        self.assertEqual(stheme.needed_spec(plain), 1)

    # ---- the frame the preview shows
    def test_linear_mapping(self):
        self.assertEqual(self.frames(linear(), '0'), 0)
        self.assertEqual(self.frames(linear(), '29'), 7)
        self.assertEqual(self.frames(linear(), '15'), 4)      # 3.62 rounds to 4
        self.assertEqual(self.frames(linear(), '14'), 3)      # 3.38 rounds to 3
        self.assertEqual(self.frames(linear(), '40'), 7)      # clamped
        self.assertEqual(self.frames(linear(), '-3'), 0)
        self.assertEqual(self.frames(linear(output=(7, 0)), '0'), 7)
        self.assertEqual(self.frames(linear(clamp=False), '40'), 7)    # still no frame past the last

    def test_stops(self):
        bind = stops([(0, 0), (4, 1), (11, 2)])
        for value, frame in (('-5', 0), ('0', 0), ('3.9', 0), ('4', 1), ('10.9', 1), ('11', 2), ('99', 2)):
            with self.subTest(value=value):
                self.assertEqual(self.frames(bind, value, 3), frame)

    def test_below_the_first_stop_shows_the_first_stops_frame(self):
        # The first stop is not at frame 0, so "frame 0" and "the first stop's frame" differ.
        bind = stops([(5, 2), (10, 1)])
        for value, frame in (('-3', 2), ('4.9', 2), ('5', 2), ('10', 1)):
            with self.subTest(value=value):
                self.assertEqual(self.frames(bind, value, 3), frame)

    def test_no_usable_value_shows_frame_0(self):
        self.assertEqual(self.frames(linear(output=(7, 7)), None), 0)
        self.assertEqual(self.frames(linear(output=(7, 7)), 'cloudy'), 0)
        self.assertEqual(self.frames(stops([(0, 2)]), None, 3), 0)

    def test_a_bound_layer_does_not_play(self):
        shown = self.preview(theme(8, linear()), {'w.moon': '15'})
        self.assertEqual({shown.frame(ms) for ms in (0, 500, 1000, 12345)}, {4})

    def test_a_clock_source_and_no_clock(self):
        manifest = theme(2, linear('time.hour', (0, 23), (0, 1)))
        shown = self.preview(manifest)
        self.assertEqual(shown.frame(0, dt.datetime(2026, 9, 17, 0, 30)), 0)
        self.assertEqual(shown.frame(0, dt.datetime(2026, 9, 17, 23, 30)), 1)
        self.assertEqual(shown.frame(0, None), 0)

    # ---- groups that play
    def test_a_group_plays_and_wraps(self):
        for day, first in (('0', 0), ('1', 3)):
            shown = self.preview(theme(6, linear('w.day', (0, 1), (0, 1), group=3)), {'w.day': day})
            self.assertEqual([shown.frame(ms) for ms in (0, 400, 500, 1000, 1500, 2000)],
                             [first, first, first + 1, first + 2, first, first + 1])

    def test_a_group_plays_once_without_loop(self):
        shown = self.preview(theme(6, linear('w.day', (0, 1), (0, 1), group=3), loop=False), {'w.day': '1'})
        self.assertEqual([shown.frame(ms) for ms in (0, 500, 1000, 1500, 9000)], [3, 4, 5, 5, 5])

    def test_groups_of_one_frame_do_not_play(self):
        shown = self.preview(theme(4, linear('w.day', (0, 3), (0, 3), group=1)), {'w.day': '2'})
        self.assertEqual({shown.frame(ms) for ms in (0, 500, 3000)}, {2})

    def test_no_usable_value_plays_group_0(self):
        shown = self.preview(theme(6, linear('w.day', (0, 1), (0, 1), group=3)))
        self.assertEqual([shown.frame(ms) for ms in (0, 500, 1000, 1500)], [0, 1, 2, 0])

    def test_an_open_input_still_ends_on_the_last_group(self):
        # clamp false lets the input extrapolate; the group still stops at
        # frames / group - 1. Group 5 of 6 frames in groups of 3 would be frame
        # 15, a picture that does not exist.
        bind = linear('w.day', (0, 1), (0, 1), group=3, clamp=False)
        for value, frame in (('5', 3), ('2', 3), ('1e9', 3), ('1', 3), ('0', 0), ('-5', 0), ('-1e9', 0)):
            with self.subTest(value=value):
                self.assertEqual(self.preview(theme(6, bind), {'w.day': value}).frame(), frame)
        steep = linear('w.day', (0, 10), (1, 2), group=2, clamp=False)
        for value, frame in (('10', 4), ('20', 6), ('100', 6), ('-100', 0)):
            with self.subTest(value=value):
                self.assertEqual(self.preview(theme(8, steep), {'w.day': value}).frame(), frame)
        single = linear('w.day', (0, 5), (0, 5), group=1, clamp=False)
        self.assertEqual(self.preview(theme(6, single), {'w.day': '50'}).frame(), 5)

    def test_a_new_group_starts_at_its_first_frame(self):
        # time.hour picks the group; the group changes at 1000 ms, a frame
        # clock started then: not the position the old group had reached.
        shown = self.preview(theme(6, linear('time.hour', (0, 1), (0, 1), group=3)))
        night, day = dt.datetime(2026, 9, 17, 0, 0), dt.datetime(2026, 9, 17, 1, 0)
        self.assertEqual([shown.frame(0, night), shown.frame(500, night)], [0, 1])
        self.assertEqual(shown.frame(1000, day), 3)     # a carried-over position would be 5
        self.assertEqual(shown.frame(1500, day), 4)
        self.assertEqual(shown.frame(2000, day), 5)
        self.assertEqual(shown.frame(2500, day), 3)
        self.assertEqual(shown.frame(3000, night), 0)   # and back, again from the first frame
        self.assertEqual(shown.frame(3500, night), 1)


if __name__ == '__main__':
    unittest.main()
