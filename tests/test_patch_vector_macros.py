# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest
import unittest.mock

spec = importlib.util.spec_from_file_location('vector_macros', Path(__file__).parents[1] / 'tools/native/patch-vector-macros.py')
MACROS = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MACROS)

LANE = '_mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{0}], ctx->vu0_vf[{0}], _MM_SHUFFLE(0,0,0,{1})))'
RSQRT = ('    // 0x141470: 0x4a6303be  vrsqrt      $Q, $vf0w, $vf3x\n'
         '    ctx->pc = 0x141470u;\n'
         '    { float ft = ' + LANE.format(3, 0) + '; ctx->vu0_q = (ft > 0.0f) ? (1.0f / sqrtf(ft)) : 0.0f; }\n')
DIV = ('    { float fs = ' + LANE.format(0, 3) + '; float ft = ' + LANE.format(1, 2) +
       '; ctx->vu0_q = (ft != 0.0f) ? (fs / ft) : 0.0f; }\n')
FTOI = ('    { __m128 src = ctx->vu0_vf[1]; src = _mm_mul_ps(src, _mm_set1_ps(1.0f)); '
        '__m128i res_i = _mm_cvttps_epi32(src); __m128 res = _mm_castsi128_ps(res_i); }\n')


class VectorMacroPatch(unittest.TestCase):
    def test_conversions_and_divides_use_vu_helpers(self):
        patched = MACROS.patch_text(RSQRT + DIV + FTOI)
        self.assertEqual(MACROS.patch_text(patched), patched)
        self.assertIn('float fs = ' + LANE.format(0, 3) + '; float ft = ' + LANE.format(3, 0) +
                      '; ctx->vu0_q = ps2_vrsqrt(fs, ft); }', patched)
        self.assertIn('ctx->vu0_q = ps2_vdiv(fs, ft);', patched)
        self.assertIn('__m128i res_i = ps2_vftoi(src);', patched)
        self.assertNotIn('sqrtf(ft)', patched)
        self.assertNotIn('_mm_cvttps_epi32', patched)
        self.assertTrue(patched.startswith(RSQRT.splitlines(True)[0]))

    def test_rsqrt_numerator_comes_from_the_instruction_word(self):
        word = 0x4a0003be | (1 << 21) | (2 << 16) | (5 << 11)
        text = RSQRT.replace('0x4a6303be', f'0x{word:08x}')
        self.assertIn('float fs = ' + LANE.format(5, 1) + ';', MACROS.patch_text(text))

    def test_unrecognized_conversion_stops_the_build(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sub_00100000_0x100000.cpp'
            path.write_text('    { float ft = ctx->vu0_i; ctx->vu0_q = (ft > 0.0f) ? (1.0f / sqrtf(ft)) : 0.0f; }\n')
            with self.assertRaises(ValueError), unittest.mock.patch('sys.argv', ['x', '--directory', directory]):
                MACROS.main()


if __name__ == '__main__':
    unittest.main()
