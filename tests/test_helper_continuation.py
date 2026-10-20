"""original retail post-helper continuation and clean-release gates."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.helper_continuation import (
 WINDOWS,synthetic_post_words,validate_post_words,interpret_post,cases,oracle,SECOND_SNAPSHOTS)
from oracles.wrapper_chain import synthetic_instructions
from oracles.entry_oracle import s32

class HelperContinuation(unittest.TestCase):
 def test_both_retail_windows_sha_pinned(self):
  self.assertEqual(len(synthetic_post_words()),19)
  self.assertEqual(len(validate_post_words(synthetic_post_words())),2)
  self.assertEqual(set(WINDOWS),{0x11388c,0x1138c0})
 def test_changed_instruction_rejected(self):
  for pc in (0x11389c,0x1138c8,0x1138a8,0x1138e4):
   words=synthetic_post_words();words[pc]^=1
   with self.assertRaises(ValueError):validate_post_words(words)
 def test_dsrlv_operand_order_and_sign_extension(self):
  # S0 is shifted by 4*saved S1 (not a0 shifted by S0).
  words=synthetic_post_words();regs=[0]*32
  regs[29]=0x200080;regs[16]=0x123456789abcdef0;regs[17]=3;regs[31]=0x1138c0
  saved={0x200080:0x111,0x200090:0x222,0x2000a0:0x333,0x2000b0:0x444}
  pc,out=interpret_post(words,0x1138c0,regs,saved)
  self.assertEqual(pc,0x113410)
  self.assertEqual(out[4],s32((regs[16]>>12)&0xffffffff))
  self.assertEqual(out[29],0x2000c0)
  self.assertEqual(out[31],0x444)
  self.assertEqual((out[16],out[17],out[18]),(0x111,0x222,0x333))
 def test_first_return_and_second_tail_cases(self):
  results=list(cases());self.assertEqual(len(results),180)
  counts={0x113400:0,0x113410:0};first_stops=0
  for case,first,second,b in results:
   got=oracle(case,first,second,b,synthetic_instructions(),synthetic_post_words())
   self.assertEqual(len(got),24)
   if got[3]==2:first_stops+=1;self.assertEqual(got[8],0)
   else:
    self.assertEqual(got[8],1)
    counts[got[9]]+=1
    self.assertEqual(got[10],case[2])
    self.assertEqual(got[11],s32(case[1]))
  self.assertEqual(counts,{0x113400:90,0x113410:60})
  self.assertEqual(first_stops,30)
 def test_windows_launcher(self):
  self.assertIn('tools/probes/run.py',(ROOT/'tools/probes/run.cmd').read_text())
  self.assertIn('openkai2_helper_continuation',(ROOT/'CMakeLists.txt').read_text())
  runner=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('compare_double_helper',runner)
  self.assertIn('DOUBLE_HELPER_RETAIL.json',runner)
 def test_not_marked_as_real_ps2_hardware(self):
  cpp=(ROOT/'src/probes/helper_continuation.cpp').read_text()
  self.assertIn('translated_post_call(cpu)',cpp)
  self.assertIn('second=virtual_helper(',cpp)
  self.assertIn('first.pc!=cpu.r[31]',cpp)
  self.assertNotIn('registerFunction(',cpp)
  self.assertNotIn('WriteProcessMemory(',cpp)
if __name__=='__main__':unittest.main()
