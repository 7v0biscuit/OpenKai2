# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from test_windows_package import pe_fixture

SPEC = importlib.util.spec_from_file_location(
    'release_packaging', Path(__file__).resolve().parents[1] / 'tools/native/build-release.py')
RELEASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RELEASE)


class ReleasePackaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def bundle(self, changed=None, extra=None, imports=(), revision="a"*40):
        files = {'OpenKai2.exe': bytes(pe_fixture(imports))+b'[build] source_revision='+b'a'*40+b' renderer=cpu', 'LICENSE': b'license',
                 'NOTICE': b'notice', 'README.txt': b'readme'}
        manifest = {'architecture': 'windows-x64', 'source_revision': revision,
                    'files_sha256': {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}}
        files['manifest.json'] = json.dumps(manifest).encode()
        if changed:
            files.update(changed)
        path = self.root / 'bundle.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            for name, data in files.items():
                archive.writestr('OpenKai2/' + name, data)
            for name, data in (extra or {}).items():
                archive.writestr(name, data)
        return path

    def test_verified_bundle(self):
        root, manifest = RELEASE.extract_verified_bundle(self.bundle(), self.root / 'unpacked')
        self.assertTrue((root / 'OpenKai2.exe').is_file())
        self.assertEqual(manifest['architecture'], 'windows-x64')

    def test_stale_native_revision_rejected(self):
        with self.assertRaisesRegex(ValueError,'native build revision does not match'):
            RELEASE.extract_verified_bundle(self.bundle(revision='b'*40),self.root/'unpacked')

    def test_tampered_payload_rejected(self):
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            RELEASE.extract_verified_bundle(self.bundle(changed={'NOTICE': b'changed'}), self.root / 'unpacked')

    def test_parent_traversal_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unsafe bundle path'):
            RELEASE.extract_verified_bundle(self.bundle(extra={'OpenKai2/../outside': b'bad'}), self.root / 'unpacked')
        self.assertFalse((self.root / 'unpacked/outside').exists())

    def test_unlisted_payload_rejected(self):
        with self.assertRaisesRegex(ValueError, 'every payload file'):
            RELEASE.extract_verified_bundle(self.bundle(extra={'OpenKai2/extra.exe': b'bad'}), self.root / 'unpacked')

    def test_missing_runtime_dll_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Missing runtime DLL'):
            RELEASE.extract_verified_bundle(self.bundle(imports=['missing.dll']), self.root / 'unpacked')

    def test_uninstall_deletes_only_listed_files(self):
        root, _ = RELEASE.extract_verified_bundle(self.bundle(), self.root / 'unpacked')
        commands = RELEASE.uninstall_commands(root)
        self.assertIn('Delete "$INSTDIR\\OpenKai2.exe"', commands)
        self.assertNotIn('/r', commands.lower())
        self.assertNotIn('discs', commands)
        self.assertNotIn('*', commands)


if __name__ == '__main__':
    unittest.main()
