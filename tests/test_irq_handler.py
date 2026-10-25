"""standalone original-handler virtual IRQ admission and packaging guards."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.irq_handler import cases,expected
from oracles.helper_continuation import synthetic_post_words,validate_post_words
from oracles.wrapper_chain import synthetic_instructions

class IrqHandler(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.sample=next(iter(cases()))
 def test_all_flags_cover_multiple_original_cases(self):
  cs=list(cases())
  self.assertEqual(len(cs),2880)
  self.assertEqual(set(c[-1] for c in cs),set(range(16)))
 def test_unadmitted_never_enters_handler(self):
  c,f,s,b,_=self.sample
  for flag in range(16):
   out=expected(c,f,s,b,flag)
   self.assertEqual(len(out),29)
   if flag not in (7,15):
    self.assertEqual(out[1],0)
    self.assertEqual(out[3],0)
    self.assertEqual(out[4:28],(0,)*24)
    self.assertEqual(out[28],0)
 def test_explicit_ack_is_separate(self):
  c,f,s,b,_=self.sample
  self.assertEqual(expected(c,f,s,b,7)[2],1)
  self.assertEqual(expected(c,f,s,b,15)[2],0)
  self.assertEqual(expected(c,f,s,b,9)[2],1)  # ack without route/handler
 def test_both_original_calls_are_observed(self):
  calls=set();poll=False;returns=False
  for c,f,s,b,flags in cases():
   if flags!=7:continue
   out=expected(c,f,s,b,flags)
   calls.add(out[5]);poll|=out[7]==2;returns|=out[21]==1
  self.assertEqual(calls,{0x113400,0x113410})
  self.assertTrue(poll)
  self.assertTrue(returns)
 def test_both_post_windows_sha_verified(self):
  self.assertEqual(len(validate_post_words(synthetic_post_words())),2)
 def test_source_invokes_real_bounded_original_blocks(self):
  cpp=(ROOT/'src/probes/irq_handler.cpp').read_text()
  for item in ('translated_entry_prefix(cpu)', 'translated_successor(cpu)',
     'translated_return_or_threshold(cpu)', 'translated_external_call_boundary(cpu)',
     'translated_post_call(cpu)', 'virtual_helper(cpu.next_pc,cpu',
     'if(dispatch)', '++helper_calls;'):
   self.assertIn(item,cpp)
  for danger in ('registerFunction(', 'WriteProcessMemory(', 'setIrqHandlerEnabled('):
   self.assertNotIn(danger,cpp)
 def test_included_native_definitions_guard(self):
  cpp=(ROOT/'src/probes/helper_continuation.cpp').read_text()
  self.assertIn('#ifndef B2_IRQ_HANDLER_LIBRARY_ONLY',cpp)
  self.assertIn('#endif // B2_IRQ_HANDLER_LIBRARY_ONLY',cpp)
 def test_standalone_runner_and_cmake(self):
  self.assertIn('tools/probes/run.py',(ROOT/'tools/probes/run.cmd').read_text())
  run=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('compare_irq_original_chain',run)
  self.assertIn('ORIGINAL_IRQ11_CHAIN_RETAIL.json',run)
  self.assertIn('openkai2_irq_handler',
    (ROOT/'CMakeLists.txt').read_text())
 def test_no_old_release_required(self):
  self.assertTrue((ROOT/'src/probes/handler_list_probe.cpp').exists())
  self.assertTrue((ROOT/'src/probes/helper_continuation.cpp').exists())
  self.assertTrue((ROOT/'tools/probes/oracles/helper_continuation.py').exists())
  self.assertTrue((ROOT/'tools/probes/oracles/irq_handler.py').exists())
if __name__=='__main__':unittest.main()
