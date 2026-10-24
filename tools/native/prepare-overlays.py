#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply reviewed runtime changes to build copies of the pinned dependency."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

EXPECTED = {
    'ps2xRuntime/src/lib/gs/gs_frontend.cpp':
        '4c6e6391972ecaafd18bf3e3e047ef980b4cd0907c8199a52cb9d98d8b7397b8',
    'ps2xIOP/src/emulator/core/iop_cpu.cpp':
        'c9742864bee0bb3c29f1d18b5a25b73fa376bd3c066a8ca850c65db2bfa83dae',
    'ps2xIOP/src/emulator/core/iop_cpu.h':
        'eaf4ea244cb1650eaf0bb82f6c5c3f2895ee88e6d09a38d91f74f9dceb93291c',
    'ps2xIOP/src/emulator/imports/iop_imports.cpp':
        'e724e0bbfa64fa639aa6d72519edcf3c2ac94d0d1430d13160378869ad73924b',
    'ps2xIOP/src/emulator/imports/iop_imports.h':
        '056894c369e6e8e8f8efcaae910c95414e7c7935658723fc7ff6af9b3fb6fd16',
    'ps2xIOP/src/emulator/core/iop_memory.h':
        '5a5b73ab69c4b58531111cad2743847ef00f3a8dd73004eea01ec8de4053135c',
    'ps2xIOP/include/ps2x/iop/iop_host.h':
        '9bfbcd65c5c61847d3427dcdaebaf383617c2bf3a5595155be8d9107c0bc6acc',
    'ps2xRuntime/src/lib/ps2_iop_host.h':
        '5116812cb61fd5be00c002f98d0b19f4f138ba10248e9e24cb9d91425b197704',
    'ps2xRuntime/src/lib/ps2_iop_host.cpp':
        '17a35b99e8535f38b47097361fb7bfedb30966c9d85b04e83f4e97a86d8ae0f6',

    'ps2xRuntime/src/lib/Kernel/EeScheduler.cpp':
        '89383df078a13e9d13335f158ce88f0fb85935c57f2edab0ef8f4a0c9edb4b83',
    'ps2xRuntime/include/runtime/gs/gs_texture_page_cache.h':
        '51bef021981a9834cc7bcadc9a4527d3588b058c481212353499e7936d12a3e6',
    'ps2xRuntime/src/lib/ps2_pad.cpp':
        'ad7767253d4e56abbfab3bb3869b38c6d10293914a0b1f6652685cc49383fd49',
    'ps2xRuntime/src/lib/Kernel/Stubs/MPEG.cpp':
        'add74172d55a5c00ec4a5bdd1a45e0768fb8b72588f2fce853b6081ca5c3bd9f',
    'ps2xRuntime/src/lib/Kernel/Stubs/MPEG.h':
        'd414d295f3bf9756ae5d91a7554fc1a36ac2a00ebab9b95d2dc95da45b135b25',
    'ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp':
        '3f0fea655726215f56d8c3c9cff8345a7d36a929fe3f845cc96346a247085fa9',
    'ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp':
        'e73d7f0a46d07fc0f70d5b49b6453ed64271b3ad19ffd62a6a872095491bcdf4',
    'ps2xRuntime/src/lib/ps2_runtime.cpp':
        '77cd184f2e70eacfd5ea584ec95829553e3deeead0a19c04e4b0970dd7c6c9ee',
    'ps2xRuntime/src/lib/ps2_memory.cpp':
        '4bae084dfa6ea30479af5efde70c61927a4fe5a49216d99c3db76297db4ac22a',
    'ps2xRuntime/src/lib/Kernel/Stubs/DMA.cpp':
        '8238ec20e2d2b4c62a495b205d3201d8cad454e513f91b21fe517e62b0a89107',
    'ps2xIOP/src/emulator/core/iop_kernel.h':
        '260863c946a687faf5e88c309615de4da319a3028124a97b714468dbf6df0ab5',
    'ps2xIOP/src/emulator/core/iop_kernel.cpp':
        '203c9949793be35f30583612db855544356d1ec1124b96fdf9377e4d321c1002',
    'ps2xIOP/src/emulator/core/iop_memory.cpp':
        '3952624ce32b501511a7460a7ce6d387f314d7173e8a6f4a2ed7fc1507e5f4ee',
    'ps2xIOP/src/emulator/iop_emulator.cpp':
        '01deb410d04f441be12adc68638846061f19759e007c36cdfcbe987796199d36',
    'ps2xIOP/src/emulator/imports/iop_cdvd.cpp':
        'ca8f524a3b708bca4183c940ecdc244468782c0162e832f13e5ae8f088d26f07',
    'ps2xRuntime/src/lib/Kernel/Stubs/Helpers/Support.h':
        'b1640bdc33adb42e7873ff501ed5a07657a2b21fede3c5448d86f15ce34699db',
}


