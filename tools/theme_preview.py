"""Turn native firmware-rendered RGB frames into an offline interactive preview."""
import base64
from datetime import datetime, timezone
import hashlib
import html
import io
import json
from pathlib import Path
import struct
import tempfile
from PIL import Image
try:
    from .theme_native import run, validate_package
except ImportError:
    from theme_native import run, validate_package


def write_preview(package, output, seconds=10, fps=15, start=None, values=None):
    if not (1 <= seconds <= 60 and 1 <= fps <= 15):
        raise ValueError('Preview requires 1..60 seconds and 1..15 FPS')
    values = values or {}
    if start is None:
        start = datetime.now().replace(microsecond=0)
    elif isinstance(start, str):
        try:
            start = datetime.fromisoformat(start)
        except ValueError as error:
            raise ValueError('--time must be a local date/time such as 2026-09-18T10:24:00') from error
    if start.tzinfo is not None or start.year < 1970 or start.year > 9998:
        raise ValueError('--time must be a local date/time without offset, in years 1970..9998')
    start = start.replace(microsecond=0)
    # UTC is a transport for the requested wall time; it is not a timezone conversion.
    epoch = int(start.replace(tzinfo=timezone.utc).timestamp())
    metadata = validate_package(package)
    images, frames, seen = [], [], {}
    with tempfile.TemporaryDirectory() as scratch:
        raw = Path(scratch)/'frames.rgb'
        run('preview', package, raw, epoch, fps, seconds*fps, *(f'{k}={v}' for k, v in values.items()))
        with raw.open('rb') as source:
            expected = b'STP1'+struct.pack('<HHHH', 240, 240, fps, seconds*fps)
            if source.read(12) != expected:
                raise ValueError('Invalid native preview header')
            for _ in range(seconds*fps):
                pixels = source.read(240*240*3)
                if len(pixels) != 240*240*3:
                    raise ValueError('Truncated native preview frame')
                digest = hashlib.sha256(pixels).digest()
                if digest not in seen:
                    png = io.BytesIO()
                    Image.frombytes('RGB', (240, 240), pixels).save(png, format='PNG')
                    seen[digest] = len(images)
                    images.append('data:image/png;base64,'+base64.b64encode(png.getvalue()).decode('ascii'))
                frames.append(seen[digest])
            if source.read(1):
                raise ValueError('Unexpected trailing preview bytes')
    data = json.dumps({'images': images, 'frames': frames, 'fps': fps, 'epoch': epoch, 'values': values},
                       separators=(',', ':')).replace('<', '\\u003c')
    template = Path(__file__).with_name('theme_preview.html').read_text(encoding='utf-8')
    values = {'TITLE': html.escape(metadata['name']),
              'BYLINE': html.escape(metadata['author']+' · '+metadata['version']),
              'FPS': str(fps), 'START': start.isoformat(), 'SECONDS': str(seconds), 'DATA': data}
    # A single pass prevents theme metadata resembling another placeholder from being expanded.
    import re
    result = re.sub(r'__(TITLE|BYLINE|FPS|START|SECONDS|DATA)__', lambda m: values[m[1]], template)
    Path(output).write_text(result, encoding='utf-8')
    return metadata
