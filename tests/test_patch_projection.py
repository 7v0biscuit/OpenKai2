# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('projection', Path(__file__).parents[1] / 'tools/native/patch-projection.py')
PROJECTION = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PROJECTION)


class ProjectionPatch(unittest.TestCase):
    def test_camera_load_and_idempotent_generation(self):
        original = '#include "ps2_runtime.h"\n' + PROJECTION.ORIGINAL + '    // next instruction\n'
        patched = PROJECTION.patch_projection(original)
        self.assertEqual(PROJECTION.patch_projection(patched), patched)
        self.assertIn(PROJECTION.ORIGINAL + PROJECTION.CORRECTION, patched)
        self.assertEqual(patched.count('FPU_MUL_S'), 1)
        self.assertIn('aspect == 1u', patched)
        self.assertTrue(patched.endswith('    // next instruction\n'))

    def test_unknown_changed_duplicate_and_partial_inputs_fail(self):
        for text in ('', PROJECTION.ORIGINAL.replace('16));', '20));'),
                     PROJECTION.ORIGINAL * 2, PROJECTION.ORIGINAL + PROJECTION.MARKER,
                     PROJECTION.ORIGINAL + PROJECTION.CORRECTION.replace('0.75f', '0.5f')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                PROJECTION.patch_projection(text)
