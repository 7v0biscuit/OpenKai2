#!/usr/bin/env python3
"""Build the CMake-selected runtime with Clang for one oversized generated unit."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import sys

SOURCE = Path(__file__).resolve().parent
WORK = Path(os.environ.get('OPENKAI2_WORK_DIR', SOURCE.parents[1] / '.local/openkai2')).resolve()
BASE = WORK / 'budokai2-native'
BUILD = WORK / 'build-recomp'
NINJA = str(WORK / 'toolchain/bin/ninja')
CLANG = str(WORK / 'sysdeps/root/usr/lib/llvm-19/bin/clang++')
OBJ = BASE / 'clang-large-unit.o'
DEP = BASE / 'clang-large-unit.d'
STATE = BASE / 'clang-large-unit-state.json'

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def run(args):
    print('+', shlex.join(map(str, args)), flush=True)
    subprocess.run(args, cwd=BUILD, check=True)

def inputs():
    if not DEP.exists():
        return {}
    text = DEP.read_text().replace('\\\n', ' ')
    result = {}
    for name in shlex.split(text.split(':', 1)[1]):
        path = Path(name)
        if not path.is_absolute():
            path = BUILD / path
        result[str(path)] = digest(path)
    return result

def main():
    output = subprocess.run([NINJA, '-t', 'commands', 'ps2EntryRunner'],
                            cwd=BUILD, text=True, capture_output=True, check=True).stdout
    commands = [shlex.split(line) for line in output.splitlines()]
    large = []
    for command in commands:
        if '-c' not in command or '-o' not in command:
            continue
        source = Path(command[command.index('-c') + 1])
        if not source.is_absolute():
            source = BUILD / source
        if source.name == 'sub_002C2824_0x2c2824.cpp':
            large.append(command)
        elif source.name.startswith('unity_') and source.is_file():
            if 'sub_002C2824_0x2c2824.cpp' in source.read_text():
                large.append(command)
    if len(large) != 1:
        raise RuntimeError('Expected exactly one compile unit containing the oversized translation')
    original = large[0]
    old_object = original[original.index('-o') + 1]
    links = [command for command in commands
             if '-o' in command and command[command.index('-o') + 1] == 'ps2xRuntime/ps2EntryRunner']
    if len(links) != 1:
        raise RuntimeError('Expected exactly one CMake native link command')
    link = links[0]
    if link[:2] == [':', '&&'] and link[-2:] == ['&&', ':']:
        link = link[2:-2]
    if any(operator in link for operator in ('&&', ';', '|')):
        raise RuntimeError('Unexpected shell operator in link rule')
    targets = [name for name in link if name.endswith(('.o', '.a')) and name != old_object]
    run([NINJA, '-j4', *dict.fromkeys(targets)])

    compile_args = original.copy()
    compile_args[0] = CLANG
    for flag, value in (('-o', str(OBJ)), ('-MF', str(DEP)), ('-MT', str(OBJ)),
                        ('-include', str(SOURCE / 'gcc-portability.h'))):
        compile_args[compile_args.index(flag) + 1] = value
    current_inputs = inputs()
    try:
        state = json.loads(STATE.read_text())
    except (OSError, ValueError):
        state = {}
    valid = (OBJ.is_file() and current_inputs and state.get('command') == compile_args
             and state.get('inputs') == current_inputs and state.get('object_sha256') == digest(OBJ))
    if not valid:
        run(compile_args)
        current_inputs = inputs()
    else:
        print('Reusing the verified Clang object; source/header hashes and compiler command recorded.', flush=True)
    STATE.write_text(json.dumps({'command': compile_args, 'inputs': current_inputs,
                                'object_sha256': digest(OBJ)}, indent=2) + '\n')
    for index, value in enumerate(link):
        if value == old_object:
            link[index] = str(OBJ)
    link[link.index('-o') + 1] = str(BASE / 'budokai2-native')
    run(link)
    print('Native executable:', BASE / 'budokai2-native')

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
