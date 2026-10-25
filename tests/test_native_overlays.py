# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('native_overlays', ROOT / 'tools/native/prepare-overlays.py')
OVERLAYS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OVERLAYS)


class RuntimeOverlays(unittest.TestCase):
    def test_git_patch_applies_inside_and_outside_a_parent_checkout(self):
        for inside_repository in (False, True):
            with self.subTest(inside_repository=inside_repository), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                if inside_repository:
                    subprocess.run(['git', 'init', '--quiet', str(root)], check=True)
                directory = root / '.local/build/overlay'
                directory.mkdir(parents=True)
                source = directory / 'Thread.cpp'
                source.write_text('priority = 0;\n')
                patch = root / 'bootstrap.patch'
                patch.write_text('diff --git a/Thread.cpp b/Thread.cpp\n'
                                 '--- a/Thread.cpp\n+++ b/Thread.cpp\n'
                                 '@@ -1 +1 @@\n-priority = 0;\n+priority = 1;\n')
                OVERLAYS.apply_overlay_patch(directory, patch)
                self.assertEqual(source.read_text(), 'priority = 1;\n')

    def test_successful_git_exit_cannot_hide_a_skipped_patch(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            (directory / 'Thread.cpp').write_text('priority = 0;\n')
            with mock.patch.object(OVERLAYS.subprocess, 'run'):
                with self.assertRaisesRegex(RuntimeError, 'changed no files'):
                    OVERLAYS.apply_overlay_patch(directory, directory / 'skipped.patch')


if __name__ == '__main__':
    unittest.main()
