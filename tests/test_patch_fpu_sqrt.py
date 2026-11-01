# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('fpu_sqrt', Path(__file__).parents[1] / 'tools/native/patch-fpu-sqrt.py')
fpu_sqrt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fpu_sqrt)

SOURCE = ('    // 0x1541dc: 0x46160504  c1          0x160504\n'
          '    ctx->pc = 0x1541dcu;\n'
          '    ctx->f[20] = FPU_SQRT_S(ctx->f[0]);\n'
          '    // 0x154298: 0x460c0004  c1          0xC0004\n'
          '    ctx->pc = 0x154298u;\n'
          '    ctx->f[0] = FPU_SQRT_S(ctx->f[0]);\n')


class PatchFpuSqrt(unittest.TestCase):
    def test_reads_ft(self):
        text, changed = fpu_sqrt.patch(SOURCE)
        self.assertEqual(changed, 2)
        self.assertIn('ctx->f[20] = FPU_SQRT_S(ctx->f[22]);', text)
        self.assertIn('ctx->f[0] = FPU_SQRT_S(ctx->f[12]);', text)

    def test_second_run_changes_nothing(self):
        text, _ = fpu_sqrt.patch(SOURCE)
        self.assertEqual(fpu_sqrt.patch(text), (text, 0))

    def test_sqrt_of_its_own_register_stays(self):
        source = '    // 0x100000: 0x46000004  sqrt.s $f0,$f0\n    ctx->f[0] = FPU_SQRT_S(ctx->f[0]);\n'
        self.assertEqual(fpu_sqrt.patch(source), (source, 0))

    def test_wrong_destination_is_refused(self):
        with self.assertRaises(ValueError):
            fpu_sqrt.patch(SOURCE.replace('ctx->f[20] =', 'ctx->f[21] ='))

    def test_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'sub.cpp'
            path.write_text(SOURCE)
            fpu_sqrt.main(['--directory', folder])
            self.assertIn('FPU_SQRT_S(ctx->f[22])', path.read_text())


if __name__ == '__main__':
    unittest.main()
