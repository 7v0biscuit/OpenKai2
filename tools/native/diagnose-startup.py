#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run ISO-required startup with a bounded log on Windows or Linux."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time

SOURCE = Path(__file__).resolve().parent
WORK = Path(os.environ.get('OPENKAI2_WORK_DIR', SOURCE.parents[1] / '.local/openkai2')).resolve()
BASE = WORK / 'budokai2-native'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path, help='Original Budokai 2 USA ISO; required on every launch')
    parser.add_argument('--timeout', type=int, default=180,
                        help='Total verification/startup time limit in seconds (default: 180)')
    args = parser.parse_args()
    if not args.iso.is_file():
        parser.error('The original game ISO must be present; an extracted ELF or cache cannot replace it.')
    if args.timeout < 1:
        parser.error('--timeout must be positive')
    if os.name == 'nt':
        command = [str(BASE / 'OpenKai2.exe'), str(args.iso.resolve())]
        options = {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP}
    else:
        command = ['bash', str(SOURCE / 'run-native.sh'), str(args.iso.resolve())]
        options = {'start_new_session': True}
    if not BASE.is_dir():
        parser.error('Build the native runtime before running this diagnostic.')
    reason = None
    retained = []
    size = 0
    missing = set()
    start = time.monotonic()
    process = subprocess.Popen(command, cwd=BASE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors='replace', **options)

    def stop(force=False):
        if process.poll() is not None:
            return
        try:
            if os.name == 'nt':
                process.kill() if force else process.terminate()
            else:
                os.killpg(process.pid, signal.SIGKILL if force else signal.SIGTERM)
        except ProcessLookupError:
            pass

    def timed_out():
        nonlocal reason
        if reason is None:
            reason = 'TIME_LIMIT_WITHOUT_BOOT_PROOF'
        stop()
        # Ensure a stuck child cannot keep its stdout pipe open indefinitely.
        killer = threading.Timer(5, lambda: stop(force=True))
        killer.daemon = True
        killer.start()

    timer = threading.Timer(args.timeout, timed_out)
    timer.start()
    try:
        with (BASE / 'startup-verified.log').open('w', encoding='utf-8') as log:
            for line in process.stdout:
                if size < 2_000_000:
                    log.write(line)
                    retained.append(line)
                    size += len(line.encode('utf-8'))
                for path in re.findall(r"failed to open IRX '([^']+)'", line):
                    missing.add(path)
                if reason is None:
                    if '[IOP:load-failed]' in line and missing:
                        reason = 'BLOCKED_MISSING_DISC_MODULE'
                        stop()
                    elif ('Error during program execution:' in line or '[main] fatal exception:' in line
                          or '[OpenKai2]' in line):
                        reason = 'NATIVE_STARTUP_REJECTED'
                        stop()
                    elif size >= 2_000_000:
                        reason = 'LOG_LIMIT_WITHOUT_BOOT_PROOF'
                        stop()
            try:
                code = process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                stop(force=True)
                code = process.wait(timeout=5)
    finally:
        timer.cancel()
        stop(force=True)
        process.stdout.close()
    text = ''.join(retained)
    report = {
        'status': reason or 'EXITED_WITHOUT_BOOT_PROOF',
        'elapsed_seconds': round(time.monotonic() - start, 2),
        'process_exit_code': code,
        'iso_file_present': args.iso.is_file(),
        'disc_files_verified': '[disc] Verified ' in text,
        'elf_loaded': 'ELF file loaded successfully' in text,
        'entry_execution_started': 'Starting execution at address 0x100008' in text,
        'opengl_initialized': 'OpenGL extensions loaded successfully' in text,
        'audio_initialized': 'AUDIO: Device initialized successfully' in text,
        'missing_disc_modules': sorted(missing),
        'game_boot_verified': False,
        'note': 'ISO verification, entry execution, and device initialization are recorded separately. '
                'Title screen and gameplay require direct verification.',
    }
    (BASE / 'startup-status.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 3 if reason == 'BLOCKED_MISSING_DISC_MODULE' else 1


if __name__ == '__main__':
    sys.exit(main())
