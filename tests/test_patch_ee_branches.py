# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest
import unittest.mock

spec = importlib.util.spec_from_file_location('ee_branches', Path(__file__).parents[1] / 'tools/native/patch-ee-branches.py')
PATCH = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PATCH)

def site(opcode, rs, rt, mnemonic, operator):
    word = (opcode << 26) | (rs << 21) | (rt << 16) | 1
    return (f'// 0x1000: 0x{word:x} {mnemonic} $r{rs}, target\n'
            f'const bool branch_taken_0x1000 = (GPR_S32(ctx, {rs}) {operator} 0);\n')

class SignedBranchPatch(unittest.TestCase):
    def test_all_sign_branches_and_likely_link_forms(self):
        cases = [(6,0,'blez','<='),(7,0,'bgtz','>'),(22,0,'blezl','<='),(23,0,'bgtzl','>')]
        cases += [(1,rt,name,op) for rt,name,op in [(0,'bltz','<'),(1,'bgez','>='),
            (2,'bltzl','<'),(3,'bgezl','>='),(16,'bltzal','<'),(17,'bgezal','>='),
            (18,'bltzall','<'),(19,'bgezall','>=')]]
        for opcode,rt,name,operator in cases:
            with self.subTest(name=name):
                source=site(opcode,19,rt,name,operator)
                fixed,count,total=PATCH.corrected(source)
                self.assertEqual((count,total),(1,1))
                self.assertIn('GPR_S64(ctx, 19)',fixed)
                self.assertEqual(PATCH.corrected(fixed),(fixed,0,1))

    def test_opcode_and_register_must_match_the_predicate(self):
        with self.assertRaises(ValueError):
            PATCH.corrected(site(1,3,2,'bgezl','>='))
        with self.assertRaises(ValueError):
            PATCH.corrected(site(1,3,2,'bltzl','<').replace('GPR_S32(ctx, 3)','GPR_S32(ctx, 4)'))

    def test_unrelated_32_bit_arithmetic_is_preserved(self):
        source=site(1,3,2,'bltzl','<')+'SET_GPR_S32(ctx, 2, GPR_S32(ctx, 3) >> 4);\n'
        self.assertTrue(PATCH.corrected(source)[0].endswith('SET_GPR_S32(ctx, 2, GPR_S32(ctx, 3) >> 4);\n'))

    def test_preflight_keeps_every_file_if_any_site_is_unrecognized(self):
        with tempfile.TemporaryDirectory() as folder:
            good=Path(folder)/'a.cpp';bad=Path(folder)/'z.cpp'
            original=site(1,3,2,'bltzl','<');good.write_text(original)
            bad.write_text(original.replace('GPR_S32(ctx, 3)','unknown_register()'))
            with self.assertRaises(ValueError), unittest.mock.patch('sys.argv',['patch','--directory',folder]):
                PATCH.main()
            self.assertEqual(good.read_text(),original)

if __name__=='__main__':unittest.main()
