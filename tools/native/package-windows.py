#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a portable Windows development bundle and check its DLL dependencies."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SYSTEM_DLLS = set('''advapi32 avicap32 bcrypt cfgmgr32 comctl32 comdlg32 crypt32
    d3d11 d3d12 d3dcompiler_47 dbghelp dnsapi dsound dwmapi dxgi gdi32 glu32
    imm32 iphlpapi kernel32 kernelbase mf mfplat mfreadwrite mfuuid mpr msacm32
    msimg32 msvcrt ncrypt ntdll ole32 oleaut32 opengl32 powrprof propsys psapi rpcrt4
    secur32 setupapi shell32 shlwapi user32 userenv usp10 uxtheme version winhttp
    wininet winmm winscard winspool ws2_32 wtsapi32'''.split())


def binary_source_revision(path):
    """Read the contiguous runtime log marker retained in the PE payload."""
    revisions = set(re.findall(rb'\[build\] source_revision=([0-9a-f]{40})(?= renderer=)',
                               Path(path).read_bytes()))
    if len(revisions) != 1:
        raise ValueError('Native build revision marker is missing or ambiguous; rebuild the runtime')
    return revisions.pop().decode('ascii')


def pe_imports(path):
    """Read normal and delay-load DLL imports from a Windows x64 PE image."""
    data = Path(path).read_bytes()

    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError(f'Truncated PE image: {path}')
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b'MZ':
        raise ValueError(f'Expected a Windows executable: {path}')
    header = unpack('<I', 0x3c)[0]
    if unpack('<4s', header)[0] != b'PE\0\0':
        raise ValueError(f'Invalid PE signature: {path}')
    machine, section_count = unpack('<HH', header + 4)
    optional_size = unpack('<H', header + 20)[0]
    optional = header + 24
    if machine != 0x8664 or unpack('<H', optional)[0] != 0x20b or optional_size < 112:
        raise ValueError(f'Expected an x64 PE32+ image: {path}')
    image_base = unpack('<Q', optional + 24)[0]
    headers_size = unpack('<I', optional + 60)[0]
    directory_count = unpack('<I', optional + 108)[0]
    if directory_count > (optional_size - 112) // 8:
        raise ValueError(f'Invalid PE data directories: {path}')
    sections = []
    for index in range(section_count):
        start = optional + optional_size + index * 40
        virtual_size, rva, size, offset = unpack('<IIII', start + 8)
        sections.append((rva, max(virtual_size, size), offset, size))

    def mapped(rva):
        if rva < min(headers_size, len(data)):
            return rva
        for base, virtual_size, offset, size in sections:
            if base <= rva < base + virtual_size and rva - base < size:
                result = offset + rva - base
                if result < len(data):
                    return result
        raise ValueError(f'Unmapped PE address in {path}')

    imports = set()
    for directory, width, name_field in ((1, 20, 12), (13, 32, 4)):
        if directory >= directory_count:
            continue
        rva, size = unpack('<II', optional + 112 + directory * 8)
        if not rva:
            continue
        terminated = False
        for index in range(min(size // width, 4096)):
            descriptor = mapped(rva + index * width)
            values = unpack('<' + 'I' * (width // 4), descriptor)
            if not any(values):
                terminated = True
                break
            name = values[name_field // 4]
            if directory == 13 and not (values[0] & 1):
                name -= image_base
            offset = mapped(name)
            end = data.find(b'\0', offset, offset + 256)
            if end < 0:
                raise ValueError(f'Invalid DLL name in {path}')
            dll = data[offset:end].decode('ascii').lower()
            if not dll.endswith('.dll') or any(ch in dll for ch in '/\\:'):
                raise ValueError(f'Unsafe DLL name in {path}')
            imports.add(dll)
        if not terminated:
            raise ValueError(f'Unterminated PE imports in {path}')
    return sorted(imports)


def system_dll(name):
    return (name.startswith(('api-ms-win-', 'ext-ms-win-'))
            or name.removesuffix('.dll') in SYSTEM_DLLS)


def dependency_files(binary, directories):
    available = {}
    for directory in directories:
        for path in Path(directory).iterdir():
            if path.is_file() and path.suffix.lower() == '.dll':
                available.setdefault(path.name.lower(), path)
    found = {Path(binary).name.lower(): Path(binary)}
    pending = [Path(binary)]
    while pending:
        for name in pe_imports(pending.pop()):
            if system_dll(name) or name in found:
                continue
            if name not in available:
                raise ValueError(f'Missing runtime DLL: {name}; add its folder with --runtime-dir')
            found[name] = available[name]
            pending.append(available[name])
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--runtime-dir', action='append', default=[], type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--strip-tool', help='Optional compiler strip tool; the original binary is preserved')
    args = parser.parse_args()
    source_revision = binary_source_revision(args.binary)
    revision = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                              capture_output=True, text=True, check=False)
    if revision.returncode == 0 and source_revision != revision.stdout.strip():
        raise ValueError('Native build revision does not match the source checkout; rebuild the runtime')
    files = dependency_files(args.binary, [args.binary.parent, *args.runtime_dir])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='openkai2-bundle-') as temporary:
        root = Path(temporary) / 'OpenKai2'
        root.mkdir()
        for path in files.values():
            shutil.copy2(path, root / ('OpenKai2.exe' if path == args.binary else path.name))
        if args.strip_tool:
            subprocess.run([args.strip_tool, '--strip-debug', str(root / 'OpenKai2.exe')], check=True)
        dependency_files(root / 'OpenKai2.exe', [root])
        if binary_source_revision(root / 'OpenKai2.exe') != source_revision:
            raise ValueError('Stripped native build revision changed')
        for name in ('LICENSE', 'NOTICE'):
            shutil.copy2(ROOT / name, root / name)
        notices = ROOT / 'third_party/licenses'
        if notices.is_dir():
            shutil.copytree(notices, root / 'licenses')
        (root / 'README.txt').write_text(
            'OpenKai2 Windows x64 development build\n\n'
            'Extract the entire ZIP, then double-click OpenKai2.exe and select your\n'
            'original Budokai 2 USA ISO. Keep the DLL files beside the executable.\n'
            'No Python or compiler is needed to launch this bundle.\n\n'
            'Command line: OpenKai2.exe "D:\\Games\\Budokai 2 USA.iso"\n'
            'The ISO must be present at every launch. Its files are verified before\n'
            'execution. Allow disk space for an extracted copy in\n'
            '%LOCALAPPDATA%\\OpenKai2\\discs.\n\n'
            'Startup log: %LOCALAPPDATA%\\OpenKai2\\logs\\startup.log\n\n'
            'This is a development build. Title-screen boot and gameplay have not\n'
            'been verified. A successful build or disc check is not game-boot proof.\n'
            'Source and build instructions: https://github.com/7v0biscuit/OpenKai2\n',
            encoding='utf-8')
        payload = {}
        for path in sorted(root.rglob('*')):
            if path.is_file():
                payload[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        version = re.search(r'application_version\[\] = "([^"]+)"',
                            (ROOT / 'src/launcher/version.h').read_text()).group(1)
        manifest = {'architecture': 'windows-x64', 'version': version,
                    'source_revision': source_revision,
                    'game_boot_verified': False,
                    'gameplay_verified': False, 'files_sha256': payload,
                    'imports': {path.name: pe_imports(path) for path in root.iterdir()
                                if path.suffix.lower() in ('.exe', '.dll')}}
        (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(root.rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(root.parent))
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix(args.output.suffix + '.sha256').write_text(
        f'{digest}  {args.output.name}\n', encoding='ascii')
    print(f'Bundle: {args.output} ({args.output.stat().st_size:,} bytes)')
    print(f'Checked executable and {len(files) - 1} runtime DLLs; SHA-256: {digest}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
