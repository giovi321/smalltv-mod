#!/usr/bin/env python3
"""SmallTV theme clocks (.stheme) without the firmware toolchain.

A pure-Python port of SmallTV Studio's core (validator, packer, RGB565
renderer) extended with the spec 2 features of smalltv-mod: arcs, hands, tick
marks, time.* / device.* sources, {H} {MO} {AMPM} and localized text. Needs
Python 3.9+ and Pillow, nothing else.

  stheme.py scaffold DIR [--id ID --name NAME --author A --palette night]
  stheme.py validate SOURCE_DIR_OR_STHEME
  stheme.py build SOURCE_DIR OUT.stheme
  stheme.py preview SOURCE_DIR_OR_STHEME OUT_BASE [--time ISO --seconds N --fps N
                    --data src.field=value --lang en --unsynced --scale 2]
  stheme.py image IN OUT.png [--size WxH --fit cover|contain|stretch --pixel]
  stheme.py gif IN.gif OUT_DIR [--size WxH --fit ... --max-frames 60]
  stheme.py install PKG.stheme --host IP [--password P --user admin --activate]
  stheme.py palette [NAME]

The packages it writes are byte-identical to the firmware's
tools/smalltv_theme.py build for the same source folder.
"""
import argparse
import datetime as dt
import decimal
import json
import math
import re
import struct
import sys
import uuid
from pathlib import Path, PurePosixPath

try:
    from PIL import Image
except ImportError:  # validate/build of shape-only themes still work
    Image = None

# --------------------------------------------------------------------------
# Constants and palettes
# --------------------------------------------------------------------------
MAX_PACKAGE, MAX_ENTRIES, MAX_MANIFEST = 3 * 1024 * 1024, 256, 16384
MAX_LAYERS, MAX_BINDINGS, MAX_STOPS, MAX_TRANSLATION_BYTES = 32, 8, 8, 1024
ANCHORS = ['top-left', 'top-center', 'top-right', 'center-left', 'center', 'center-right',
           'bottom-left', 'bottom-center', 'bottom-right']
TOKENS = ['HH', 'hh', 'MM', 'SS', 'DD', 'MON', 'MONTH', 'WD', 'WEEKDAY', 'YYYY']
TOKENS_SPEC2 = ['H', 'MO', 'AMPM']
DEVICE_NAMES = ['focusToday', 'focusGoal', 'streak', 'humidity', 'temperature']
TIME_NAMES = ['second', 'minute', 'hour', 'hour12', 'secondOfDay', 'day', 'month', 'weekday', 'moon']
LANGS = ['en', 'pt', 'es', 'fr', 'de', 'it']
ROTATION = ('arc', 'hand', 'ticks')

PALETTES = {
    'night': {  # SmallTV Night, the skill's default (matches examples/themes/live-status)
        'background': '#0b1420', 'surface': '#16202c', 'line': '#233145', 'text': '#e9edf4',
        'muted': '#7f93ab', 'accent': '#47c2ff', 'ok': '#35c46a', 'warn': '#f0b429', 'bad': '#e5484d'},
    'terminal': {
        'background': '#071117', 'surface': '#0b1b22', 'line': '#22535a', 'text': '#8ed1c4',
        'muted': '#22535a', 'accent': '#39e6a5', 'ok': '#39e6a5', 'warn': '#f4d06f', 'bad': '#d96b6b'},
    'mono': {
        'background': '#000000', 'surface': '#1a1a1a', 'line': '#333333', 'text': '#ffffff',
        'muted': '#8a8a8a', 'accent': '#ff5a47', 'ok': '#ffffff', 'warn': '#ffb020', 'bad': '#ff5a47'},
}

