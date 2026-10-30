import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'bootstrap_nsis', Path(__file__).resolve().parents[1] / 'tools/native/bootstrap-nsis.py')
NSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NSIS)


class NsisBootstrapTests(unittest.TestCase):
    def test_corrupt_second_archive_never_installs_partial_compiler(self):
        good = b'verified package'
        packages = [(name, len(good), hashlib.sha256(good).hexdigest())
                    for name in ('compiler.deb', 'support.deb')]
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / 'compiler'
            with patch.object(NSIS.platform, 'system', return_value='Linux'), \
                    patch.object(NSIS.platform, 'machine', return_value='x86_64'), \
                    patch.object(NSIS, 'PACKAGES', packages), \
                    patch.object(NSIS.urllib.request, 'urlopen',
                                 side_effect=[io.BytesIO(good), io.BytesIO(b'corrupt archive')]), \
                    patch.object(NSIS.subprocess, 'run') as extract:
                with self.assertRaisesRegex(ValueError, 'verification failed'):
                    NSIS.install(destination)
                self.assertEqual(extract.call_count, 1)
                self.assertFalse(destination.exists())

    def test_existing_destination_is_preserved_without_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            sentinel = destination / 'keep.txt'
            sentinel.write_text('keep this directory')
            with patch.object(NSIS.platform, 'system', return_value='Linux'), \
                    patch.object(NSIS.platform, 'machine', return_value='x86_64'), \
                    patch.object(NSIS.urllib.request, 'urlopen') as download:
                with self.assertRaisesRegex(ValueError, 'empty NSIS'):
                    NSIS.install(destination)
                download.assert_not_called()
            self.assertEqual(sentinel.read_text(), 'keep this directory')
