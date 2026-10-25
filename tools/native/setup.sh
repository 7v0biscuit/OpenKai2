#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
native_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$native_source/paths.sh"
native_elf=${1:-$native_base/game/SLUS_207.79}
native_upstream=$native_base/deps/PS2Recomp
native_build=$native_base/build-recomp
native_native=$native_base/budokai2-native
native_pin=eb8688b2fd191129f103a39543002c005b33409c
export CMAKE_BUILD_PARALLEL_LEVEL=4
export PYTHONDONTWRITEBYTECODE=1

# Validate input before installation and preserve an existing game file.
python - "$native_elf" <<'PY'
from pathlib import Path
import hashlib, os, shutil, sys
source = Path(sys.argv[1]).resolve()
target = Path(os.environ['OPENKAI2_WORK_DIR']) / 'game/SLUS_207.79'
expected = '431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'
if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
    raise SystemExit('Provide the exact owned Budokai 2 USA SLUS_207.79 executable.')
target.parent.mkdir(parents=True, exist_ok=True)
if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != expected:
    raise SystemExit('A different local game file already exists; it was preserved.')
if not target.exists():
    shutil.copy2(source, target)
PY

if [ ! -x "$native_base/toolchain/bin/python" ]; then
    python -m venv "$native_base/toolchain"
fi
source "$native_base/toolchain/bin/activate"
python -m pip install --no-cache-dir 'cmake==4.4.4' 'ninja==1.13.2' 'libarchive-c==5.3'

# Keep package signatures, package hashes and TLS verification enabled.
python - <<'PY'
from pathlib import Path
import getpass, json, os
base = Path(os.environ['OPENKAI2_WORK_DIR']) / 'sysdeps/apt'
for directory in ['lists/partial', 'archives/partial', 'empty']:
    (base / directory).mkdir(parents=True, exist_ok=True)
files = {
    'empty.conf': '',
    'sources.list': 'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main\ndeb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main\n',
    'apt.conf': '''Dir::Etc::parts "@WORK_DIR@/sysdeps/apt/empty";
Dir::Etc::main "@WORK_DIR@/sysdeps/apt/empty.conf";
Dir::Etc::sourcelist "@WORK_DIR@/sysdeps/apt/sources.list";
Dir::Etc::sourceparts "-";
Dir::State::lists "@WORK_DIR@/sysdeps/apt/lists";
Dir::State::extended_states "@WORK_DIR@/sysdeps/apt/extended_states";
Dir::Cache::archives "@WORK_DIR@/sysdeps/apt/archives";
Dir::Log "@WORK_DIR@/sysdeps/apt/log";
APT::Sandbox::User "agent";
Acquire::Retries "1";
APT::Update::Error-Mode "any";
Debug::NoLocking "true";
''',
}
for name, content in files.items():
    content = content.replace('@WORK_DIR@', os.environ['OPENKAI2_WORK_DIR']).replace('APT::Sandbox::User \"agent\"', 'APT::Sandbox::User \"' + getpass.getuser() + '\"')
    path = base / name
    if path.exists() and path.read_text() != content:
        raise SystemExit('Preserved differing local APT configuration: ' + str(path))
    if not path.exists():
        path.write_text(content)
PY

if [ ! -x "$native_base/sysdeps/root/usr/lib/llvm-19/bin/clang++" ] || \
   [ ! -x "$native_base/sysdeps/root/usr/bin/Xvfb" ] || \
   [ ! -f "$native_base/sysdeps/root/usr/lib/x86_64-linux-gnu/pkgconfig/libavcodec.pc" ]; then
    export APT_CONFIG="$native_base/sysdeps/apt/apt.conf"
    apt-get update
    apt-get --download-only --no-install-recommends --assume-yes --reinstall install \
        libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
        xvfb libgl1-mesa-dri libglx-mesa0 libgl1 libglx0 libavcodec61 libavformat61 \
        libavutil59 libswresample5 libswscale8 libx11-6 libxrandr2 libxinerama1 \
        libxcursor1 libxi6 libxext6 libxfixes3 libxrender1 libxcb1 libxau6 libxdmcp6 \
        clang-19 libclang-cpp19 libllvm19 libclang-common-19-dev
    python - <<'PY'
