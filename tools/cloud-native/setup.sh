#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cloud_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cloud_base=/workspace/.onboarding
cloud_elf=${1:-$cloud_base/game/SLUS_207.79}
cloud_upstream=$cloud_base/deps/PS2Recomp
cloud_build=$cloud_base/build-recomp
cloud_native=$cloud_base/budokai2-native
cloud_pin=eb8688b2fd191129f103a39543002c005b33409c
export CMAKE_BUILD_PARALLEL_LEVEL=4
export PYTHONDONTWRITEBYTECODE=1

# Validate input before installation and preserve an existing game file.
python - "$cloud_elf" <<'PY'
from pathlib import Path
import hashlib, shutil, sys
source = Path(sys.argv[1]).resolve()
target = Path('/workspace/.onboarding/game/SLUS_207.79')
expected = '431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'
if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
    raise SystemExit('Provide the exact owned Budokai 2 USA SLUS_207.79 executable.')
target.parent.mkdir(parents=True, exist_ok=True)
if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != expected:
    raise SystemExit('A different local game file already exists; it was preserved.')
if not target.exists():
    shutil.copy2(source, target)
PY

if [ ! -x "$cloud_base/toolchain/bin/python" ]; then
    python -m venv "$cloud_base/toolchain"
fi
source "$cloud_base/toolchain/bin/activate"
python -m pip install --no-cache-dir 'cmake==4.4.4' 'ninja==1.13.2' 'libarchive-c==5.3'

# Keep package signatures, package hashes and TLS verification enabled.
python - <<'PY'
from pathlib import Path
import json
base = Path('/workspace/.onboarding/sysdeps/apt')
for directory in ['lists/partial', 'archives/partial', 'empty']:
    (base / directory).mkdir(parents=True, exist_ok=True)
files = {
    'empty.conf': '',
    'sources.list': 'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main\ndeb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main\n',
    'apt.conf': '''Dir::Etc::parts "/workspace/.onboarding/sysdeps/apt/empty";
Dir::Etc::main "/workspace/.onboarding/sysdeps/apt/empty.conf";
Dir::Etc::sourcelist "/workspace/.onboarding/sysdeps/apt/sources.list";
Dir::Etc::sourceparts "-";
Dir::State::lists "/workspace/.onboarding/sysdeps/apt/lists";
Dir::State::extended_states "/workspace/.onboarding/sysdeps/apt/extended_states";
Dir::Cache::archives "/workspace/.onboarding/sysdeps/apt/archives";
Dir::Log "/workspace/.onboarding/sysdeps/apt/log";
APT::Sandbox::User "agent";
Acquire::Retries "1";
APT::Update::Error-Mode "any";
Debug::NoLocking "true";
''',
}
for name, content in files.items():
    path = base / name
    if path.exists() and path.read_text() != content:
        raise SystemExit('Preserved differing local APT configuration: ' + str(path))
    if not path.exists():
        path.write_text(content)
PY

if [ ! -x "$cloud_base/sysdeps/root/usr/lib/llvm-19/bin/clang++" ] || \
   [ ! -x "$cloud_base/sysdeps/root/usr/bin/Xvfb" ] || \
   [ ! -f "$cloud_base/sysdeps/root/usr/lib/x86_64-linux-gnu/pkgconfig/libavcodec.pc" ]; then
    export APT_CONFIG="$cloud_base/sysdeps/apt/apt.conf"
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
import subprocess
base = Path('/workspace/.onboarding/sysdeps')
for archive in sorted((base / 'apt/archives').glob('*.deb')):
    subprocess.run(['dpkg-deb', '--extract', str(archive), str(base / 'root')], check=True)
PY
fi

if [ ! -d "$cloud_upstream/.git" ]; then
    if [ -e "$cloud_upstream" ]; then
        printf '%s\n' 'Preserved an existing non-checkout dependency directory.' >&2
        exit 1
    fi
    mkdir -p "$cloud_upstream"
    git -C "$cloud_upstream" init
    git -C "$cloud_upstream" remote add origin https://github.com/ran-j/PS2Recomp.git
    git -C "$cloud_upstream" fetch --depth 1 origin "$cloud_pin"
    git -C "$cloud_upstream" checkout --detach FETCH_HEAD
fi
if [ "$(git -C "$cloud_upstream" rev-parse HEAD)" != "$cloud_pin" ]; then
    printf '%s\n' 'Preserved a different PS2Recomp checkout; inspect it before setup.' >&2
    exit 1
fi
git -C "$cloud_upstream" diff --exit-code
git -C "$cloud_upstream" diff --cached --exit-code

if [ ! -x "$cloud_build/ps2xAnalyzer/ps2_analyzer" ] || \
   [ ! -x "$cloud_build/ps2xRecomp/ps2_recomp" ]; then
    cmake -S "$cloud_upstream" -B "$cloud_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPS2X_BUILD_RUNTIME=OFF \
        -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF \
        -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES= \
        -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1
    cmake --build "$cloud_build" --target ps2_analyzer ps2_recomp --parallel 4
fi

mkdir -p "$cloud_native"
for cloud_helper in attach-game.cmake build-native.py gcc-portability.h \
    refresh-native.sh run-native.sh diagnose-startup.py asound.conf; do
    if [ -e "$cloud_native/$cloud_helper" ]; then
        if ! cmp -s "$cloud_source/$cloud_helper" "$cloud_native/$cloud_helper"; then
            printf 'Preserved differing native helper: %s\n' "$cloud_native/$cloud_helper" >&2
            exit 1
        fi
    else
        cp "$cloud_source/$cloud_helper" "$cloud_native/$cloud_helper"
    fi
done

if [ ! -f "$cloud_native/native.toml" ] || \
   [ ! -f "$cloud_native/output/register_functions.cpp" ]; then
    "$cloud_build/ps2xAnalyzer/ps2_analyzer" \
        "$cloud_base/game/SLUS_207.79" "$cloud_native/analyzer.toml" \
        > "$cloud_native/analyzer.log" 2>&1
    python - <<'PY'
from pathlib import Path
base = Path('/workspace/.onboarding/budokai2-native')
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
    "$cloud_build/ps2xRecomp/ps2_recomp" "$cloud_native/native.toml" \
        > "$cloud_native/recompile.log" 2>&1
fi
bash "$cloud_native/refresh-native.sh"
printf '%s\n' 'Native build prepared. Run diagnose-startup.py for the bounded boot check.'
