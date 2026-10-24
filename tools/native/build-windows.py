#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Windows runtime locally from the user's original game ISO."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PIN = 'eb8688b2fd191129f103a39543002c005b33409c'
ELF_SHA256 = '431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'


def run(*command, log=None):
    print('+', subprocess.list2cmdline([str(x) for x in command]), flush=True)
    if log is None:
        subprocess.run([str(x) for x in command], check=True)
    else:
        print('Log:', log, flush=True)
        with log.open('w', encoding='utf-8') as output:
            subprocess.run([str(x) for x in command], check=True,
                           stdout=output, stderr=subprocess.STDOUT)


def vc_runtime_directories():
    installation_root = os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')
    vswhere = Path(installation_root) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if not vswhere.is_file():
        return []
    installation = subprocess.check_output(
        [str(vswhere), '-latest', '-products', '*', '-requires',
         'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
        text=True).strip()
    if not installation:
        return []
    return sorted((Path(installation) / 'VC/Redist/MSVC').glob('*/x64/Microsoft.VC*.CRT'), reverse=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, help='Original Budokai 2 USA ISO; required for the game build')
    parser.add_argument('--work-dir', type=Path,
                        default=Path(os.environ.get('OPENKAI2_WORK_DIR', ROOT / '.local/openkai2')))
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--tools-only', action='store_true', help='Build and test development tools without game data')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Run this builder on 64-bit Windows with Visual Studio 2022 C++ build tools.')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if not args.tools_only and (not args.iso or not args.iso.is_file()):
        parser.error('Provide the original game ISO with --iso; an extracted ELF cannot replace it.')
    for tool in ('git', 'cmake'):
        if not shutil.which(tool):
            parser.error(f'{tool} must be installed and available on PATH')
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    os.environ['OPENKAI2_WORK_DIR'] = work.as_posix()
    upstream = work / 'deps/PS2Recomp'
    build = work / 'windows-build'
    components = work / 'windows-components'
    native = work / 'budokai2-native'
    native.mkdir(parents=True, exist_ok=True)
    generator = ('-G', 'Visual Studio 17 2022', '-A', 'x64')
    configuration = 'RelWithDebInfo'

    if not upstream.exists():
        upstream.mkdir(parents=True)
        run('git', '-C', upstream, 'init')
        run('git', '-C', upstream, 'remote', 'add', 'origin', 'https://github.com/ran-j/PS2Recomp.git')
        run('git', '-C', upstream, 'fetch', '--depth', '1', 'origin', PIN)
        run('git', '-C', upstream, 'checkout', '--detach', 'FETCH_HEAD')
    head = subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        raise RuntimeError('Preserved an unexpected PS2Recomp checkout; use a different work directory.')
    run('git', '-C', upstream, 'diff', '--exit-code')
    run('git', '-C', upstream, 'diff', '--cached', '--exit-code')

    run('cmake', '-S', ROOT, '-B', components, *generator)
    run('cmake', '--build', components, '--config', configuration, '--parallel', args.jobs)
    run('ctest', '--test-dir', components, '-C', configuration, '--output-on-failure', '--no-tests=error')

    if not args.tools_only:
        manifest = native / 'disc-files.json'
        run(components / configuration / 'openkai2_disc_verify.exe', args.iso.resolve(), '--manifest', manifest)
        executable = Path(json.loads(manifest.read_text())['executable'])
        if hashlib.sha256(executable.read_bytes()).hexdigest() != ELF_SHA256:
            raise RuntimeError('Disc verification did not return the supported USA executable.')

    # Analysis tools do not need the game or desktop runtime dependencies.
    run('cmake', '-S', upstream, '-B', build, *generator,
        '-DPS2X_BUILD_RUNTIME=OFF', '-DPS2X_BUILD_STUDIO=OFF', '-DPS2X_BUILD_TEST=OFF',
        '-DPS2X_BUILD_RECOMP=ON', '-DPS2X_BUILD_ANALYZER=ON',
        '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=')
    run('cmake', '--build', build, '--config', configuration, '--target',
        'ps2_analyzer', 'ps2_recomp', '--parallel', args.jobs)
    if args.tools_only:
        run('cmake', '-S', upstream, '-B', build, *generator,
            '-DPS2X_BUILD_RUNTIME=ON', '-DPS2X_ENABLE_DEBUG_UI=OFF',
            '-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF', '-DPS2X_IOP_BUILD_TESTS=ON',
            '-DOPENKAI2_RUNTIME_TESTS_ONLY=ON',
            f'-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES={(ROOT / "tools/native/attach-game.cmake").as_posix()}')
        run('cmake', '--build', build, '--config', configuration, '--target',
            'ps2EntryRunner', 'openkai2_iop_wait_tests', 'openkai2_iop_pipeline_tests',
            'openkai2_sound_tests', 'openkai2_cdvd_tests', 'openkai2_dma_tests', 'openkai2_startup_tests',
            'openkai2_thread_tests', 'openkai2_callback_tests', 'openkai2_graphics_tests', 'openkai2_guest_timing_tests', 'openkai2_input_tests', 'openkai2_presenter_tests',
            'ps2_iop_emulator_tests', 'ps2_iop_import_tests',
            'ps2_iop_compatibility_tests', 'ps2_iop_import_version_tests', '--parallel', args.jobs)
        run('ctest', '--test-dir', build, '-C', configuration, '--output-on-failure', '--no-tests=error')
        return 0

    analyzer = build / 'ps2xAnalyzer' / configuration / 'ps2_analyzer.exe'
    recompiler = build / 'ps2xRecomp' / configuration / 'ps2_recomp.exe'
    analyzer_config = native / 'analyzer.toml'
    run(analyzer, executable, analyzer_config, log=native / 'analyzer.log')
    text = analyzer_config.read_text()
    text = text.replace('patch_cop0 = true', 'patch_cop0 = false')
    text = text.replace('patch_cache = true', 'patch_cache = false')
    text = text.replace('single_file_output = false',
                        'single_file_output = false\nlow_memory_mode = true\noutput_worker_threads = 2')
    start = text.index('[patches]')
    end = text.index('[performance]', start)
    text = text[:start] + '[patches]\ninstructions = []\n\n' + text[end:]
    config = native / 'native.toml'
    config.write_text(text)
    run(sys.executable, ROOT / 'tools/native/translation-entries.py',
        '--config', config, '--elf', executable)
    run(recompiler, config, log=native / 'recompile.log')
    run(sys.executable, ROOT / 'tools/native/translation-entries.py',
        '--verify-generated', native / 'output')
    if not (native / 'output/register_functions.cpp').is_file():
        raise RuntimeError('The recompiler did not produce the function registry.')
    recompile_log = (native / 'recompile.log').read_text(encoding='utf-8', errors='replace')
    unresolved = re.search(r'Unhandled instructions:\s*(\d+)', recompile_log)
    if unresolved and int(unresolved.group(1)):
        print('Translation contains', unresolved.group(1),
              'unhandled instructions; see recompile.log. Full-game correctness is unverified.')

    run('cmake', '-S', upstream, '-B', build, *generator,
        '-DPS2X_BUILD_RUNTIME=ON', '-DPS2X_BUILD_STUDIO=OFF', '-DPS2X_BUILD_TEST=OFF',
        '-DPS2X_ENABLE_DEBUG_UI=OFF', '-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF',
        '-DPS2X_ENABLE_FFMPEG=ON', '-DPS2X_IOP_BUILD_TESTS=ON',
        '-DOPENKAI2_RUNTIME_TESTS_ONLY=OFF',
        '-DPS2X_RUNNER_UNITY_BUILD_BATCH_SIZE=8',
        f'-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES={(ROOT / "tools/native/attach-game.cmake").as_posix()}')
    run('cmake', '--build', build, '--config', configuration, '--target', 'ps2EntryRunner',
        'ps2_iop_emulator_tests', 'ps2_iop_import_tests', 'ps2_iop_compatibility_tests',
        'ps2_iop_import_version_tests', 'openkai2_iop_wait_tests', 'openkai2_iop_pipeline_tests', 'openkai2_sound_tests',
        'openkai2_cdvd_tests', 'openkai2_dma_tests', 'openkai2_startup_tests', 'openkai2_thread_tests', 'openkai2_callback_tests', 'openkai2_graphics_tests', 'openkai2_guest_timing_tests', 'openkai2_input_tests', 'openkai2_presenter_tests',
        '--parallel', args.jobs)
    run('ctest', '--test-dir', build, '-C', configuration, '--output-on-failure', '--no-tests=error')
    output = build / 'ps2xRuntime' / configuration
    shutil.copy2(output / 'ps2EntryRunner.exe', native / 'OpenKai2.exe')
    for dll in output.glob('*.dll'):
        shutil.copy2(dll, native / dll.name)
    shutil.copy2(ROOT / 'LICENSE', native / 'LICENSE')
    shutil.copy2(ROOT / 'NOTICE', native / 'NOTICE')
    package = [sys.executable, ROOT / 'tools/native/package-windows.py',
               '--binary', native / 'OpenKai2.exe', '--output', work / 'OpenKai2-windows-x64.zip']
    for directory in vc_runtime_directories():
        package.extend(['--runtime-dir', directory])
    run(*package)
    print('Built:', native / 'OpenKai2.exe')
    print('Launch:', subprocess.list2cmdline([str(native / 'OpenKai2.exe'), str(args.iso.resolve())]))
    print('Build completion does not establish title-screen boot or gameplay compatibility.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
