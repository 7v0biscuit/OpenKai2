"""Phase223 standalone original-handler virtual IRQ admission and packaging guards."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from _engine.phase223_original_irq_chain import cases,expected
from _engine.phase222_continuation import synthetic_post_words,validate_post_words
from _engine.phase217_semantics import synthetic_instructions

class Phase223(unittest.TestCase):
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
  cpp=(ROOT/'src/phase223_virtual_irq_original_handler.cpp').read_text()
  for item in ('translated_entry_prefix(cpu)', 'translated_successor(cpu)',
     'translated_return_or_threshold(cpu)', 'translated_external_call_boundary(cpu)',
     'translated_post_call(cpu)', 'virtual_helper(cpu.next_pc,cpu',
     'if(dispatch)', '++helper_calls;'):
   self.assertIn(item,cpp)
  for danger in ('registerFunction(', 'WriteProcessMemory(', 'setIrqHandlerEnabled('):
   self.assertNotIn(danger,cpp)
 def test_included_native_definitions_guard(self):
  cpp=(ROOT/'src/phase222_handler_double_helper_continuation.cpp').read_text()
  self.assertIn('#ifndef B2_PHASE223_LIBRARY_ONLY',cpp)
  self.assertIn('#endif // B2_PHASE223_LIBRARY_ONLY',cpp)
 def test_standalone_runner_and_cmake(self):
  self.assertIn('RUN_PHASE223.py',(ROOT/'DOUBLE_CLICK_PHASE223.cmd').read_text())
  run=(ROOT/'RUN_PHASE223.py').read_text()
  self.assertIn('compare_irq_original_chain',run)
  self.assertIn('PHASE223_ORIGINAL_IRQ11_CHAIN_RETAIL.json',run)
  self.assertIn("'phase':223",run)
  self.assertNotIn('RUN_PHASE222.py',run)
  self.assertIn('budokai2_phase223_virtual_irq_original_handler',
    (ROOT/'standalone/CMakeLists.txt').read_text())
 def test_no_old_release_required(self):
  self.assertTrue((ROOT/'src/handler_list_probe.cpp').exists())
  self.assertTrue((ROOT/'src/phase222_handler_double_helper_continuation.cpp').exists())
  self.assertTrue((ROOT/'_engine/phase222_continuation.py').exists())
  self.assertTrue((ROOT/'_engine/phase223_original_irq_chain.py').exists())
 def test_manifest_and_prohibited_game_payloads(self):
  manifest=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text())
  for filename in ('RUN_PHASE223.py','DOUBLE_CLICK_PHASE223.cmd',
    'src/phase223_virtual_irq_original_handler.cpp','_engine/phase223_original_irq_chain.py',
    'tests/test_phase223.py'):
   self.assertIn(filename,manifest)
  for name,digest in manifest.items():
   self.assertTrue((ROOT/name).is_file(),name)
   self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),digest,name)
   self.assertFalse(name.startswith('UPLOAD_'),name)
   self.assertFalse(name.lower().endswith(('.iso','.elf','.exe','.obj','.rar','.bin','.o','.pdb')),name)
if __name__=='__main__':unittest.main()