from pathlib import Path
import os, subprocess
base = Path(os.environ['OPENKAI2_WORK_DIR']) / 'sysdeps'
for archive in sorted((base / 'apt/archives').glob('*.deb')):
    subprocess.run(['dpkg-deb', '--extract', str(archive), str(base / 'root')], check=True)
PY
fi

if [ ! -d "$native_upstream/.git" ]; then
    if [ -e "$native_upstream" ]; then
        printf '%s\n' 'Preserved an existing non-checkout dependency directory.' >&2
        exit 1
    fi
    mkdir -p "$native_upstream"
    git -C "$native_upstream" init
    git -C "$native_upstream" remote add origin https://github.com/ran-j/PS2Recomp.git
    git -C "$native_upstream" fetch --depth 1 origin "$native_pin"
    git -C "$native_upstream" checkout --detach FETCH_HEAD
fi
if [ "$(git -C "$native_upstream" rev-parse HEAD)" != "$native_pin" ]; then
    printf '%s\n' 'Preserved a different PS2Recomp checkout; inspect it before setup.' >&2
    exit 1
fi
git -C "$native_upstream" diff --exit-code
git -C "$native_upstream" diff --cached --exit-code

if [ ! -x "$native_build/ps2xAnalyzer/ps2_analyzer" ] || \
   [ ! -x "$native_build/ps2xRecomp/ps2_recomp" ]; then
    cmake -S "$native_upstream" -B "$native_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPS2X_BUILD_RUNTIME=OFF \
        -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF \
        -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES= \
        -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1
    cmake --build "$native_build" --target ps2_analyzer ps2_recomp --parallel 4
fi

mkdir -p "$native_native"

if [ ! -f "$native_native/native.toml" ] || \
   [ ! -f "$native_native/output/register_functions.cpp" ]; then
    "$native_build/ps2xAnalyzer/ps2_analyzer" \
        "$native_base/game/SLUS_207.79" "$native_native/analyzer.toml" \
        > "$native_native/analyzer.log" 2>&1
    python - <<'PY'
from pathlib import Path
import os
base = Path(os.environ['OPENKAI2_WORK_DIR']) / 'budokai2-native'
text = (base / 'analyzer.toml').read_text()
text = text.replace('patch_cop0 = true', 'patch_cop0 = false')
text = text.replace('patch_cache = true', 'patch_cache = false')
text = text.replace('single_file_output = false',
                    'single_file_output = false\nlow_memory_mode = true\noutput_worker_threads = 2')
start = text.index('[patches]')
end = text.index('[performance]', start)
text = text[:start] + '[patches]\ninstructions = []\n\n' + text[end:]
(base / 'native.toml').write_text(text)
PY
    python -B "$native_source/translation-entries.py" \
        --config "$native_native/native.toml" --elf "$native_base/game/SLUS_207.79"
    "$native_build/ps2xRecomp/ps2_recomp" "$native_native/native.toml" \
        > "$native_native/recompile.log" 2>&1
fi
python -B "$native_source/translation-entries.py" \
    --config "$native_native/native.toml" --elf "$native_base/game/SLUS_207.79"
if ! python -B "$native_source/translation-entries.py" --verify-generated "$native_native/output"; then
    "$native_build/ps2xRecomp/ps2_recomp" "$native_native/native.toml" \
        > "$native_native/recompile.log" 2>&1
    python -B "$native_source/translation-entries.py" --verify-generated "$native_native/output"
fi
bash "$native_source/refresh-native.sh"
printf '%s\n' 'Native build prepared. Run tools/native/diagnose-startup.py for the bounded boot check.'
