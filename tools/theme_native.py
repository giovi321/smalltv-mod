"""Build/cache the shared native theme adapter. No firmware build is needed."""
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def dependency(variable, folder, header):
    explicit = os.environ.get(variable)
    if explicit:
        path = Path(explicit).resolve()
        if (path / header).is_file():
            return path
        raise ValueError(f'{variable} must point to a directory containing {header}')
    preferred = ROOT / '.pio/libdeps/smalltv_esp32_8mb' / folder / 'src'
    candidates = [preferred, *sorted((ROOT / '.pio/libdeps').glob(f'*/{folder}/src'))]
    for path in candidates:
        if (path / header).is_file():
            return path
    raise ValueError(f'Missing {header}. Run "pio pkg install -e smalltv_esp32_8mb" '
                     f'or set {variable} to the library source directory.')


def max_layers():
    """The layer limit of the firmware the theme is for: 32, the release images',
    unless SMALLTV_THEME_MAX_LAYERS names a build's -DTHEME_MAX_LAYERS (1 to 255)."""
    value = os.environ.get('SMALLTV_THEME_MAX_LAYERS', '32')
    if not re.fullmatch(r'[0-9]{1,4}', value) or not 1 <= int(value) <= 255:
        raise ValueError('SMALLTV_THEME_MAX_LAYERS must be a number from 1 to 255')
    return int(value)


def executable():
    return build_executable(max_layers())


@lru_cache(maxsize=4)
def build_executable(layers):
    arduino_json = dependency('SMALLTV_ARDUINOJSON_INCLUDE', 'ArduinoJson', 'ArduinoJson.h')
    gfx = dependency('SMALLTV_GFX_INCLUDE', 'GFX Library for Arduino', 'font/glcdfont.h')
    sources = [ROOT/'tools/theme_native.cpp', ROOT/'src/features/theme/ThemeEngine.cpp',
               ROOT/'src/features/theme/ThemePackage.cpp']
    dependencies = [*sources, *sorted((ROOT/'src/features/theme').glob('*.h')),
                    *sorted(arduino_json.rglob('*.h')), *sorted(arduino_json.rglob('*.hpp')),
                    gfx/'font/glcdfont.h']
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    try:
        version = subprocess.run([*compiler, '--version'], capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError('A desktop C++11 compiler is required (c++/g++/clang++; or set CXX).') from error
    digest = hashlib.sha256(version)
    digest.update(f'layers={layers}'.encode())
    for path in dependencies:
        digest.update(str(path).encode())
        digest.update(path.read_bytes())
    cache = ROOT/'.pio/theme-host'
    cache.mkdir(parents=True, exist_ok=True)
    target = cache/f'theme-native-{digest.hexdigest()[:20]}'
    if target.is_file():
        return target
    with tempfile.TemporaryDirectory(dir=cache) as scratch:
        output = Path(scratch)/'theme-native'
        args = [*compiler, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror', f'-DTHEME_MAX_LAYERS={layers}',
                '-I'+str(ROOT/'src/features/theme'), '-I'+str(arduino_json), '-I'+str(gfx),
                *map(str, sources), '-o', str(output)]
        result = subprocess.run(args, capture_output=True, text=True)
        if result.returncode:
            raise ValueError('Native theme tool compilation failed:\n'+result.stderr)
        output.replace(target)
    return target


def run(*args):
    result = subprocess.run([str(executable()), *map(str, args)], capture_output=True, text=True)
    if result.returncode:
        raise ValueError(result.stderr.strip() or 'Native theme validation failed')
    return result.stdout


def validate_manifest(path):
    return json.loads(run('validate-manifest', path))


def validate_package(path):
    return json.loads(run('validate', path))