def apply_overlay_patch(directory, patch):
    directory = Path(directory).resolve()
    environment = os.environ.copy()
    # A build directory may be inside the source checkout. Git otherwise
    # discovers that parent repository and can silently skip diff --git paths
    # outside its current-directory prefix, even with --no-index.
    for name in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE', 'GIT_COMMON_DIR', 'GIT_PREFIX'):
        environment.pop(name, None)
    environment['GIT_CEILING_DIRECTORIES'] = str(directory.parent)

    def contents():
        return {path.relative_to(directory): hashlib.sha256(path.read_bytes()).digest()
                for path in directory.rglob('*') if path.is_file()}

    before = contents()
    command = ['git', 'apply', '--no-index']
    subprocess.run([*command, '--check', str(patch)], cwd=directory, env=environment, check=True)
    subprocess.run([*command, str(patch)], cwd=directory, env=environment, check=True)
    if contents() == before:
        raise RuntimeError(f'Runtime overlay changed no files: {patch}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    for name, expected in EXPECTED.items():
        # Git may check out Windows source with CRLF; verify canonical text.
        data = (args.upstream / name).read_bytes().replace(b'\r\n', b'\n')
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit(f'Unexpected upstream source; preserved without modification: {name}')
    args.output.mkdir(parents=True, exist_ok=True)
    patches = Path(__file__).resolve().parent / 'patches'
    with tempfile.TemporaryDirectory(prefix='openkai2-overlays-', dir=args.output.parent) as folder:
        temporary = Path(folder)
        (temporary / 'runtime').mkdir()
        (temporary / 'ps2x/iop').mkdir(parents=True)
        shutil.copy2(args.upstream / 'ps2xIOP/include/ps2x/iop/iop_host.h',temporary / 'ps2x/iop/iop_host.h')
        for name in ('ps2_iop_host.cpp','ps2_iop_host.h'):
            shutil.copy2(args.upstream / 'ps2xRuntime/src/lib' / name,temporary / 'runtime' / name)
        (temporary / 'include/runtime/gs').mkdir(parents=True)
        shutil.copy2(args.upstream / 'ps2xRuntime/include/runtime/gs/gs_texture_page_cache.h', temporary / 'include/runtime/gs/gs_texture_page_cache.h')
        shutil.copy2(args.upstream / 'ps2xRuntime/src/lib/ps2_memory.cpp', temporary / 'runtime/ps2_memory.cpp')
        shutil.copy2(args.upstream / 'ps2xRuntime/src/lib/ps2_runtime.cpp', temporary / 'runtime/ps2_runtime.cpp')
        shutil.copy2(args.upstream / 'ps2xRuntime/src/lib/ps2_pad.cpp', temporary / 'runtime/ps2_pad.cpp')
        (temporary / 'runtime/gs').mkdir()
        shutil.copy2(args.upstream / 'ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp', temporary / 'runtime/gs/gs_cpu_backend.cpp')
        shutil.copy2(args.upstream / 'ps2xRuntime/src/lib/gs/gs_frontend.cpp', temporary / 'runtime/gs/gs_frontend.cpp')
        shutil.copytree(args.upstream / 'ps2xRuntime/src/lib/Kernel/Stubs', temporary / 'Kernel/Stubs')
        shutil.copytree(args.upstream / 'ps2xRuntime/src/lib/Kernel/Syscalls', temporary / 'Kernel/Syscalls')
        shutil.copy2(args.upstream / 'ps2xRuntime/src/lib/Kernel/EeScheduler.cpp', temporary / 'Kernel/EeScheduler.cpp')
        shutil.copytree(args.upstream / 'ps2xIOP/src/emulator', temporary / 'emulator')
        for source in temporary.rglob('*'):
            if source.is_file() and source.suffix in ('.h', '.cpp', '.hpp', '.inc'):
                source.write_bytes(source.read_bytes().replace(b'\r\n', b'\n'))
        for patch in (patches / 'disc-layout.patch', patches / 'iop-startup.patch',
                      patches / 'dma-receive.patch', patches / 'startup-diagnostics.patch',
                      patches / 'callback-stacks.patch', patches / 'thread-bootstrap.patch',
                      patches / 'thread-wakeup.patch', patches / 'thread-priority.patch',
                      patches / 'graphics-diagnostics.patch', patches / 'interlaced-display.patch',
                      patches / 'sprite-coordinates.patch', patches / 'field-presentation.patch',
                      patches / 'mpeg-ipu-input.patch', patches / 'mpeg-native-scheduling.patch',
                      patches / 'movie-performance-diagnostics.patch', patches / 'sprite-sampling.patch',
                      patches / 'sprite-throughput.patch', patches / 'host-input.patch',
                      patches / 'asynchronous-presentation.patch', patches / 'input-observation.patch', patches / 'texture-pages.patch',
                      patches / 'empty-dma-completion.patch', patches / 'parallel-sprites.patch', patches / 'guest-progress.patch',
                      patches / 'interrupt-critical-sections.patch', patches / 'sprite-formats.patch',
                      patches / 'pixel-blending.patch', patches / 'spu2-pcm-output.patch', patches / 'iop-import-cache.patch', patches / 'small-triangles.patch', patches / 'iop-execution.patch', patches / 'ct32-pixels.patch', patches / 'early-depth.patch', patches / 'prepared-clut.patch', patches / 'draw-clut-sampling.patch', patches / 'guest-timing.patch', patches / 'vu-context.patch', patches / 'draw-history-sampling.patch', patches / 'texture-cache-binding.patch', patches / 'tested-pixel-writer.patch', patches / 'host-audio-disabled.patch'):
            apply_overlay_patch(temporary, patch)
        for source in temporary.rglob('*'):
            if source.is_file():
                target = args.output / source.relative_to(temporary)
                data = source.read_bytes()
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists() or target.read_bytes() != data:
                    target.write_bytes(data)


if __name__ == '__main__':
    main()
