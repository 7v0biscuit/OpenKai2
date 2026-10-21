#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise release launchers, install, and uninstall on an isolated Windows runner."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(command, expected=0, capture=True):
    result = subprocess.run(command, check=False, timeout=180, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None)
    if result.returncode != expected:
        raise RuntimeError(f'Exit {result.returncode}, expected {expected}: {command}\n{result.stdout or ""}')
    return result.stdout or ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Run this integration test on an isolated Windows runner.')
    import winreg
    assets = args.assets.resolve()
    for line in (assets / 'SHA256SUMS.txt').read_text().splitlines():
        digest, name = line.split('  ', 1)
        if hashlib.sha256((assets / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Checksum mismatch: {name}')
    key = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenKai2'
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, access=winreg.KEY_READ | winreg.KEY_WOW64_64KEY):
            raise RuntimeError('Preserved an existing OpenKai2 installation; use an isolated Windows runner.')
    except FileNotFoundError:
        pass
    with tempfile.TemporaryDirectory(prefix='openkai2-release-test-') as temporary:
        root = Path(temporary)
        installed = root / 'App with spaces'
        setup = assets / 'OpenKai2-Setup.exe'
        # NSIS consumes the unquoted remainder after its final /D= argument.
        run(subprocess.list2cmdline([str(setup), '/S']) + ' /D=' + str(installed))
        executable = installed / 'OpenKai2.exe'
        if not executable.is_file():
            raise RuntimeError('Installer did not honor the destination directory.')
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, access=winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as handle:
            if Path(winreg.QueryValueEx(handle, 'InstallLocation')[0]) != installed:
                raise RuntimeError('Uninstall registry entry has the wrong installation path.')
        help_text = run([str(executable), '--help'])
        if 'The original game ISO is required' not in help_text:
            raise RuntimeError('Installed executable did not print the expected help.')
        self_test = run([str(executable), '--self-test'])
        if 'Runtime thread bootstrap regression passed' not in self_test:
            raise RuntimeError('Compiled EE worker initialization regression did not pass.')
        if 'Runtime callback stack regression passed' not in self_test:
            raise RuntimeError('Compiled callback stack preservation regression did not pass.')
        if 'Startup diagnostics regression passed' not in self_test:
            raise RuntimeError('Bounded startup diagnostic regression did not pass.')
        if 'Runtime scratchpad switch regression passed' not in self_test:
            raise RuntimeError('Compiled scratchpad save/restore regression did not pass.')
        if 'IOP wait diagnostics regression passed' not in self_test:
            raise RuntimeError('Compiled IOP wait diagnostics regression did not pass.')
        if 'IOP import pipeline regression passed' not in self_test:
            raise RuntimeError('Compiled IOP import pipeline regression did not pass.')
        if 'IOP module startup regression passed' not in self_test:
            raise RuntimeError('Compiled IOP module startup regression did not pass.')
        if 'SPU2 initialization regression passed' not in self_test:
            raise RuntimeError('Compiled SPU2 initialization regression did not pass.')
        if 'Runtime callback regression passed' not in self_test:
            raise RuntimeError('Compiled startup callback regression did not pass.')
        if 'Runtime dispatch table regression passed' not in self_test:
            raise RuntimeError('Compiled indirect method dispatch regression did not pass.')
        if 'Runtime file callback regression passed' not in self_test:
            raise RuntimeError('Compiled file callback regression did not pass.')
        if 'Runtime scratchpad DMA regression passed' not in self_test:
            raise RuntimeError('Compiled scratchpad DMA regression did not pass.')
        missing = root / 'missing-game.iso'
        run([str(executable), str(missing)], expected=1)
        log = Path(os.environ['LOCALAPPDATA']) / 'OpenKai2/logs/startup.log'
        if 'Game ISO is required' not in log.read_text(errors='replace'):
            raise RuntimeError('Installed executable did not retain its startup failure.')
        run([str(assets / 'OpenKai2.exe'), '--help'])
        run([str(assets / 'OpenKai2.exe'), str(missing)], expected=1)
        # Uninstall must preserve unrelated files placed in the chosen folder.
        preserved = installed / 'user-note.txt'
        preserved.write_text('Keep this file.', encoding='utf-8')
        uninstaller = root / 'RunUninstall.exe'
        shutil.copy2(installed / 'Uninstall.exe', uninstaller)
        run(subprocess.list2cmdline([str(uninstaller), '/S']) + ' _?=' + str(installed))
        if executable.exists() or (installed / 'Uninstall.exe').exists():
            raise RuntimeError('Uninstall left application executables behind.')
        if preserved.read_text(encoding='utf-8') != 'Keep this file.':
            raise RuntimeError('Uninstall removed an unrelated user file.')
        remaining = {path.relative_to(installed).as_posix() for path in installed.rglob('*')}
        if remaining != {'user-note.txt'}:
            raise RuntimeError(f'Unexpected files remain after uninstall: {remaining}')
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, access=winreg.KEY_READ | winreg.KEY_WOW64_64KEY):
                raise RuntimeError('Uninstall registry entry was not removed.')
        except FileNotFoundError:
            pass
    print('Release checks passed: checksums, installed launch, portable launch, ISO rejection, registry, and safe uninstall.')


if __name__ == '__main__':
    main()
