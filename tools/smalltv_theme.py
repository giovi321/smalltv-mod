#!/usr/bin/env python3
"""Validate, build and preview SmallTV themes without a device."""
import argparse
from pathlib import Path
import tempfile
from theme_pack import build
from theme_native import validate_package
from theme_preview import write_preview


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
                write_preview(package, args.output, args.seconds, args.fps, args.time)
                print(f'Preview: {args.output.resolve()}')
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f'Theme {args.command} failed: {error}\n')


if __name__ == '__main__':
    main()
