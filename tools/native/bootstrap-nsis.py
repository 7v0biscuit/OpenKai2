#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install the pinned NSIS 3.11 compiler for Linux-hosted Windows packaging."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import urllib.request

PACKAGES = (
    ('nsis_3.11-1_amd64.deb', 568816,
     'e6eb6ffb70f0df09e95dedb2d2cf55ad3c2679f7c2ae357d0c00cc4c83178690'),
    ('nsis-common_3.11-1_all.deb', 1168188,
     '103a3284c1a5356efa0aba90fdfc391c3cde8e7df8726377cef0f98471584047'),
)
BASE_URL = 'https://deb.debian.org/debian/pool/main/n/nsis/'


def verify_package(data, size, digest):
    if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
        raise ValueError('NSIS package verification failed')


def install(destination):
    if platform.system() != 'Linux' or platform.machine().lower() not in ('x86_64', 'amd64'):
        raise ValueError('The pinned packaging compiler requires Linux x64')
    destination = Path(destination).absolute()
    if destination.is_symlink() or (destination.exists() and
                                   (not destination.is_dir() or any(destination.iterdir()))):
        raise ValueError('Choose an empty NSIS compiler destination')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nsis-', dir=destination.parent) as temporary:
        temporary = Path(temporary)
        root = temporary / 'root'
        for name, size, digest in PACKAGES:
            with urllib.request.urlopen(BASE_URL + name, timeout=60) as response:
                data = response.read(size + 1)
            verify_package(data, size, digest)
            package = temporary / name
            package.write_bytes(data)
            subprocess.run(['dpkg-deb', '--extract', str(package), str(root)], check=True)
        env = dict(os.environ, NSISDIR=str(root / 'usr/share/nsis'))
        result = subprocess.run([str(root / 'usr/bin/makensis'), '-VERSION'],
                                env=env, check=True, capture_output=True, text=True)
        if result.stdout.strip() != 'v3.11-1':
            raise ValueError('The packaging compiler reports an unexpected version')
        root.replace(destination)
    print(f'NSIS 3.11 verified: {destination / "usr/bin/makensis"}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', required=True, type=Path)
    args = parser.parse_args()
    try:
        install(args.destination)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
