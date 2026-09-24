#!/usr/bin/env python3
"""Validate, build and preview SmallTV themes without a device."""
import argparse
from pathlib import Path
import tempfile
from theme_pack import build
from theme_native import validate_package
from theme_preview import write_preview


def parse_data_values(pairs):
    if len(pairs) > 32:
        raise ValueError('At most 32 --data values are allowed')
    values = {}
    for pair in pairs:
        key, sep, value = pair.partition('=')
        if not sep:
            raise ValueError(f'--data must be KEY=VALUE: {pair}')
        if not key:
            raise ValueError(f'--data key must not be empty: {pair}')
        if key in values:
            raise ValueError(f'Duplicate --data key: {key}')
        values[key] = value
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    validate = commands.add_parser('validate', help='Check a source directory or .stheme using the firmware validator')
    validate.add_argument('source', type=Path)
    pack = commands.add_parser('build', help='Validate and package a source directory')
    pack.add_argument('source', type=Path)
    pack.add_argument('output', type=Path)
    preview = commands.add_parser('preview', help='Create a standalone HTML preview using the firmware renderer')
    preview.add_argument('source', type=Path)
    preview.add_argument('output', type=Path)
    preview.add_argument('--seconds', type=int, default=10, help='Simulation duration, 1..60 (default: 10)')
    preview.add_argument('--fps', type=int, default=15, help='Preview sampling rate, 1..15 (default: 15)')
    preview.add_argument('--time', help='Local wall time, e.g. 2026-09-18T10:24:00 (default: computer time)')
    preview.add_argument('--data', action='append', default=[], metavar='KEY=VALUE',
                         help='Inject a declared data value into every preview frame')
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory() as scratch:
            if args.command == 'build':
                data = build(args.source)
                args.output.write_bytes(data)
                print(f'Built {args.output}: {len(data)} bytes (validated)')
                return
            package = args.source
            if args.source.is_dir():
                package = Path(scratch)/'theme.stheme'
                package.write_bytes(build(args.source))
            if args.command == 'validate':
                metadata = validate_package(package)
                print(f'Valid: {metadata["name"]} ({metadata["id"]})')
            else:
                values = parse_data_values(args.data)
                write_preview(package, args.output, args.seconds, args.fps, args.time, values=values)
                print(f'Preview: {args.output.resolve()}')
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f'Theme {args.command} failed: {error}\n')


if __name__ == '__main__':
    main()
