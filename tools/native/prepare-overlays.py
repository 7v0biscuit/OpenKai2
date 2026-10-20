#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply reviewed runtime changes to build copies of the pinned dependency."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

EXPECTED = {
    'ps2xIOP/src/emulator/imports/iop_cdvd.cpp':
        'ca8f524a3b708bca4183c940ecdc244468782c0162e832f13e5ae8f088d26f07',
    'ps2xRuntime/src/lib/Kernel/Stubs/Helpers/Support.h':
        'b1640bdc33adb42e7873ff501ed5a07657a2b21fede3c5448d86f15ce34699db',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    for name, expected in EXPECTED.items():
        # Git may check out Windows source with CRLF; verify canonical text.
        data = (args.upstream / name).read_bytes().replace(b'\r\n', b'\n')
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit(f'Unexpected upstream source; preserved without modification: {name}')
    args.output.mkdir(parents=True, exist_ok=True)
    patch = Path(__file__).resolve().parent / 'patches/disc-layout.patch'
    with tempfile.TemporaryDirectory(prefix='openkai2-overlays-', dir=args.output.parent) as folder:
        temporary = Path(folder)
        shutil.copytree(args.upstream / 'ps2xRuntime/src/lib/Kernel/Stubs', temporary / 'Kernel/Stubs')
        shutil.copy2(args.upstream / 'ps2xIOP/src/emulator/imports/iop_cdvd.cpp', temporary / 'iop_cdvd.cpp')
        for source in temporary.rglob('*'):
            if source.is_file() and source.suffix in ('.h', '.cpp', '.hpp', '.inc'):
                source.write_bytes(source.read_bytes().replace(b'\r\n', b'\n'))
        subprocess.run(['git', 'apply', '--no-index', '--check', str(patch)], cwd=temporary, check=True)
        subprocess.run(['git', 'apply', '--no-index', str(patch)], cwd=temporary, check=True)
        for source in temporary.rglob('*'):
            if source.is_file():
                target = args.output / source.relative_to(temporary)
                data = source.read_bytes()
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists() or target.read_bytes() != data:
                    target.write_bytes(data)


if __name__ == '__main__':
    main()
