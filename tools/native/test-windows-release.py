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
        audio_status = run([str(executable), '--audio-test'])
        if 'Host audio is disabled in this build. No output device is opened.' not in audio_status:
            raise RuntimeError('Installed application did not report disabled host audio.')
        self_test = run([str(executable), '--self-test'])
        for marker in ('Runtime attack effect regression passed', 'Runtime tested pixel regression passed', 'Runtime fight rule regression passed', 'Runtime draw history regression passed', 'Runtime skeletal transform regression passed', 'Runtime battle draw regression passed', 'Runtime battle update regression passed', 'Runtime prepared palette regression passed', 'Runtime early depth regression passed', 'Runtime aura update regression passed', 'Runtime battle command regression passed', 'Runtime prepared pixel regression passed', 'Runtime scene mode regression passed', 'Runtime buffered audio regression passed', 'Runtime battle asset regression passed', 'Runtime battle initialization regression passed',
                       'Runtime streamed audio regression passed', 'Runtime IOP import cache regression passed'):
            if marker not in self_test:
                raise RuntimeError('Installed application is missing required battle/audio execution checks: '+marker)
        if 'Runtime versus animation regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the original versus animation and indirect caller regression')
        if 'Runtime triangle pipeline regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the triangle pipeline and sampler residency comparisons')
        if 'Runtime pixel blending regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the scalar pixel blending comparisons')
        if 'Runtime draw setup regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the original draw setup and indirect caller regression')
        if 'Runtime DMA critical section regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the compiled DMA interrupt masking regression')
        if 'Runtime parallel raster regression passed' not in self_test:
            raise RuntimeError('Installed application is missing parallel raster and serial-reference comparisons')
        if 'Runtime save menu regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the original save-menu transition regression')
        if 'Runtime frame gate regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the empty DMA queue and VBlank frame regression')
        if 'Runtime interlaced presentation regression passed' not in self_test:
            raise RuntimeError('Half-height interlaced framebuffer presentation regression did not pass.')
        if 'Runtime movie timing regression passed' not in self_test:
            raise RuntimeError('Installed application did not pass the original movie timing dispatch regression')
        if 'Runtime movie packet regression passed' not in self_test:
            raise RuntimeError('Installed application did not pass the original movie packet dispatch regression')
        if 'Runtime frame scheduling regression passed' not in self_test:
            raise RuntimeError('Installed application did not pass the compiled frame scheduling regression')
        if 'Runtime worker wake regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the compiled worker wake regression')
        if 'Runtime field stability regression passed' not in self_test:
            raise RuntimeError('Installed application is missing the field stability regression')
        if 'Runtime sprite coordinate regression passed' not in self_test:
            raise RuntimeError('Fractional sprite position and texture-coordinate regression did not pass.')
        if 'Runtime MPEG decode regression passed' not in self_test:
            raise RuntimeError('Original CRI input ring, IPU DMA, and real MPEG picture decoding regression did not pass.')
        if 'Runtime MPEG scheduling regression passed' not in self_test:
            raise RuntimeError('Synchronous MPEG decoding, end of stream, and compiled movie worker handoff regression did not pass.')
        if 'Runtime MPEG setup regression passed' not in self_test:
            raise RuntimeError('Original MPEG SDK work-area and callback regression did not pass.')
        if 'Runtime movie transition diagnostics regression passed' not in self_test:
            raise RuntimeError('Movie activation and live-state diagnostics regression did not pass.')
        if 'Runtime graphics presentation regression passed' not in self_test:
            raise RuntimeError('Original draw packet presentation regression did not pass.')
        if 'Runtime movie callback regression passed' not in self_test:
            raise RuntimeError('Compiled movie callback regression did not pass.')
        if 'Runtime fade callback regression passed' not in self_test:
            raise RuntimeError('Compiled fade callback regression did not pass.')
        if 'Runtime asset relocation regression passed' not in self_test:
            raise RuntimeError('Compiled asset relocation regression did not pass.')
        if 'Runtime sound transfer regression passed' not in self_test:
            raise RuntimeError('Compiled sound transfer regression did not pass.')
        if 'Runtime stream callback regression passed' not in self_test:
            raise RuntimeError('Compiled stream interface regression did not pass.')
        for marker in ('Runtime input regression passed', 'Runtime save regression passed',
                       'Runtime sprite pipeline regression passed',
                       'Runtime asynchronous presentation regression passed', 'Runtime texture sampling regression passed'):
            if marker not in self_test:
                raise RuntimeError('Installed application did not pass ' + marker)
        if 'Runtime movie cleanup regression passed' not in self_test:
            raise RuntimeError('Original movie teardown and memory-stream interface regression did not pass.')
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
