# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'windows_package', Path(__file__).resolve().parents[1] / 'tools/native/package-windows.py')
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def pe_fixture(imports=(), delay=()):
    data = bytearray(4096)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 1)
    struct.pack_into('<H', data, 0x94, 240)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<Q', data, optional + 24, 0x140000000)
    struct.pack_into('<I', data, optional + 60, 512)
    struct.pack_into('<I', data, optional + 108, 16)
    struct.pack_into('<IIII', data, optional + 240 + 8, 3584, 0x1000, 3584, 512)
    name_offset = 2048
    for directory, names, offset, width, name_field in (
            (1, imports, 512, 20, 12), (13, delay, 1024, 32, 4)):
        struct.pack_into('<II', data, optional + 112 + directory * 8,
                         offset - 512 + 0x1000, (len(names) + 1) * width)
        for index, name in enumerate(names):
            if directory == 13:
                struct.pack_into('<I', data, offset + index * width, 1)
            struct.pack_into('<I', data, offset + index * width + name_field,
                             name_offset - 512 + 0x1000)
            value = name.encode('ascii') + b'\0'
            data[name_offset:name_offset + len(value)] = value
            name_offset += len(value)
    return data


class WindowsPackage(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def binary(self, name='OpenKai2.exe', imports=(), delay=()):
        path = self.root / name
        path.write_bytes(pe_fixture(imports, delay))
        return path

    def test_native_revision_stamp(self):
        binary=self.binary();revision='a'*40
        binary.write_bytes(binary.read_bytes()+b'[build] source_revision='+revision.encode()+b' renderer=cpu')
        self.assertEqual(PACKAGE.binary_source_revision(binary),revision)

    def test_invalid_native_revision_stamp(self):
        for marker in [b'',b'[build] source_revision=unknown renderer=cpu',
                       b'[build] source_revision='+b'a'*40+b' renderer=cpu'+b'[build] source_revision='+b'b'*40+b' renderer=cpu']:
            binary=self.binary();binary.write_bytes(binary.read_bytes()+marker)
            with self.subTest(marker=marker),self.assertRaisesRegex(ValueError,'missing or ambiguous'):
                PACKAGE.binary_source_revision(binary)

    def test_normal_and_delay_imports(self):
        binary = self.binary(imports=['KERNEL32.dll'], delay=['Media.DLL'])
        self.assertEqual(PACKAGE.pe_imports(binary), ['kernel32.dll', 'media.dll'])

    def test_transitive_dependencies_and_case(self):
        binary = self.binary(imports=['KERNEL32.dll', 'media.dll'])
        self.binary('Media.DLL', imports=['codec.dll'])
        self.binary('codec.dll', delay=['USER32.dll'])
        self.assertEqual(set(PACKAGE.dependency_files(binary, [self.root])),
                         {'openkai2.exe', 'media.dll', 'codec.dll'})

    def test_missing_dependency_rejected(self):
        binary = self.binary(imports=['missing.dll'])
        with self.assertRaisesRegex(ValueError, 'Missing runtime DLL: missing.dll'):
            PACKAGE.dependency_files(binary, [self.root])

    def test_dependency_cycle(self):
        binary = self.binary(imports=['first.dll'])
        self.binary('first.dll', imports=['second.dll'])
        self.binary('second.dll', imports=['first.dll'])
        self.assertEqual(len(PACKAGE.dependency_files(binary, [self.root])), 3)

    def test_wrong_architecture_rejected(self):
        binary = self.binary()
        data = bytearray(binary.read_bytes())
        struct.pack_into('<H', data, 0x84, 0x14c)
        binary.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'x64'):
            PACKAGE.pe_imports(binary)

    def test_truncated_image_rejected(self):
        binary = self.binary()
        binary.write_bytes(binary.read_bytes()[:120])
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            PACKAGE.pe_imports(binary)

    def test_import_path_rejected(self):
        binary = self.binary(imports=['../bad.dll'])
        with self.assertRaisesRegex(ValueError, 'Unsafe DLL name'):
            PACKAGE.pe_imports(binary)

    def test_system_library_boundaries(self):
        self.assertTrue(PACKAGE.system_dll('kernel32.dll'))
        self.assertTrue(PACKAGE.system_dll('msimg32.dll'))
        self.assertTrue(PACKAGE.system_dll('api-ms-win-crt-runtime-l1-1-0.dll'))
        self.assertFalse(PACKAGE.system_dll('vcruntime140.dll'))
        self.assertFalse(PACKAGE.system_dll('avcodec-61.dll'))


if __name__ == '__main__':
    unittest.main()