MONTHS = [
    ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"],
    ["Janeiro", "Fevereiro", "Marco", "Abril", "Maio", "Junho", "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro"],
    ["Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"],
    ["Janvier", "Fevrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Aout", "Septembre", "Octobre", "Novembre", "Decembre"],
    ["Januar", "Februar", "Maerz", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"],
    ["Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre"]]
MONTHS_SHORT = [
    ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"],
    ["Jan", "Fev", "Mar", "Abr", "Mai", "Jun", "Jul", "Ago", "Set", "Out", "Nov", "Dez"],
    ["Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"],
    ["Janv", "Fevr", "Mars", "Avr", "Mai", "Juin", "Juil", "Aout", "Sept", "Oct", "Nov", "Dec"],
    ["Jan", "Feb", "Mar", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"],
    ["Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"]]
DAYS = [  # Sunday first, like tm_wday
    ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"],
    ["Domingo", "Segunda-feira", "Terca-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sabado"],
    ["Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"],
    ["Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"],
    ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"],
    ["Domenica", "Lunedi", "Martedi", "Mercoledi", "Giovedi", "Venerdi", "Sabato"]]
DAYS_SHORT = [
    ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"],
    ["Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"],
    ["Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"],
    ["Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"],
    ["Son", "Mon", "Die", "Mit", "Don", "Fre", "Sam"],
    ["Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"]]

# Classic Adafruit GFX 5x8 font, ASCII 0-126 (see FONT-LICENSE.txt), the
# firmware's built-in font: 5 column bytes per glyph, bit n = row n.
FONT = bytes.fromhex(
    "00000000003e5b4f5b3e3e6b4f6b3e1c3e7c3e1c183c7e3c181c577d571c1c5e7f5e1c00183c1800ffe7c3e7ff001824"
    "1800ffe7dbe7ff30483a060e2629792926407f050507407f05253f5a3ce73c5a7f3e1c1c08081c1c3e7f14227f22145f"
    "5f005f5f06097f017f006689956a606060606094a2ffa29408047e040810207e201008082a1c08081c2a08081e101010"
    "100c1e0c1e0c30383e3830060e3e0e06000000000000005f00000007000700147f147f14242a7f2a1223130864623649"
    "5620500008070300001c2241000041221c002a1c7f1c2a08083e08080080703000080808080800006060002010080402"
    "3e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949"
    "291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e"
    "414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040"
    "407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f20"
    "40201f3f4038403f631408146303047804036159494d43007f4141410204081020004141417f04020102044040404040"
    "000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c787f0804047800447d"
    "40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c0804040848"
    "5454542404043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c44000836410000007700"
    "0000413608000201020402"
)

# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------
ID_RE = re.compile(r'^[A-Za-z0-9_-]{1,48}$')
COLOR_RE = re.compile(r'^#[0-9a-fA-F]{6}$')


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def color565(s):
    n = int(s[1:], 16)
    return rgb565(n >> 16, (n >> 8) & 255, n & 255)


def rgb888(c):
    return ((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def is_int(v):
    return type(v) is int


def is_num(v):
    return type(v) in (int, float) and math.isfinite(v)


def path_ok(s):
    return (isinstance(s, str) and 0 < len(s) <= 120 and re.fullmatch(r'[A-Za-z0-9_./-]+', s) is not None
            and all(p not in ('', '.', '..') for p in s.split('/')))


def nbytes(s):
    return len(s.encode('utf-8'))


def manifest_json(theme):
    """The packaged theme.json, exactly as tools/theme_pack.py writes it."""
    return json.dumps(theme, separators=(',', ':'), ensure_ascii=True).encode('utf-8')


def depth(v):
    if isinstance(v, dict):
        return 1 + max([0] + [depth(x) for x in v.values()])
    if isinstance(v, list):
        return 1 + max([0] + [depth(x) for x in v])
    return 0


def asset_path(layer, frame=0):
    src = layer['source']
    if src.endswith('.sti'):
        src = src[:-4]
    return src + '.sti' if layer['type'] == 'image' else f'{src}/{frame:03d}.png.sti'


def frame_paths(layer):
    if layer.get('type') == 'image':
        return [asset_path(layer)]
    if layer.get('type') == 'animation':
        return [asset_path(layer, i) for i in range(min(240, max(0, layer.get('frames') or 0)))]
    return []


def data_fields(theme):
    out = []
    for s in theme.get('data') or []:
        if isinstance(s, dict):
            for f in s.get('fields') or []:
                if isinstance(f, dict):
                    out.append(f'{s.get("id")}.{f.get("id")}')
    return out


def text_values(layer):
    v = layer.get('value')
    return list(v.values()) if isinstance(v, dict) else [v]


# --------------------------------------------------------------------------
# Spec detection
# --------------------------------------------------------------------------
def spec2_reasons(theme):
    """Every feature of the manifest that only a "spec": 2 theme may use."""
    reasons = []
    own_device = any(isinstance(s, dict) and s.get('id') == 'device' for s in theme.get('data') or [])
    for i, s in enumerate(theme.get('data') if isinstance(theme.get('data'), list) else []):
        for j, f in enumerate(s.get('fields') if isinstance(s, dict) and isinstance(s.get('fields'), list) else []):
            if isinstance(f, dict) and 'decimals' in f:
                reasons.append(f'data[{i}].fields[{j}].decimals: decimals')
    for i, l in enumerate(theme.get('layers') or []):
        if not isinstance(l, dict):
            continue
        p = f'layers[{i}]'
        if l.get('type') == 'shape' and l.get('shape') in ROTATION:
            reasons.append(f'{p}: {l["shape"]} shape')
        if l.get('type') == 'text':
            if isinstance(l.get('value'), dict):
                reasons.append(f'{p}.value: text per language')
            for v in text_values(l):
                for tok in re.findall(r'\{([^{}]*)\}', v if isinstance(v, str) else ''):
                    if tok in TOKENS_SPEC2 or (tok.startswith('device.') and not own_device):
                        reasons.append(f'{p}.value: {{{tok}}}')
        for k, b in (l.get('bind') or {}).items() if isinstance(l.get('bind'), dict) else []:
            src = b.get('source') if isinstance(b, dict) else None
            if isinstance(src, str) and (src.startswith('time.') or src.startswith('device.')):
                reasons.append(f'{p}.bind.{k}: {src}')
    return reasons


def needed_spec(theme):
    return 2 if spec2_reasons(theme) else 1


# --------------------------------------------------------------------------
# Validation (Studio's rules + the firmware's spec 2 rules)
# --------------------------------------------------------------------------
NUMERIC_RANGE = {
    'x': (-240, 479), 'y': (-240, 479), 'x2': (-240, 479), 'y2': (-240, 479),
    'width': (0, 240), 'height': (0, 240), 'radius': (0, 240), 'cornerRadius': (0, 120),
    'size': (8, 96), 'strokeWidth': (1, 32), 'scroll.width': (1, 240), 'scroll.speed': (1, 240),
    'sweep': (-360, 360), 'angle': (-360, 360)}
COLOR_TARGETS = ('color', 'fill', 'stroke')


def applicable(l, target):
    t, shape = l.get('type'), l.get('shape')
    if target == 'color':
        return t == 'text'
    if target == 'fill':
        return t == 'shape' and l.get('fill') is not None
    if target == 'stroke':
        return t == 'shape' and l.get('stroke') is not None
    if target in ('x', 'y'):
        return True
    if target in ('scroll.width', 'scroll.speed'):
        return t == 'text' and l.get('scroll') is not None
    if t == 'text':
        return target == 'size'
    if t != 'shape':
        return False
    if target == 'strokeWidth':
        return True
    return {'rectangle': ('width', 'height', 'cornerRadius'), 'circle': ('radius',), 'arc': ('radius', 'sweep'),
            'hand': ('angle',), 'ticks': ('radius',), 'line': ('x2', 'y2')}.get(shape, ()).__contains__(target)


def valid_text(s, fields, spec, own_device):
    if not isinstance(s, str) or re.search(r'[^\x20-\x7e]', s):
        return False
    i = 0
    while i < len(s):
        if s[i] == '}':
            return False
        if s[i] == '{':
            end = s.find('}', i)
            if end < 0:
                return False
            tok = s[i + 1:end]
            ok = tok in TOKENS or tok in fields
            if spec >= 2 and (tok in TOKENS_SPEC2 or (tok.startswith('device.') and tok[7:] in DEVICE_NAMES)):
                ok = True
            if not ok:
                return False
            i = end
        i += 1
    return True


def translation_cost(text):
    return 28 + (len(text) + 1 if len(text) > 15 else 0)


def validate(theme, assets=None):
    """Every problem as 'path: reason'. `assets` maps compiled paths to (w, h, ...)."""
    errors = []

    def fail(p, why):
        errors.append(f'{p}: {why}')

    def obj(o, p, allowed):
        if not isinstance(o, dict):
            fail(p, 'expected an object')
            return False
        for k in o:
            if k not in allowed:
                fail(f'{p}.{k}', 'unknown field')
        return True

    def string(o, k, p, mx, optional=False):
        v = o.get(k)
        if v is None and optional:
            return
        if not isinstance(v, str) or not 1 <= nbytes(v) <= mx or re.search(r'[\x00-\x1f\x7f]', v):
            fail(f'{p}.{k}', f'expected 1-{mx} bytes without control characters')

    def number(o, k, p, lo, hi, optional=False):
        if o.get(k) is None and optional:
            return
        if not is_int(o.get(k)) or not lo <= o[k] <= hi:
            fail(f'{p}.{k}', f'expected an integer from {lo} to {hi}')

    def color(o, k, p):
        if not isinstance(o.get(k), str) or not COLOR_RE.match(o[k]):
            fail(f'{p}.{k}', 'expected a color in #RRGGBB form')

    if not obj(theme, 'theme.json', ['spec', 'theme', 'display', 'layers', 'data']):
        return errors
    if len(manifest_json(theme)) > MAX_MANIFEST:
        fail('theme.json', 'exceeds 16 KiB')
    if depth(theme) > 8:
        fail('theme.json', 'exceeds 8 levels of nesting')
    spec = theme.get('spec')
    if not is_int(spec) or spec not in (1, 2):
        fail('spec', 'expected 1 or 2')
        spec = 2
    if spec == 1:
        for r in spec2_reasons(theme):
            fail(r.split(':')[0], r.split(': ', 1)[1] + ' needs "spec": 2 (or "spec": "auto")')
    if obj(theme.get('theme'), 'theme', ['id', 'name', 'author', 'version']):
        for k, mx in (('id', 48), ('name', 96), ('author', 96), ('version', 32)):
            string(theme['theme'], k, 'theme', mx)
        if not ID_RE.match(str(theme['theme'].get('id', ''))):
            fail('theme.id', 'use 1-48 letters, digits, - or _')
    if obj(theme.get('display'), 'display', ['width', 'height', 'background']):
        number(theme['display'], 'width', 'display', 240, 240)
        number(theme['display'], 'height', 'display', 240, 240)
        color(theme['display'], 'background', 'display')

    fields = []
    own_device = False
    if theme.get('data') is not None:
        data = theme['data']
        if not isinstance(data, list) or len(data) > 4:
            fail('data', 'expected an array with at most 4 sources')
        seen = set()
        for i, s in enumerate(data if isinstance(data, list) else []):
            p = f'data[{i}]'
            if isinstance(s, dict) and 'id' in s:
                if s['id'] in seen:
                    fail(f'{p}.id', 'duplicate data source ID')
                seen.add(s['id'])
                if spec >= 2 and s['id'] in ('time', 'device'):
                    fail(f'{p}.id', 'time and device are the names of built-in sources')
                own_device = own_device or s['id'] == 'device'
            if not obj(s, p, ['id', 'url', 'interval', 'insecureTls', 'fields']):
                continue
            string(s, 'id', p, 32)
            string(s, 'url', p, 200)
            number(s, 'interval', p, 10, 86400)
            if not ID_RE.match(str(s.get('id', ''))):
                fail(f'{p}.id', 'use letters, digits, - or _')
            if s.get('insecureTls') is not None and not isinstance(s['insecureTls'], bool):
                fail(f'{p}.insecureTls', 'expected true or false')
            url = s.get('url')
            if isinstance(url, str):
                if not re.match(r'^https?://[^/?#]', url):
                    fail(f'{p}.url', 'expected an http:// or https:// URL that includes a host')
                elif url.startswith('https:') and s.get('insecureTls') is not True:
                    fail(f'{p}.insecureTls', 'set to true: HTTPS certificates cannot be verified on the device')
            fl = s.get('fields')
            if not isinstance(fl, list) or not 1 <= len(fl) <= 8:
                fail(f'{p}.fields', 'expected 1 to 8 fields')
                continue
            ids = set()
            for j, f in enumerate(fl):
                fp = f'{p}.fields[{j}]'
                if not obj(f, fp, ['id', 'path', 'decimals']):
                    continue
                string(f, 'id', fp, 24)
                string(f, 'path', fp, 96)
                number(f, 'decimals', fp, 0, 3, optional=True)
                if not ID_RE.match(str(f.get('id', ''))):
                    fail(f'{fp}.id', 'use letters, digits, - or _')
                if f.get('id') in ids:
                    fail(f'{fp}.id', 'duplicate field ID')
                ids.add(f.get('id'))
                if isinstance(f.get('path'), str) and not re.fullmatch(r'[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)*', f['path']):
                    fail(f'{fp}.path', 'use a dotted JSON object path')
                fields.append(f'{s.get("id")}.{f.get("id")}')

    layers = theme.get('layers')
    if not isinstance(layers, list) or len(layers) > MAX_LAYERS:
        fail('layers', 'expected an array with at most 32 layers')
        return errors
    ids, translation_bytes = set(), 0
    for i, o in enumerate(layers):
        p = f'layers[{i}]'
        if not isinstance(o, dict):
            fail(p, 'expected an object')
            continue
        if not isinstance(o.get('id'), str) or not ID_RE.match(o['id']) or o['id'] in ids:
            fail(f'{p}.id', 'expected a unique ID (letters, digits, - or _)')
        ids.add(o.get('id'))
        number(o, 'x', p, -240, 479)
        number(o, 'y', p, -240, 479)
        allowed = ['id', 'type', 'x', 'y', 'scroll', 'bind']
        t = o.get('type')
        if t == 'text':
            allowed += ['anchor', 'value', 'size', 'color']
            number(o, 'size', p, 8, 96)
            color(o, 'color', p)
            v = o.get('value')
            if isinstance(v, dict) and spec >= 2:
                if 'en' not in v:
                    fail(f'{p}.value', 'needs an "en" text, shown for every other language')
                for k, text in v.items():
                    if k not in LANGS:
                        fail(f'{p}.value', f'unknown language {k}, expected en, pt, es, fr, de or it')
                    elif not isinstance(text, str) or not 1 <= len(text) <= 128:
                        fail(f'{p}.value.{k}', 'each language expects 1 to 128 bytes')
                    elif not valid_text(text, fields, spec, own_device):
                        fail(f'{p}.value.{k}', 'use printable ASCII and declared clock/data variables only')
                    elif k != 'en':
                        translation_bytes += translation_cost(text)
                if translation_bytes > MAX_TRANSLATION_BYTES:
                    fail(f'{p}.value', 'the translations of a theme share 1024 bytes of RAM')
            elif not isinstance(v, dict):
                string(o, 'value', p, 128)
                if not valid_text(v, fields, spec, own_device):
                    fail(f'{p}.value', 'use printable ASCII and declared clock/data variables only')
            if o.get('anchor') is not None and o['anchor'] not in ANCHORS:
                fail(f'{p}.anchor', 'unsupported anchor')
        elif t in ('image', 'animation'):
            allowed.append('source')
            string(o, 'source', p, 110)
            if not path_ok(o.get('source')):
                fail(f'{p}.source', 'expected a safe relative path')
            if t == 'animation':
                allowed += ['width', 'height', 'frames', 'fps', 'loop']
                number(o, 'width', p, 1, 240)
                number(o, 'height', p, 1, 240)
                number(o, 'frames', p, 1, 240)
                number(o, 'fps', p, 1, 15)
                if not isinstance(o.get('loop'), bool):
                    fail(f'{p}.loop', 'expected true or false')
            if isinstance(o.get('source'), str):
                last = asset_path(o, max(0, (o.get('frames') or 1) - 1) if t == 'animation' else 0)
                if len(last) > 120:
                    fail(f'{p}.source', 'compiled asset path exceeds 120 bytes')
                if assets is not None:
                    for path in frame_paths(o):
                        a = assets.get(path)
                        if a is None:
                            fail(f'{p}.source', f'missing asset {path}')
                            break
                        if t == 'animation' and (a[0], a[1]) != (o.get('width'), o.get('height')):
                            fail(f'{p}.source', 'frame dimensions must match width and height')
                            break
        elif t == 'shape':
            shape = o.get('shape')
            allowed += ['shape', 'stroke', 'strokeWidth']
            number(o, 'strokeWidth', p, 1, 32, True)
            if shape == 'rectangle':
                allowed += ['width', 'height', 'fill', 'cornerRadius']
                number(o, 'width', p, 1, 240)
                number(o, 'height', p, 1, 240)
                number(o, 'cornerRadius', p, 0, 120, True)
            elif shape == 'circle':
                allowed += ['radius', 'fill']
                number(o, 'radius', p, 1, 240)
            elif shape == 'line':
                allowed += ['x2', 'y2']
                number(o, 'x2', p, -240, 479)
                number(o, 'y2', p, -240, 479)
            elif shape == 'arc':
                allowed += ['radius', 'start', 'sweep']
                number(o, 'radius', p, 1, 240)
                number(o, 'start', p, -360, 360)
                number(o, 'sweep', p, -360, 360)
            elif shape == 'hand':
                allowed += ['length', 'tail', 'angle']
                number(o, 'length', p, 1, 240)
                number(o, 'tail', p, 0, 240, True)
                number(o, 'angle', p, -360, 360)
            elif shape == 'ticks':
                allowed += ['radius', 'count', 'length']
                number(o, 'radius', p, 1, 240)
                number(o, 'count', p, 1, 60)
                number(o, 'length', p, 1, 240)
            else:
                fail(f'{p}.shape', 'expected rectangle, circle, line, arc, hand or ticks')
            if shape in ROTATION or shape == 'line':
                if o.get('stroke') is None:
                    fail(f'{p}.stroke', f'required: a {shape} is drawn in stroke only' if shape != 'line' else 'required for a line')
            elif o.get('fill') is None and o.get('stroke') is None:
                fail(p, 'provide a fill or stroke color')
            if o.get('fill') is not None and shape not in ROTATION:
                color(o, 'fill', p)
            if o.get('stroke') is not None:
                color(o, 'stroke', p)
        else:
            fail(f'{p}.type', 'expected text, image, animation or shape')
        obj(o, p, allowed)
        sc = o.get('scroll')
        if sc is not None:
            if t != 'text':
                fail(f'{p}.scroll', 'only allowed on text layers')
            elif obj(sc, f'{p}.scroll', ['width', 'mode', 'speed', 'pause', 'gap']):
                sp = f'{p}.scroll'
                number(sc, 'width', sp, 1, 240)
                number(sc, 'speed', sp, 1, 240)
                number(sc, 'pause', sp, 0, 10000, True)
                number(sc, 'gap', sp, 0, 240, True)
                if sc.get('mode') not in ('loop', 'bounce'):
                    fail(f'{sp}.mode', 'expected loop or bounce')
                elif sc['mode'] == 'bounce' and 'gap' in sc:
                    fail(f'{sp}.gap', 'not allowed in bounce mode')
        if o.get('bind') is not None:
            validate_bindings(o, p, fields, spec, fail, obj, color)
    return errors


def validate_bindings(o, p, fields, spec, fail, obj, color):
    bp = f'{p}.bind'
    if not isinstance(o['bind'], dict):
        fail(bp, 'expected an object')
        return
    if len(o['bind']) > MAX_BINDINGS:
        fail(bp, f'expected at most {MAX_BINDINGS} bindings')
    for target, b in o['bind'].items():
        tp = f'{bp}.{target}'
        is_color = target in COLOR_TARGETS
        if not is_color and target not in NUMERIC_RANGE:
            fail(tp, 'unknown binding target')
            continue
        if not applicable(o, target):
            fail(tp, 'binding is not applicable to this layer')
            continue
        if not obj(b, tp, ['source', 'stops'] if is_color else ['source', 'input', 'output', 'clamp']):
            continue
        src = b.get('source')
        builtin = isinstance(src, str) and (src.startswith('time.') or src.startswith('device.'))
        if builtin and src not in fields:
            if spec < 2:
                fail(f'{tp}.source', 'time.* and device.* sources need "spec": 2')
            elif not ((src.startswith('time.') and src[5:] in TIME_NAMES) or (src.startswith('device.') and src[7:] in DEVICE_NAMES)):
                fail(f'{tp}.source', 'unknown time.* or device.* name')
        elif not isinstance(src, str) or src not in fields:
            fail(f'{tp}.source', 'expected a declared data field')
        if is_color:
            stops = b.get('stops')
            if not isinstance(stops, list) or not 1 <= len(stops) <= MAX_STOPS:
                fail(f'{tp}.stops', f'expected 1 to {MAX_STOPS} stops')
                continue
            for i, stop in enumerate(stops):
                sp = f'{tp}.stops[{i}]'
                if not obj(stop, sp, ['at', 'value']):
                    continue
                if not is_num(stop.get('at')):
                    fail(f'{sp}.at', 'expected a finite number')
                elif i and isinstance(stops[i - 1], dict) and is_num(stops[i - 1].get('at')) and stop['at'] <= stops[i - 1]['at']:
                    fail(f'{sp}.at', 'stops must be strictly increasing')
                color(stop, 'value', sp)
            continue
        lo, hi = NUMERIC_RANGE[target]
        inp, out = b.get('input'), b.get('output')
        if not isinstance(inp, list) or len(inp) != 2 or not all(is_num(n) for n in inp):
            fail(f'{tp}.input', 'expected exactly two finite numbers')
        elif inp[0] == inp[1]:
            fail(f'{tp}.input', 'endpoints must differ')
        if not isinstance(out, list) or len(out) != 2 or not all(is_int(n) and lo <= n <= hi for n in out):
            fail(f'{tp}.output', f'expected exactly two integers from {lo} to {hi}')
        if b.get('clamp') is not None and not isinstance(b['clamp'], bool):
            fail(f'{tp}.clamp', 'expected true or false')


# --------------------------------------------------------------------------
# STI images (encoders copied from smalltv-mod tools/theme_pack.py)
# --------------------------------------------------------------------------
def rgba_pixels(rgba):
    raw = rgba.tobytes()
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


def image_rgba(image):
    w, h = image.size
    if not (1 <= w <= 240 and 1 <= h <= 240):
        raise ValueError('Image dimensions must be 1..240 pixels')
    rgba = image.convert('RGBA')
    return rgba, rgba.getextrema()[3][0] != 255


def encode_sti1(image):
    rgba, has_alpha = image_rgba(image)
    w, h = rgba.size
    data = bytearray(b'STI1' + struct.pack('<HHB3x', w, h, int(has_alpha)))
    for r, g, b, a in rgba_pixels(rgba):
        data.extend(struct.pack('<H', rgb565(r, g, b)))
        if has_alpha:
            data.append(a)
    return bytes(data)


def row_runs(row, min_repeat):
    runs, literal = [], []

    def add(kind, pixels):
        for start in range(0, len(pixels), 64):
            runs.append((kind, pixels[start:start + 64]))

    i = 0
    while i < len(row):
        j = i
        while j < len(row) and row[j] == row[i]:
            j += 1
        if row[i] is None or j - i >= min_repeat:
            add(2, literal)
            literal = []
            add(0 if row[i] is None else 1, row[i:j])
        else:
            literal.extend(row[i:j])
        i = j
    add(2, literal)
    return runs


def encode_sti2(image, indexed=False):
    rgba, has_alpha = image_rgba(image)
    w, h = rgba.size
    pixels = [None if a == 0 else (rgb565(r, g, b), a) for r, g, b, a in rgba_pixels(rgba)]
    rows = [pixels[y * w:(y + 1) * w] for y in range(h)]
    palette = []
    if indexed:
        for pixel in pixels:
            if pixel is not None and pixel not in palette:
                palette.append(pixel)
        if len(palette) > 16:
            return None

    def colour(pixel):
        return struct.pack('<H', pixel[0]) + (bytes([pixel[1]]) if has_alpha else b'')

    def encode_row(row):
        data = bytearray()
        for kind, run in row_runs(row, 4 if indexed else 2):
            data.append(kind << 6 | (len(run) - 1))
            if kind == 1:
                data.extend(bytes([palette.index(run[0])]) if indexed else colour(run[0]))
            elif kind == 2 and indexed:
                indices = [palette.index(pixel) for pixel in run] + [0]
                data.extend(indices[k] << 4 | indices[k + 1] for k in range(0, len(run), 2))
            elif kind == 2:
                for pixel in run:
                    data.extend(colour(pixel))
        return bytes(data)

    data = bytearray(b'STI2' + struct.pack('<HHBB2x', w, h, int(has_alpha), len(palette)))
    for pixel in palette:
        data.extend(colour(pixel))
    offsets, body, last = [], bytearray(), None
    for row in rows:
        encoded = encode_row(row)
        if encoded != last:
            offsets.append(len(body))
            body.extend(encoded)
            last = encoded
        else:
            offsets.append(offsets[-1])
    data.extend(struct.pack('<%dI' % h, *offsets))
    data.extend(body)
    return bytes(data)


def encode_image(image, sti2=False):
    forms = [encode_sti1(image)]
    if sti2:
        forms += [encode_sti2(image), encode_sti2(image, indexed=True)]
    return min((f for f in forms if f is not None), key=len)


def decode_sti(data):
    """(width, height, colors565 list, alpha list) of an STI1 or STI2 payload."""
    magic = data[:4]
    if magic == b'STI1':
        w, h, flag = struct.unpack_from('<HHB', data, 4)
        stride = 3 if flag else 2
        if not (1 <= w <= 240 and 1 <= h <= 240) or len(data) != 12 + w * h * stride:
            raise ValueError('Invalid STI1 image')
        colors, alpha = [], []
        for i in range(w * h):
            colors.append(struct.unpack_from('<H', data, 12 + i * stride)[0])
            alpha.append(data[14 + i * stride] if flag else 255)
        return w, h, colors, alpha
    if magic != b'STI2':
        raise ValueError('Unknown image format')
    w, h, flag, pal = struct.unpack_from('<HHBB', data, 4)
    if not (1 <= w <= 240 and 1 <= h <= 240) or pal > 16:
        raise ValueError('Invalid STI2 image')
    pos, csize = 12, 3 if flag else 2
    palette = []
    for _ in range(pal):
        palette.append((struct.unpack_from('<H', data, pos)[0], data[pos + 2] if flag else 255))
        pos += csize
    offsets = struct.unpack_from('<%dI' % h, data, pos)
    base = pos + 4 * h
    ends = sorted(set(offsets)) + [len(data) - base]
    colors, alpha = [], []

    def read_colour(at):
        return (struct.unpack_from('<H', data, at)[0], data[at + 2] if flag else 255), at + csize

    for off in offsets:
        end = next(e for e in ends if e > off) if off < len(data) - base else off
        at, stop, row = base + off, base + end, []
        while len(row) < w:
            if at >= stop:
                raise ValueError('STI2 row too short')
            head = data[at]
            at += 1
            kind, n = head >> 6, (head & 63) + 1
            if kind == 0:
                row += [(0, 0)] * n
            elif kind == 1:
                if pal:
                    px = palette[data[at]]
                    at += 1
                else:
                    px, at = read_colour(at)
                row += [px] * n
            elif kind == 2:
                if pal:
                    for k in range(n):
                        byte = data[at + k // 2]
                        row.append(palette[(byte >> 4) if k % 2 == 0 else (byte & 15)])
                    at += (n + 1) // 2
                else:
                    for _ in range(n):
                        px, at = read_colour(at)
                        row.append(px)
            else:
                raise ValueError('Reserved STI2 run')
        if len(row) != w:
            raise ValueError('STI2 row width mismatch')
        colors += [c for c, _ in row]
        alpha += [a for _, a in row]
    return w, h, colors, alpha


def pil_asset(image):
    rgba = image.convert('RGBA')
    w, h = rgba.size
    colors, alpha = [], []
    for r, g, b, a in rgba_pixels(rgba):
        colors.append(rgb565(r, g, b))
        alpha.append(a)
    return w, h, colors, alpha


# --------------------------------------------------------------------------
# Source folders and packages
# --------------------------------------------------------------------------
def unique_keys(pairs):
    out = {}
    for k, v in pairs:
        if k in out:
            raise ValueError(f'Duplicate JSON key: {k}')
        out[k] = v
    return out


def read_manifest(directory):
    path = Path(directory) / 'theme.json'
    if path.stat().st_size > MAX_MANIFEST:
        raise ValueError('Manifest exceeds 16 KiB')
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_keys)


def resolve_spec(theme):
    """'auto' (or a missing spec) becomes the lowest spec the manifest needs."""
    if theme.get('spec') in (None, 'auto'):
        new = {'spec': needed_spec(theme)}
        new.update({k: v for k, v in theme.items() if k != 'spec'})
        return new, True
    return theme, False


def source_file(root, name):
    if not path_ok(name) or len(name) > 116:
        raise ValueError(f'Invalid asset path: {name}')
    path = (root / PurePosixPath(name)).resolve()
    if root not in path.parents and path != root:
        raise ValueError('Asset path escapes the theme directory')
    return path


def load_source(directory):
    """(theme, {compiled path: PIL image}) of a source folder."""
    root = Path(directory).resolve()
    theme, _ = resolve_spec(read_manifest(root))
    images = {}
    for i, l in enumerate(theme.get('layers') or []):
        if not isinstance(l, dict) or l.get('type') not in ('image', 'animation') or not isinstance(l.get('source'), str):
            continue
        if Image is None:
            raise ValueError('Pillow is needed for image and animation layers: pip install Pillow')
        names = [l['source']] if l['type'] == 'image' else [f'{l["source"]}/{k:03d}.png' for k in range(min(240, max(0, l.get('frames') or 0)))]
        for name in names:
            if name + '.sti' in images:
                continue
            try:
                with Image.open(source_file(root, name)) as im:
                    im.load()
                    images[name + '.sti'] = im.convert('RGBA')
            except (OSError, ValueError) as e:
                raise ValueError(f'layers[{i}].source: {name}: {e}') from e
    return theme, images


def pack(theme, images):
    sizes = {k: (im.size[0], im.size[1]) for k, im in images.items()}
    errors = validate(theme, sizes)
    if errors:
        raise ValueError('\n'.join(errors))
    sti2 = theme['spec'] == 2
    entries = {'theme.json': manifest_json(theme)}
    for l in theme['layers']:
        for name in frame_paths(l):
            if name not in entries:
                entries[name] = encode_image(images[name], sti2)
    if len(entries) > MAX_ENTRIES:
        raise ValueError('Package exceeds 256 entries')
    data = bytearray(b'STH1' + struct.pack('<HH', len(entries), 0))
    for name, payload in entries.items():
        p = name.encode('ascii')
        data += struct.pack('<HI', len(p), len(payload)) + p + payload
    if len(data) > MAX_PACKAGE:
        raise ValueError('Package exceeds 3 MiB')
    return bytes(data)


def unpack(data):
    """(theme, {compiled path: decoded asset}) of a .stheme package."""
    if len(data) < 8 or len(data) > MAX_PACKAGE or data[:4] != b'STH1' or struct.unpack_from('<H', data, 6)[0]:
        raise ValueError('Invalid .stheme package')
    count = struct.unpack_from('<H', data, 4)[0]
    if not 1 <= count <= MAX_ENTRIES:
        raise ValueError('Invalid package entry count')
    off, theme, assets, seen, entries_magic = 8, None, {}, set(), {}
    for _ in range(count):
        if off + 6 > len(data):
            raise ValueError('Truncated package entry')
        nlen, size = struct.unpack_from('<HI', data, off)
        off += 6
        if not nlen or nlen > 120 or off + nlen + size > len(data):
            raise ValueError('Truncated package payload')
        name = data[off:off + nlen].decode('ascii')
        off += nlen
        if not path_ok(name) or name in seen:
            raise ValueError('Unsafe or duplicate package path')
        seen.add(name)
        payload = data[off:off + size]
        off += size
        if name == 'theme.json':
            if size > MAX_MANIFEST:
                raise ValueError('Manifest exceeds 16 KiB')
            theme = json.loads(payload.decode('utf-8'), object_pairs_hook=unique_keys)
        elif name.endswith('.sti'):
            entries_magic[name] = bytes(payload[:4])
            assets[name] = decode_sti(payload)
        else:
            raise ValueError(f'Unsupported package entry: {name}')
    if off != len(data):
        raise ValueError('Trailing package bytes')
    if theme is None:
        raise ValueError('Package has no theme.json')
    if theme.get('spec') == 1:
        for name in assets:
            if entries_magic.get(name) == b'STI2':
                raise ValueError(f'{name}: STI2 images need "spec": 2')
    errors = validate(theme, assets)
    if errors:
        raise ValueError('\n'.join(errors))
    return theme, assets


def load_any(path):
    """(theme, decoded assets) of a source folder or a .stheme file."""
    path = Path(path)
    if path.is_dir():
        theme, images = load_source(path)
        errors = validate(theme, {k: im.size for k, im in images.items()})
        if errors:
            raise ValueError('\n'.join(errors))
        return theme, {k: pil_asset(im) for k, im in images.items()}
    return unpack(path.read_bytes())


# --------------------------------------------------------------------------
# Renderer: the firmware's integer geometry, row-free but pixel-equivalent
# --------------------------------------------------------------------------
TURN16 = 5760


def cdiv(a, b):  # C integer division (truncates toward zero)
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def wrap16(a):
    a = int(math.fmod(a, TURN16))
    return a + TURN16 if a < 0 else a


def sin_q14(a):
    a = wrap16(a)
    neg = a >= TURN16 // 2
    if neg:
        a -= TURN16 // 2
    k = a * (TURN16 // 2 - a)
    den = 10368000 - k
    v = (4 * k * 16384 + den // 2) // den
    return -v if neg else v


def direction(a):
    return sin_q14(a), -sin_q14(a + TURN16 // 4)


def round_q14(v):
    return (v + 8192) >> 14 if v >= 0 else -((8192 - v) >> 14)


def arctan16(a, b):
    r = a * 1024 // b
    return (r * (737280 + 250 * (1024 - r))) >> 20


def pixel_angle16(dx, dy):
    u, v = abs(dx), abs(dy)
    if not u and not v:
        return 0
    base = arctan16(u, v) if u <= v else TURN16 // 4 - arctan16(v, u)
    if dx >= 0:
        return base if dy <= 0 else TURN16 // 2 - base
    return TURN16 // 2 + base if dy > 0 else TURN16 - base


def line_pixel(lx, ly, lx2, ly2, sw, x, y):
    dx, dy, px, py = lx2 - lx, ly2 - ly, x - lx, y - ly
    ln, dot = dx * dx + dy * dy, px * dx + py * dy
    if dot <= 0 or ln == 0:
        return 4 * (px * px + py * py) <= sw * sw
    if dot >= ln:
        px, py = x - lx2, y - ly2
        return 4 * (px * px + py * py) <= sw * sw
    cross = px * dy - py * dx
    return 4 * cross * cross <= sw * sw * ln


def rect_hit(x, y, rx, ry, rw, rh, radius, sw):
    def straight():
        return True, (x - rx < sw or rx + rw - x <= sw or y - ry < sw or ry + rh - y <= sw)
    if radius <= 0:
        return straight()
    left, right = x < rx + radius, x >= rx + rw - radius
    top, bottom = y < ry + radius, y >= ry + rh - radius
    if not ((left or right) and (top or bottom)):
        return straight()
    cx = rx + radius if left else rx + rw - radius - 1
    cy = ry + radius if top else ry + rh - radius - 1
    dist = (x - cx) ** 2 + (y - cy) ** 2
    inside, inner = dist <= radius * radius, max(0, radius - sw)
    return inside, inside and (inner == 0 or dist > inner * inner)


def blend(bg, fg, a):
    if a == 255:
        return fg
    if a == 0:
        return bg
    b = 255 - a
    return ((((fg >> 11) * a + (bg >> 11) * b + 127) // 255) << 11) | \
        (((((fg >> 5) & 63) * a + ((bg >> 5) & 63) * b + 127) // 255) << 5) | \
        (((fg & 31) * a + (bg & 31) * b + 127) // 255)


def text_px(size, length):
    return length * ((size * 6 + 7) // 8)


def parse_number(text):
    if not isinstance(text, str):
        return None
    t = text.strip(' \t\n\r\f\v')
    if not re.fullmatch(r'[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?', t):
        return None
    try:
        n = float(t)
    except ValueError:
        return None
    return n if math.isfinite(n) else None


def moon_age(t):
    seconds = int((t - dt.datetime(1970, 1, 1)).total_seconds())
    return ((seconds - 947182440) % 2551443) // 86400


def time_value(name, t):
    return {'second': t.second, 'minute': t.minute, 'hour': t.hour, 'hour12': t.hour % 12,
            'secondOfDay': t.hour * 3600 + t.minute * 60 + t.second, 'day': t.day, 'month': t.month,
            'weekday': (t.weekday() + 1) % 7, 'moon': moon_age(t)}[name]


def round_decimal(text, places):
    """A plain decimal number rounded to `places` decimals, half away from zero, on its
    digits (1.15 -> 1.2), never '-0'. Anything else, an exponent form included, is
    returned as fetched. Like the firmware's roundDecimal()."""
    s = text.strip(' \t\n\r\f\v')
    if places is None or not re.fullmatch(r'[+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)', s):
        return text
    with decimal.localcontext() as context:
        context.prec = 400
        out = format(decimal.Decimal(s).quantize(decimal.Decimal(1).scaleb(-places), rounding=decimal.ROUND_HALF_UP), 'f')
    return out[1:] if out.startswith('-') and not out.strip('-0.') else out


def expand(value, t, data, lang, decimals=None):
    """Clock tokens, then data fields, then device values; anything unknown is '--'.
    `decimals` maps "<source>.<field>" to the places its text is rounded to."""
    def repl(m):
        key, v = m.group(1), None
        if t is not None:
            wd = (t.weekday() + 1) % 7
            v = {'HH': f'{t.hour:02d}', 'hh': f'{(t.hour % 12) or 12:02d}', 'MM': f'{t.minute:02d}',
                 'SS': f'{t.second:02d}', 'DD': f'{t.day:02d}', 'YYYY': f'{t.year:04d}', 'MO': f'{t.month:02d}',
                 'H': str(t.hour), 'AMPM': 'AM' if t.hour < 12 else 'PM',
                 'MON': MONTHS_SHORT[lang][t.month - 1], 'MONTH': MONTHS[lang][t.month - 1],
                 'WD': DAYS_SHORT[lang][wd], 'WEEKDAY': DAYS[lang][wd]}.get(key)
        if v is not None:
            return v
        return round_decimal(data[key], (decimals or {}).get(key)) if key in data else '--'
    return re.sub(r'\{([^}]*)\}', repl, value)


def resolve_numeric(b, value, lo, hi):
    i0, i1 = b['input']
    o0, o1 = b['output']
    if b.get('clamp', True):
        value = max(min(i0, i1), min(value, max(i0, i1)))
    mapped = o0 + (value - i0) * (o1 - o0) / (i1 - i0)
    if mapped <= lo:
        return lo
    if mapped >= hi:
        return hi
    return int(math.copysign(math.floor(abs(mapped) + 0.5), mapped))


def resolve_color(b, value):
    result = b['stops'][0]['value']
    for stop in b['stops']:
        if stop['at'] > value:
            break
        result = stop['value']
    return result


def source_value(src, t, data):
    if src.startswith('time.') and src[5:] in TIME_NAMES and src not in data:
        return None if t is None else float(time_value(src[5:], t))
    return parse_number(data.get(src))


def resolve_layer(l, t, data):
    r = {'x': l['x'], 'y': l['y'], 'width': l.get('width', 0), 'height': l.get('height', 0),
         'x2': l.get('x2', 0), 'y2': l.get('y2', 0), 'radius': l.get('radius', 0),
         'cornerRadius': l.get('cornerRadius', 0), 'size': l.get('size', 16), 'strokeWidth': l.get('strokeWidth', 1),
         'scroll.width': (l.get('scroll') or {}).get('width', 0), 'scroll.speed': (l.get('scroll') or {}).get('speed', 0),
         'sweep': l.get('sweep', 0), 'angle': l.get('angle', 0),
         'color': l.get('color'), 'fill': l.get('fill'), 'stroke': l.get('stroke')}
    for target, b in (l.get('bind') or {}).items():
        v = source_value(b['source'], t, data)
        if v is None:
            continue
        if target in COLOR_TARGETS:
            r[target] = resolve_color(b, v)
        else:
            lo, hi = NUMERIC_RANGE[target]
            r[target] = resolve_numeric(b, v, lo, hi)
    if l.get('shape') == 'rectangle':
        r['cornerRadius'] = min(r['cornerRadius'], max(0, min(r['width'], r['height']) // 2))
    return r


def hand_segment(l, r):
    dx, dy = direction(r['angle'] * 16)
    tail, length = l.get('tail', 0), l['length']
    return (r['x'] - round_q14(dx * tail), r['y'] - round_q14(dy * tail),
            r['x'] + round_q14(dx * length), r['y'] + round_q14(dy * length))


def tick_segment(l, r, k):
    dx, dy = direction((k * TURN16 + l['count'] // 2) // l['count'])
    inner = max(0, r['radius'] - l['length'])
    return (r['x'] + round_q14(dx * inner), r['y'] + round_q14(dy * inner),
            r['x'] + round_q14(dx * r['radius']), r['y'] + round_q14(dy * r['radius']))


def arc_range(r):
    start, sweep = r['start16'], r['sweep'] * 16
    if sweep < 0:
        start += sweep
        sweep = -sweep
    return wrap16(start), sweep


def arc_wedge(r):
    start, sweep = arc_range(r)
    sx, sy = direction(start)
    ex, ey = direction(start + sweep)
    return sx, sy, ex, ey, sweep >= TURN16, sweep > TURN16 // 2


def in_wedge(w, dx, dy):
    sx, sy, ex, ey, full, reflex = w
    if full:
        return True
    from_start, to_end = sx * dy - sy * dx, dx * ey - dy * ex
    if not reflex:
        return from_start >= 0 and to_end >= 0
    return not (to_end < 0 and from_start < 0)


def arc_bounds(r):
    if r['radius'] <= 0 or r['sweep'] == 0:
        return r['x'], r['y'], 0, 0
    start, sweep = arc_range(r)
    x0, x1, y0, y1 = r['x'] - r['radius'], r['x'] + r['radius'], r['y'] - r['radius'], r['y'] + r['radius']
    if sweep < TURN16:
        x0 = x1 = r['x']
        y0 = y1 = r['y']
        inner = max(0, r['radius'] - r['strokeWidth'])
        pts = []
        for ang in (start, start + sweep):
            pts += [(r['radius'], ang), (inner, ang)]
        for c in range(4):
            if wrap16(c * (TURN16 // 4) - start) <= sweep:
                pts.append((r['radius'], c * (TURN16 // 4)))
        for rad, ang in pts:
            dx, dy = direction(ang)
            x, y = r['x'] + round_q14(dx * rad), r['y'] + round_q14(dy * rad)
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
    return x0 - 2, y0 - 2, x1 - x0 + 5, y1 - y0 + 5


def layer_bounds(l, r, text, assets):
    t, shape = l['type'], l.get('shape')
    if t == 'text':
        w = r['scroll.width'] if l.get('scroll') else text_px(r['size'], len(text))
        a = ANCHORS.index(l.get('anchor', 'top-left'))
        return r['x'] - w * (a % 3) // 2, r['y'] - r['size'] * (a // 3) // 2, w, r['size']
    if t == 'image':
        a = assets.get(asset_path(l))
        return r['x'], r['y'], (a[0] if a else 0), (a[1] if a else 0)
    if t == 'animation':
        return r['x'], r['y'], l['width'], l['height']
    if shape == 'circle':
        if r['radius'] == 0:
            return r['x'], r['y'], 0, 0
        return r['x'] - r['radius'], r['y'] - r['radius'], 2 * r['radius'] + 1, 2 * r['radius'] + 1
    if shape in ('line', 'hand'):
        x0, y0, x1, y1 = (r['x'], r['y'], r['x2'], r['y2']) if shape == 'line' else hand_segment(l, r)
        pad = (r['strokeWidth'] + 1) // 2
        return min(x0, x1) - pad, min(y0, y1) - pad, abs(x0 - x1) + 2 * pad + 1, abs(y0 - y1) + 2 * pad + 1
    if shape == 'arc':
        return arc_bounds(r)
    if shape == 'ticks':
        if r['radius'] <= 0:
            return r['x'], r['y'], 0, 0
        reach = r['radius'] + (r['strokeWidth'] + 1) // 2 + 1
        return r['x'] - reach, r['y'] - reach, 2 * reach + 1, 2 * reach + 1
    return r['x'], r['y'], r['width'], r['height']


def scroll_offset(scroll, viewport, speed, width, elapsed):
    pause = scroll.get('pause', 1000)
    loop = scroll['mode'] == 'loop'
    gap = scroll.get('gap', 24) if loop else 0
    target = width + gap if loop else width - viewport
    if width <= viewport or target <= 0 or speed <= 0:
        return 0
    offset, direction_, phase, last, pause_until = 0, -1, 0, 0, pause
    now = max(0, int(elapsed))
    while True:
        if not now > pause_until:
            return offset
        base = last if last > pause_until else pause_until
        ticks = (now - base) * speed + phase
        progress = offset if loop or direction_ < 0 else target - offset
        needed = (target - progress) * 1000
        if ticks < needed:
            nxt = progress * 1000 + ticks
            pixels = nxt // 1000
            return pixels if loop or direction_ < 0 else target - pixels
        ms = -(-(needed - phase) // speed)
        completion = base + ms
        phase = ms * speed + phase - needed
        last, pause_until = completion, completion + pause
        if loop:
            offset = 0
        elif direction_ < 0:
            offset, direction_ = target, 1
        else:
            offset, direction_ = 0, -1


def scroll_column(scroll, offset, width, local):
    if not scroll:
        return local
    if scroll['mode'] == 'loop':
        cycle = width + scroll.get('gap', 24)
        x = (local + offset) % cycle
        return x if x < width else None
    x = local + offset
    return x if x < width else None


class Renderer:
    """Renders frames of a theme. Keeps per-layer scroll state like the device:
    a text layer's scroll restarts when its expanded text changes."""

    def __init__(self, theme, assets, lang='en', data=None):
        self.theme, self.assets = theme, assets
        self.lang = LANGS.index(lang) if theme['spec'] >= 2 and lang in LANGS else 0
        self.lang_code = LANGS[self.lang]
        self.data = data or {}
        # Only a spec 2 theme may set `decimals` (the validator refuses it in spec 1).
        self.decimals = {f'{s["id"]}.{f["id"]}': f['decimals'] for s in theme.get('data') or [] for f in s['fields']
                         if theme['spec'] >= 2 and f.get('decimals') is not None}
        self.scroll_since = {}

    def layer_text(self, l):
        v = l['value']
        return v.get(self.lang_code, v['en']) if isinstance(v, dict) else v

    def frame(self, t, elapsed):
        bg = color565(self.theme['display']['background'])
        out = [bg] * (240 * 240)
        for idx, l in enumerate(self.theme['layers']):
            r = resolve_layer(l, t, self.data)
            if l['type'] == 'shape' and l['shape'] == 'arc':
                r['start16'] = l['start'] * 16
            text = expand(self.layer_text(l), t, self.data, self.lang, self.decimals) if l['type'] == 'text' else ''
            bx, by, bw, bh = layer_bounds(l, r, text, self.assets)
            x1, y1, x2, y2 = max(0, bx), max(0, by), min(240, bx + bw), min(240, by + bh)
            if x1 >= x2 or y1 >= y2:
                continue
            t_ = l['type']
            if t_ in ('image', 'animation'):
                frame = 0
                if t_ == 'animation':
                    n = int(elapsed * l['fps'] // 1000)
                    frame = n % l['frames'] if l['loop'] else min(l['frames'] - 1, n)
                a = self.assets.get(asset_path(l, frame))
                if not a:
                    continue
                aw, _, colors, alpha = a
                for y in range(y1, y2):
                    row = (y - r['y']) * aw - r['x']
                    for x in range(x1, x2):
                        i = row + x
                        if alpha[i]:
                            out[y * 240 + x] = blend(out[y * 240 + x], colors[i], alpha[i])
                continue
            if t_ == 'text':
                size, fg = r['size'], color565(r['color'])
                cell, width = (size * 6 + 7) // 8, text_px(size, len(text))
                offset = 0
                sc = l.get('scroll')
                if sc:
                    key = (text, size, r['scroll.width'], r['scroll.speed'])
                    since = self.scroll_since.get(idx)
                    if since is None or since[0] != key:
                        since = (key, elapsed)
                        self.scroll_since[idx] = since
                    offset = scroll_offset(sc, r['scroll.width'], r['scroll.speed'], width, elapsed - since[1])
                codes = [ord(c) for c in text]
                for y in range(y1, y2):
                    bit = 1 << ((y - by) * 8 // size)
                    for x in range(x1, x2):
                        cx = scroll_column(sc, offset, width, x - bx)
                        if cx is None:
                            continue
                        col, ci = (cx % cell) * 6 // cell, cx // cell
                        if col < 5 and ci < len(codes) and codes[ci] < 127 and FONT[codes[ci] * 5 + col] & bit:
                            out[y * 240 + x] = fg
                continue
            shape, sw = l['shape'], r['strokeWidth']
            fill = color565(r['fill']) if r.get('fill') and shape not in ROTATION else None
            stroke = color565(r['stroke']) if r.get('stroke') else None
            if shape == 'arc':
                wedge, outer = arc_wedge(r), r['radius']
                inner = max(0, outer - sw)
                for y in range(y1, y2):
                    for x in range(x1, x2):
                        dx, dy = x - r['x'], y - r['y']
                        dist = dx * dx + dy * dy
                        if r['sweep'] != 0 and dist <= outer * outer and (inner == 0 or dist > inner * inner) and in_wedge(wedge, dx, dy):
                            out[y * 240 + x] = stroke
                continue
            if shape == 'hand':
                seg = hand_segment(l, r)
                for y in range(y1, y2):
                    for x in range(x1, x2):
                        if line_pixel(*seg, sw, x, y):
                            out[y * 240 + x] = stroke
                continue
            if shape == 'ticks':
                count = l['count']
                pad = (sw + 1) // 2 + 1
                near, far = max(0, r['radius'] - l['length'] - pad), r['radius'] + pad
                segs = {}
                for y in range(y1, y2):
                    for x in range(x1, x2):
                        dx, dy = x - r['x'], y - r['y']
                        dist = dx * dx + dy * dy
                        if near * near <= dist <= far * far:
                            k = ((pixel_angle16(dx, dy) * count + TURN16 // 2) // TURN16) % count
                            if k not in segs:
                                segs[k] = tick_segment(l, r, k)
                            if line_pixel(*segs[k], sw, x, y):
                                out[y * 240 + x] = stroke
                continue
            for y in range(y1, y2):
                for x in range(x1, x2):
                    if shape == 'rectangle':
                        inside, edge = rect_hit(x, y, r['x'], r['y'], r['width'], r['height'], r['cornerRadius'], sw)
                    elif shape == 'circle':
                        dx, dy = x - r['x'], y - r['y']
                        dist, inner = dx * dx + dy * dy, max(0, r['radius'] - sw)
                        inside = dist <= r['radius'] ** 2
                        edge = inside and (inner == 0 or dist > inner * inner)
                    else:
                        inside = edge = line_pixel(r['x'], r['y'], r['x2'], r['y2'], sw, x, y)
                    if inside and fill is not None:
                        out[y * 240 + x] = fill
                    if edge and stroke is not None:
                        out[y * 240 + x] = stroke
        return out


def to_image(pixels, scale=1):
    lut = {}
    raw = bytearray()
    for c in pixels:
        v = lut.get(c)
        if v is None:
            v = lut[c] = bytes(rgb888(c))
        raw += v
    im = Image.frombytes('RGB', (240, 240), bytes(raw))
    return im.resize((240 * scale, 240 * scale), Image.NEAREST) if scale > 1 else im


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------
def scaffold_theme(theme_id, name, author, palette):
    p = PALETTES[palette]
    return {
        'spec': 'auto',
        'theme': {'id': theme_id, 'name': name, 'author': author, 'version': '1.0.0'},
        'display': {'width': 240, 'height': 240, 'background': p['background']},
        'layers': [
            {'id': 'card', 'type': 'shape', 'shape': 'rectangle', 'x': 12, 'y': 40, 'width': 216, 'height': 160,
             'cornerRadius': 16, 'fill': p['surface'], 'stroke': p['line'], 'strokeWidth': 1},
            {'id': 'weekday', 'type': 'text', 'x': 120, 'y': 64, 'anchor': 'center', 'value': '{WEEKDAY}',
             'size': 16, 'color': p['accent']},
            {'id': 'clock', 'type': 'text', 'x': 120, 'y': 112, 'anchor': 'center', 'value': '{HH}:{MM}',
             'size': 56, 'color': p['text']},
            {'id': 'rule', 'type': 'shape', 'shape': 'rectangle', 'x': 90, 'y': 146, 'width': 60, 'height': 3,
             'cornerRadius': 1, 'fill': p['accent']},
            {'id': 'date', 'type': 'text', 'x': 120, 'y': 172, 'anchor': 'center', 'value': '{DD} {MON} {YYYY}',
             'size': 16, 'color': p['muted']},
        ]}


def write_json(path, theme):
    Path(path).write_text(json.dumps(theme, indent=2) + '\n', encoding='utf-8')


def cmd_scaffold(a):
    d = Path(a.dir)
    d.mkdir(parents=True, exist_ok=True)
    if (d / 'theme.json').exists() and not a.force:
        raise ValueError(f'{d}/theme.json exists (use --force)')
    theme_id = a.id or re.sub(r'[^A-Za-z0-9_-]+', '-', d.name).strip('-')[:48] or 'my-clock'
    write_json(d / 'theme.json', scaffold_theme(theme_id, a.name or theme_id.replace('-', ' ').title(), a.author, a.palette))
    print(f'Wrote {d / "theme.json"}')


def cmd_validate(a):
    theme, _ = load_any(a.source)
    reasons = spec2_reasons(theme)
    print(f'Valid: {theme["theme"]["name"]} ({theme["theme"]["id"]}), spec {theme["spec"]}'
          + (f' (needed for: {"; ".join(reasons[:4])}{"..." if len(reasons) > 4 else ""})' if reasons else ''))


def cmd_build(a):
    src = Path(a.source)
    raw = read_manifest(src)
    theme, images = load_source(src)
    data = pack(theme, images)
    rewrite = raw.get('spec') in (None, 'auto') and not a.keep_auto
    if rewrite:
        write_json(src / 'theme.json', theme)  # so the firmware's own tools accept the folder too
    Path(a.output).parent.mkdir(parents=True, exist_ok=True)
    Path(a.output).write_bytes(data)
    print(f'Built {a.output}: {len(data)} bytes, spec {theme["spec"]}, {len(theme["layers"])} layers, '
          f'{len(data) * 100 // MAX_PACKAGE}% of the 3 MiB limit, manifest {len(manifest_json(theme))}/16384 bytes')
    if rewrite:
        was = f'"spec": {json.dumps(raw["spec"])}' if 'spec' in raw else 'no "spec"'
        print(f'Rewrote {src / "theme.json"}: {was} -> "spec": {theme["spec"]} '
              '(--keep-auto leaves the file unchanged)')


def parse_data(pairs):
    out = {}
    for pair in pairs:
        k, sep, v = pair.partition('=')
        if not sep or not k:
            raise ValueError(f'--data must be KEY=VALUE: {pair}')
        out[k] = v
    return out


def cmd_preview(a):
    if Image is None:
        raise ValueError('Pillow is needed for previews: pip install Pillow')
    theme, assets = load_any(a.source)
    data = parse_data(a.data)
    known = set(data_fields(theme)) | {f'device.{n}' for n in DEVICE_NAMES}
    for k in data:
        if k not in known:
            raise ValueError(f'--data {k}: not a declared data field or device.* name')
    start = None if a.unsynced else (dt.datetime.fromisoformat(a.time) if a.time else dt.datetime.now().replace(microsecond=0))
    renderer = Renderer(theme, assets, a.lang, data)
    base = Path(a.output)
    if base.suffix.lower() in ('.png', '.gif'):
        base = base.with_suffix('')
    base.parent.mkdir(parents=True, exist_ok=True)
    first = renderer.frame(start, 0)
    to_image(first, a.scale).save(base.with_suffix('.png'))
    print(f'Preview: {base.with_suffix(".png")}')
    if a.seconds <= 0:
        return
    fps = max(1, min(15, a.fps))
    count = max(1, min(60, a.seconds)) * fps
    frames, durations, last = [], [], None
    renderer = Renderer(theme, assets, a.lang, data)
    for n in range(count):
        ms = n * 1000 // fps
        t = None if start is None else start + dt.timedelta(milliseconds=ms)
        px = renderer.frame(t, ms)
        if px == last:
            durations[-1] += 1000 // fps
            continue
        frames.append(to_image(px, a.scale))
        durations.append(1000 // fps)
        last = px
    frames[0].save(base.with_suffix('.gif'), save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=False)
    print(f'Animated preview: {base.with_suffix(".gif")} ({count} frames at {fps} FPS, {len(frames)} distinct)')


def fit_image(im, size, mode, pixel):
    im = im.convert('RGBA')
    if size is None:
        if im.width <= 240 and im.height <= 240:
            return im
        size = (240, 240)
        mode = 'contain'
    w, h = size
    resample = Image.NEAREST if pixel else Image.LANCZOS
    if mode == 'stretch':
        return im.resize((w, h), resample)
    scale = (max if mode == 'cover' else min)(w / im.width, h / im.height)
    nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    im = im.resize((nw, nh), resample)
    if mode == 'cover':
        left, top = (nw - w) // 2, (nh - h) // 2
        return im.crop((left, top, left + w, top + h))
    canvas = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    canvas.paste(im, ((w - nw) // 2, (h - nh) // 2))
    return canvas


def parse_size(s):
    if not s:
        return None
    m = re.fullmatch(r'(\d+)x(\d+)', s)
    if not m or not all(1 <= int(v) <= 240 for v in m.groups()):
        raise ValueError('--size must be WxH with each side 1..240')
    return int(m[1]), int(m[2])


def cmd_image(a):
    with Image.open(a.input) as im:
        out = fit_image(im, parse_size(a.size), a.fit, a.pixel)
    Path(a.output).parent.mkdir(parents=True, exist_ok=True)
    out.save(a.output)
    print(f'Wrote {a.output} ({out.width}x{out.height})')


def cmd_gif(a):
    size = parse_size(a.size)
    out = Path(a.output)
    out.mkdir(parents=True, exist_ok=True)
    frames, total = [], 0
    with Image.open(a.input) as im:
        n = getattr(im, 'n_frames', 1)
        for i in range(n):
            im.seek(i)
            total += im.info.get('duration', 100) or 100
            frames.append(fit_image(im.convert('RGBA'), size or (min(im.width, 240), min(im.height, 240)), a.fit, a.pixel))
    step = max(1, math.ceil(len(frames) / a.max_frames))
    frames = frames[::step]
    for i, f in enumerate(frames):
        f.save(out / f'{i:03d}.png')
    fps = max(1, min(15, round(len(frames) * 1000 / max(1, total))))
    w, h = frames[0].size
    root = next((d for d in out.resolve().parents if (d / 'theme.json').exists()), None)
    source = out.resolve().relative_to(root).as_posix() if root else out.as_posix()
    print(json.dumps({'type': 'animation', 'source': source, 'width': w, 'height': h, 'frames': len(frames),
                      'fps': fps, 'loop': True}))


def cmd_install(a):
    import urllib.request
    import urllib.error
    data = Path(a.package).read_bytes()
    theme, _ = unpack(data)
    theme_id = theme['theme']['id']
    base = f'http://{a.host}'
    handlers = []
    if a.password:
        mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        mgr.add_password(None, base, a.user, a.password)
        handlers.append(urllib.request.HTTPDigestAuthHandler(mgr))
    opener = urllib.request.build_opener(*handlers)

    def call(path, body=None, ctype=None):
        req = urllib.request.Request(base + path, data=body, method='POST' if body is not None else 'GET')
        if ctype:
            req.add_header('Content-Type', ctype)
        try:
            with opener.open(req, timeout=30) as resp:
                return resp.status, json.loads(resp.read() or b'{}')
        except urllib.error.HTTPError as e:
            try:
                return e.code, json.loads(e.read() or b'{}')
            except ValueError:
                return e.code, {'error': e.reason}

    status, listing = call('/api/themes')
    if status != 200:
        raise ValueError(f'GET /api/themes: HTTP {status} {listing.get("error", "")}'.strip())
    if any(t.get('id') == theme_id for t in listing.get('themes', [])):
        if not a.replace:
            raise ValueError(f'Theme {theme_id} is already installed; rerun with --replace to remove it first')
        if listing.get('selected') == theme_id:
            raise ValueError(f'{theme_id} is the active theme: switch the TV to another mode or theme first')
        status, body = call('/api/themes/delete', json.dumps({'id': theme_id}).encode(), 'text/plain')
        if status != 200:
            raise ValueError(f'Delete failed: HTTP {status} {body.get("error", "")}')
    boundary = uuid.uuid4().hex
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{theme_id}.stheme"\r\n'
            f'Content-Type: application/octet-stream\r\n\r\n').encode() + data + f'\r\n--{boundary}--\r\n'.encode()
    status, resp = call('/api/themes/install', body, f'multipart/form-data; boundary={boundary}')
    if status not in (200, 201):
        raise ValueError(f'Install failed: HTTP {status} {resp.get("error", "")}')
    print(f'Installed {theme_id} on {a.host}')
    if a.activate:
        status, resp = call('/api/themes/select', json.dumps({'id': theme_id}).encode(), 'text/plain')
        if status != 200:
            raise ValueError(f'Select failed: HTTP {status} {resp.get("error", "")}')
        print(f'Activated {theme_id}')


def cmd_palette(a):
    names = [a.name] if a.name else list(PALETTES)
    for n in names:
        print(n + ': ' + json.dumps(PALETTES[n]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('scaffold', help='Write a default digital clock theme.json')
    s.add_argument('dir')
    s.add_argument('--id')
    s.add_argument('--name')
    s.add_argument('--author', default='SmallTV theme skill')
    s.add_argument('--palette', choices=list(PALETTES), default='night')
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('validate', help='Validate a source folder or .stheme')
    s.add_argument('source')
    s = sub.add_parser('build', help='Validate and pack a source folder')
    s.add_argument('source')
    s.add_argument('output')
    s.add_argument('--keep-auto', action='store_true', help='Do not write the resolved spec back to theme.json')
    s = sub.add_parser('preview', help='Render OUT.png and an animated OUT.gif')
    s.add_argument('source')
    s.add_argument('output')
    s.add_argument('--time', help='Local wall time, e.g. 2026-09-18T10:24:55 (default: now)')
    s.add_argument('--seconds', type=int, default=6, help='GIF length 1..60 s, 0 for PNG only (default 6)')
    s.add_argument('--fps', type=int, default=10)
    s.add_argument('--data', action='append', default=[], metavar='SRC.FIELD=VALUE')
    s.add_argument('--lang', choices=LANGS, default='en')
    s.add_argument('--unsynced', action='store_true', help='Show the clock before NTP sync (-- everywhere)')
    s.add_argument('--scale', type=int, default=2)
    for name, helptext in (('image', 'Convert an image into a theme asset PNG'), ('gif', 'Split a GIF into animation frames')):
        s = sub.add_parser(name, help=helptext)
        s.add_argument('input')
        s.add_argument('output')
        s.add_argument('--size', help='WxH, each 1..240')
        s.add_argument('--fit', choices=['cover', 'contain', 'stretch'], default='cover')
        s.add_argument('--pixel', action='store_true', help='Nearest-neighbour scaling for pixel art')
        if name == 'gif':
            s.add_argument('--max-frames', type=int, default=60)
    s = sub.add_parser('install', help='Install a .stheme on a SmallTV over its HTTP API')
    s.add_argument('package')
    s.add_argument('--host', required=True)
    s.add_argument('--user', default='admin')
    s.add_argument('--password')
    s.add_argument('--activate', action='store_true')
    s.add_argument('--replace', action='store_true')
    s = sub.add_parser('palette', help='Print the built-in palettes')
    s.add_argument('name', nargs='?', choices=list(PALETTES))
    a = ap.parse_args(argv)
    try:
        {'scaffold': cmd_scaffold, 'validate': cmd_validate, 'build': cmd_build, 'preview': cmd_preview,
         'image': cmd_image, 'gif': cmd_gif, 'install': cmd_install, 'palette': cmd_palette}[a.cmd](a)
    except (OSError, ValueError, KeyError, TypeError) as e:
        print(f'{a.cmd} failed:\n{e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
