#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the bundled Windows update worker against isolated disposable copies."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import traceback
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def script_text():
    text = (ROOT / 'src/launcher/update_script.h').read_text(encoding='utf-8')
    return text.split('R"OPENKAI2(', 1)[1].split(')OPENKAI2"', 1)[0]


def main():
    if sys.platform != 'win32':
        raise SystemExit('This integration check requires Windows PowerShell 5.1')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateEventW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    with tempfile.TemporaryDirectory(prefix='OpenKai2 updater ') as temporary:
        base = Path(temporary) / 'Test 日本語'
        base.mkdir()
        script = base / 'apply.ps1'
        script.write_text(script_text(), encoding='ascii')
        case_number = 0

        def run(files=None, *, portable=False, bad_hash=False, unsafe=False, bad_payload=False,
                locked=False):
            nonlocal case_number
            case_number += 1
            print(f'Update case {case_number}: portable={portable}, bad_hash={bad_hash}, '
                  f'unsafe={unsafe}, bad_payload={bad_payload}, locked={locked}', flush=True)
            stage = base / str(case_number)
            target = stage / 'Application folder'
            target.mkdir(parents=True)
            (target / 'OpenKai2.exe').write_bytes(b'old executable')
            (target / 'NOTICE').write_bytes(b'old notice')
            (target / 'custom.txt').write_bytes(b'user file')
            source = stage / ('package.exe' if portable else 'package.zip')
            if portable:
                source.write_bytes(b'new portable executable')
            else:
                payload = files or {'NOTICE': b'new notice', 'OpenKai2.exe': b'new executable',
                                    'codec.dll': b'new dependency'}
                manifest = {'architecture': 'windows-x64', 'version': '0.1.26',
                            'files_sha256': {name: hashlib.sha256(data).hexdigest()
                                             for name, data in payload.items()}}
                with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as archive:
                    for name, data in payload.items():
                        if bad_payload and name == 'OpenKai2.exe':
                            data += b'tampered'
                        archive.writestr('OpenKai2/' + name, data)
                    archive.writestr('OpenKai2/manifest.json', json.dumps(manifest))
                    if unsafe:
                        archive.writestr('OpenKai2/../escaped', b'unsafe')
            digest = hashlib.sha256(source.read_bytes()).hexdigest() if not bad_hash else '0' * 64
            name = 'Local\\OpenKai2.UpdateTest.' + uuid.uuid4().hex
            ready = kernel.CreateEventW(None, True, False, name)
            assert ready, ctypes.get_last_error()
            lock = (target / 'OpenKai2.exe').open('rb') if locked else None
            try:
                process = subprocess.Popen([str(powershell), '-NoProfile', '-NonInteractive',
                    '-ExecutionPolicy', 'Bypass', '-File', str(script), '-ParentId', '1073741823',
                    '-ReadyEvent', name, '-Package', str(source), '-Expected', digest,
                    '-Target', str(target / 'OpenKai2.exe' if portable else target),
                    '-Version', '0.1.26', '-Portable', str(int(portable)), '-NoRestart'],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                signaled = kernel.WaitForSingleObject(ready, 30000) == 0
                out, err = process.communicate(timeout=60)
                assert signaled, ('Update worker did not become ready', out, err)
            finally:
                if lock:
                    lock.close()
                kernel.CloseHandle(ready)
            success = not (bad_hash or unsafe or bad_payload or locked)
            assert (process.returncode == 0) == success, (process.returncode, out, err,
                (stage / 'apply.log').read_text() if (stage / 'apply.log').exists() else 'no worker log')
            assert (target / 'custom.txt').read_bytes() == b'user file'
            if success:
                assert (target / 'OpenKai2.exe').read_bytes() == (
                    b'new portable executable' if portable else b'new executable')
                if not portable:
                    assert (target / 'NOTICE').read_bytes() == b'new notice'
                    assert (target / 'codec.dll').read_bytes() == b'new dependency'
            else:
                assert (target / 'OpenKai2.exe').read_bytes() == b'old executable'
                assert (target / 'NOTICE').read_bytes() == b'old notice', 'Rollback did not restore an earlier replaced file'
                assert not (stage / 'escaped').exists()
            assert not (base / 'escaped').exists()

        run()
        run(portable=True)
        run(bad_hash=True)
        run(unsafe=True)
        run(bad_payload=True)
        run(locked=True)
        print('Windows update worker: ZIP and portable replacement, checksums, archive traversal, '
              'manifest integrity, rollback after a locked file, Unicode paths and user-file preservation passed.')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    try:
        main()
    except Exception:
        detail = traceback.format_exc()
        print(detail, flush=True)
        for start in range(0, len(detail), 3000):
            part = detail[start:start+3000].replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
            print('::error title=Windows update worker::' + part, flush=True)
        raise
