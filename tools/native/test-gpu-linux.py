#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and require the headless GPU comparisons without game data."""
import argparse
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PIN = 'eb8688b2fd191129f103a39543002c005b33409c'


def run(*command, env=None):
    subprocess.run([str(value) for value in command], env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, default=ROOT / '.local/gpu-tests')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    upstream = work / 'deps/PS2Recomp'
    if not upstream.exists():
        upstream.mkdir(parents=True)
        run('git', '-C', upstream, 'init')
        run('git', '-C', upstream, 'remote', 'add', 'origin', 'https://github.com/ran-j/PS2Recomp.git')
        run('git', '-C', upstream, 'fetch', '--depth', '1', 'origin', PIN)
        run('git', '-C', upstream, 'checkout', '--detach', 'FETCH_HEAD')
    revision = subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != PIN:
        raise RuntimeError('Preserved an unexpected dependency checkout; use a different work directory.')
    run('git', '-C', upstream, 'diff', '--exit-code')
    run('git', '-C', upstream, 'diff', '--cached', '--exit-code')
    build = work / 'gpu-build'
    env = os.environ.copy()
    env['OPENKAI2_WORK_DIR'] = str(work)
    env['LIBGL_ALWAYS_SOFTWARE'] = '1'
    env['MESA_SHADER_CACHE_DIR'] = str(work / 'gpu-shader-cache')
    compiler_options = []
    if platform.machine().lower() in {'x86_64', 'amd64'}:
        # The runtime uses SSE4.1 intrinsics, including in public inline helpers.
        compiler_options.append('-DCMAKE_CXX_FLAGS=-msse4.1 -fno-strict-aliasing -fwrapv')
    run('cmake', '-S', upstream, '-B', build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DPS2X_BUILD_RUNTIME=ON',
        '-DPS2X_BUILD_RECOMP=OFF', '-DPS2X_BUILD_ANALYZER=OFF', '-DPS2X_BUILD_STUDIO=OFF',
        '-DPS2X_BUILD_TEST=OFF', '-DPS2X_ENABLE_DEBUG_UI=OFF',
        '-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF', '-DOPENKAI2_RUNTIME_TESTS_ONLY=ON',
        f'-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES={ROOT / "tools/native/attach-game.cmake"}',
        *compiler_options, env=env)
    run('cmake', '--build', build, '--target', 'openkai2_gpu_tests', '--parallel', args.jobs, env=env)
    run(build / 'openkai2_gpu_tests', '--required', env=env)


if __name__ == '__main__':
    main()
