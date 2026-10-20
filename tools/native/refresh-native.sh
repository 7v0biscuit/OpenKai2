#!/usr/bin/env bash
set -euo pipefail
native_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$native_source/paths.sh"
native_upstream="$native_base/deps/PS2Recomp"
native_build="$native_base/build-recomp"
native_game="$native_base/budokai2-native"
native_elf="$native_base/game/SLUS_207.79"
source "$native_base/toolchain/bin/activate"
export CMAKE_BUILD_PARALLEL_LEVEL=4
export PYTHONDONTWRITEBYTECODE=1
export PKG_CONFIG_PATH="$native_base/sysdeps/root/usr/lib/x86_64-linux-gnu/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$native_base/sysdeps/root"
if [ "$(git -C "$native_upstream" rev-parse HEAD)" != eb8688b2fd191129f103a39543002c005b33409c ]; then
    printf '%s\n' 'Unexpected PS2Recomp revision; inspect local changes before refreshing.' >&2
    exit 1
fi
python -B - <<'PY'
from pathlib import Path
import hashlib, os
path = Path(os.environ['OPENKAI2_WORK_DIR']) / 'game/SLUS_207.79'
expected = '431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'
if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
    raise SystemExit('The exact owned Budokai 2 USA executable is missing or mismatched.')
PY
for native_required in "$native_game/native.toml" "$native_game/output/register_functions.cpp" \
    "$native_source/attach-game.cmake" "$native_source/gcc-portability.h" \
    "$native_base/sysdeps/root/usr/bin/Xvfb"; do
    if [ ! -f "$native_required" ]; then
        printf 'Missing retained setup file: %s\n' "$native_required" >&2
        exit 1
    fi
done
cmake -S "$native_upstream" -B "$native_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON \
    -DPS2X_BUILD_RUNTIME=ON -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF \
    -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF \
    -DPS2X_ENABLE_IOP_RPC_TRACE=ON -DPS2X_ENABLE_RUNNER_UNITY_BUILD=ON \
    -DPS2X_RUNNER_UNITY_BUILD_BATCH_SIZE=32 -DPS2X_ENABLE_RUNNER_PCH=ON \
    -DPS2X_IOP_BUILD_TESTS=ON -DGLFW_BUILD_WAYLAND=OFF \
    -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1 \
    -DCMAKE_PREFIX_PATH="$native_base/sysdeps/root/usr" \
    -DCMAKE_FIND_ROOT_PATH="$native_base/sysdeps/root" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$native_source/attach-game.cmake"
# A restored CMake-generated header may retain an older timestamp. Refresh a
# stale PCH when it predates the local compatibility header.
native_pch="$native_build/ps2xRuntime/CMakeFiles/ps2EntryRunner.dir/cmake_pch.hxx.gch"
if [ -f "$native_pch" ] && [ "$native_source/gcc-portability.h" -nt "$native_pch" ]; then
    touch "$native_build/ps2xRuntime/CMakeFiles/ps2EntryRunner.dir/cmake_pch.hxx"
fi
python -B "$native_source/build-native.py"
cmake --build "$native_build" --target ps2_iop_emulator_tests \
    ps2_iop_import_tests ps2_iop_compatibility_tests ps2_iop_import_version_tests \
    --parallel 4
ctest --test-dir "$native_build/ps2xIOP" --output-on-failure
