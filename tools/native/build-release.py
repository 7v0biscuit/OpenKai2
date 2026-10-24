#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build portable and installer EXEs from a verified Windows runtime ZIP."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('windows_package', Path(__file__).with_name('package-windows.py'))
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def extract_verified_bundle(bundle, destination):
    destination = Path(destination)
    names = set()
    with zipfile.ZipFile(bundle) as archive:
        for entry in archive.infolist():
            path = PurePosixPath(entry.filename)
            if (not path.parts or path.parts[0] != 'OpenKai2' or path.is_absolute()
                    or any(part in ('..', '.') for part in path.parts)
                    or any(ch in entry.filename for ch in '\\:\r\n')
                    or stat.S_ISLNK(entry.external_attr >> 16)):
                raise ValueError(f'Unsafe bundle path: {entry.filename}')
            key = entry.filename.casefold()
            if key in names:
                raise ValueError(f'Duplicate bundle path: {entry.filename}')
            names.add(key)
        archive.extractall(destination)
    root = destination / 'OpenKai2'
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    expected = manifest['files_sha256']
    actual = {path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file()}
    if actual != set(expected) | {'manifest.json'}:
        raise ValueError('Bundle manifest does not describe every payload file')
    for name, digest in expected.items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Bundle hash mismatch: {name}')
    PACKAGE.dependency_files(root / 'OpenKai2.exe', [root])
    for required in ('LICENSE', 'NOTICE', 'README.txt'):
        if required not in actual:
            raise ValueError(f'Bundle is missing {required}')
    if manifest.get('architecture') != 'windows-x64':
        raise ValueError('Expected a Windows x64 bundle')
    if PACKAGE.binary_source_revision(root / 'OpenKai2.exe') != manifest.get('source_revision'):
        raise ValueError('Bundle native build revision does not match its manifest')
    return root, manifest


def nsis_literal(value):
    if '\r' in str(value) or '\n' in str(value):
        raise ValueError('NSIS values must fit on one line')
    return str(value).replace('$', '$$').replace('"', '$\\"')


def uninstall_commands(root):
    files = [path.relative_to(root) for path in root.rglob('*') if path.is_file()]
    commands = [f'Delete "$INSTDIR\\{nsis_literal(path.as_posix().replace("/", chr(92)))}"'
                for path in sorted(files)]
    directories = [path.relative_to(root) for path in root.rglob('*') if path.is_dir()]
    for path in sorted(directories, key=lambda item: (len(item.parts), item.as_posix()), reverse=True):
        commands.append(f'RMDir "$INSTDIR\\{nsis_literal(path.as_posix().replace("/", chr(92)))}"')
    return '\n'.join(commands) + '\n'


def release_notes(version, manifest):
    changelog = ROOT / 'docs/release-notes' / f'{version}.md'
    changes = changelog.read_text(encoding='utf-8').strip() + '\n\n' if changelog.is_file() else ''
    return f'''OpenKai2 {version} for Windows x64.

{changes}Full gameplay and stable 60 FPS remain unverified. An original Budokai 2 USA ISO is required at every launch; disc images and original assets are not included.

- **OpenKai2.exe**: a single-file portable launcher that extracts its runtime temporarily and opens the ISO picker. No installation is required.
- **OpenKai2-windows-x64.zip**: extract the entire archive and run OpenKai2.exe. Keep the included DLLs together.
- **OpenKai2-Setup.exe**: installs for the current Windows user and adds Start-menu and uninstall entries. No administrator account is required.
- **SHA256SUMS.txt**: SHA-256 checksums for the three downloads.

Requires Windows 10/11 x64 and an SSE4.1-capable CPU. These development executables are unsigned.

Startup log: `%LOCALAPPDATA%\\OpenKai2\\logs\\startup.log`.

Runtime bundle source: `{manifest.get('source_revision', 'unknown')}`.
The release workflow checks the payload hashes and DLL dependencies and tests compiled callback execution, portable launch, installation, installed launch, and uninstall on Windows before publication.
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--makensis', default='makensis')
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', args.version):
        parser.error('--version must be a semantic version without a leading v')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='openkai2-release-') as temporary:
        temporary = Path(temporary)
        payload, manifest = extract_verified_bundle(args.bundle, temporary)
        delete_files = temporary / 'delete-files.nsh'
        delete_files.write_text(uninstall_commands(payload), encoding='utf-8')
        installed_kib = sum(path.stat().st_size for path in payload.rglob('*') if path.is_file()) // 1024 + 1
        numeric = args.version.split('-')[0] + '.0'
        for template, filename in [('portable.nsi', 'OpenKai2.exe'), ('installer.nsi', 'OpenKai2-Setup.exe')]:
            script = temporary / template
            values = {'VERSION': args.version, 'VERSION_NUMERIC': numeric,
                      'PAYLOAD_DIR': str(payload), 'OUTPUT_FILE': str(output / filename),
                      'DELETE_FILES': str(delete_files), 'INSTALLED_KIB': str(installed_kib)}
            definitions = ''.join(f'!define {key} "{nsis_literal(value)}"\n' for key, value in values.items())
            script.write_text(definitions + (ROOT / 'packaging/windows' / template).read_text(), encoding='utf-8')
            subprocess.run([args.makensis, '-V2', str(script)], check=True)
        shutil.copy2(args.bundle, output / 'OpenKai2-windows-x64.zip')
        (output / 'release-notes.md').write_text(release_notes(args.version, manifest), encoding='utf-8')
        names = ['OpenKai2.exe', 'OpenKai2-windows-x64.zip', 'OpenKai2-Setup.exe']
        checksums = ''.join(f'{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n' for name in names)
        (output / 'SHA256SUMS.txt').write_text(checksums, encoding='ascii')
        print(f'Created release assets in {output}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
