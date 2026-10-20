#!/usr/bin/env python3
"""Launch the real ELF and retain bounded evidence of startup failures."""
from pathlib import Path
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time

BASE = Path('/workspace/.onboarding/budokai2-native')
reason = None
process = subprocess.Popen(['bash', str(BASE / 'run-native.sh')], cwd=BASE,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors='replace', start_new_session=True)
start = time.monotonic()

def stop():
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass

def timed_out():
    global reason
    if reason is None:
        reason = 'TIME_LIMIT_WITHOUT_BOOT_PROOF'
    stop()

timer = threading.Timer(30, timed_out)
timer.start()
retained = []
size = 0
missing = set()
try:
    with (BASE / 'startup-verified.log').open('w') as log:
        for line in process.stdout:
            if size < 2_000_000:
                log.write(line)
                retained.append(line)
                size += len(line)
            for path in re.findall(r"failed to open IRX '([^']+)'", line):
                missing.add(path)
            if reason is None:
                if '[IOP:load-failed]' in line and missing:
                    reason = 'BLOCKED_MISSING_DISC_MODULE'
                    stop()
                elif 'Error during program execution:' in line or '[main] fatal exception:' in line:
                    reason = 'NATIVE_EXECUTION_ERROR'
                    stop()
                elif size >= 2_000_000:
                    reason = 'LOG_LIMIT_WITHOUT_BOOT_PROOF'
                    stop()
    code = process.wait(timeout=5)
finally:
    timer.cancel()
    stop()
text = ''.join(retained)
report = {
    'status': reason or 'EXITED_WITHOUT_BOOT_PROOF',
    'elapsed_seconds': round(time.monotonic() - start, 2),
    'process_exit_code': code,
    'elf_loaded': 'ELF file loaded successfully' in text,
    'entry_execution_started': 'Starting execution at address 0x100008' in text,
    'opengl_initialized': 'OpenGL extensions loaded successfully' in text,
    'software_renderer': 'llvmpipe' in text,
    'audio_initialized_with_null_output': 'AUDIO: Device initialized successfully' in text,
    'missing_disc_modules': sorted(missing),
    'game_boot_verified': False,
    'note': 'Native entry execution is demonstrated; title screen and gameplay are unverified. '
            'A missing-module run is stopped deliberately to avoid an unbounded retry log.',
}
(BASE / 'startup-status.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
sys.exit(3 if reason == 'BLOCKED_MISSING_DISC_MODULE' else 1)
