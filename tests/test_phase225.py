"""Phase225 bounded original-handler list-then-helper native integration."""
import hashlib,json,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from _engine.phase225_list_irq_chain import cases,expected
from _engine.phase224_list_walk import SYNTHETIC

class Phase225(unittest.TestCase):
 def test_case_matrix_and_control_flow(self):
  cc=list(cases())
  self.assertEqual(len(cc),360)
  results=[expected(x) for x in cc]
  self.assertTrue(all(len(x)==46 for x in results))
  self.assertEqual(sum(x[1]==0 for x in results),120)
  self.assertEqual(sum(x[3]==1 for x in results),240)
  self.assertEqual(sum(x[20]==2 for x in results),48)
  self.assertEqual(sum(x[22]==0x113400 for x in results),96)
  self.assertEqual(sum(x[22]==0x113410 for x in results),96)
  self.assertEqual(sum(x[24]==2 for x in results),64)
  self.assertEqual(sum(x[38]==2 for x in results),64)
  self.assertEqual(sum(x[38]==1 for x in results),64)
 def test_suppressed_irq_never_enters_list_or_helpers(self):
  for c in cases():
   if c[1]&7!=7:
    v=expected(c)
    self.assertEqual(v[3],0)
    self.assertEqual(v[4:45],(0,)*41)
    self.assertEqual(v[45],0)
 def test_list_budget_blocks_helper_execution(self):
  for c in cases():
   v=expected(c)
   if v[20]==2:
    self.assertEqual(v[21:45],(0,)*24)
    self.assertEqual(v[45],0)
 def test_opcode_oracle_detects_corruption(self):
  corrupt=dict(SYNTHETIC);corrupt[0x1137f4]=0xffffffff
  c=next(x for x in cases() if x[1]==7)
  with self.assertRaises(ValueError):expected(c,list_words=corrupt)
 def test_cpp_library_integration_and_bounded_memory(self):
  source=(ROOT/'src/phase225_original_list_to_irq_helper.cpp').read_text()
  for marker in ('translated_entry_prefix(c)','translated_successor(c)',
   'translate(c.r[16],c.r[18],c.r[7],c.r[6],nodes)',
   'translated_return_or_threshold(c)','translated_external_call_boundary(c)',
   'virtual_helper(c.next_pc,c','translated_post_call(c)'):
   self.assertIn(marker,source)
  self.assertIn('kNode+0xc0',(ROOT/'src/handler_list_probe.cpp').read_text())
  self.assertIn('budokai2_phase225_original_list_irq_chain',(ROOT/'standalone/CMakeLists.txt').read_text())
  for dangerous in ('WriteProcessMemory','setIrqHandlerEnabled(','registerFunction('):
   self.assertNotIn(dangerous,source)
 def test_fresh_launcher_and_manifest(self):
  self.assertIn('RUN_PHASE225.py',(ROOT/'DOUBLE_CLICK_PHASE225.cmd').read_text())
  self.assertIn('COLLECT_PHASE225_DIAGNOSTICS.py',(ROOT/'ONE_CLICK_PHASE225_DIAGNOSTICS.cmd').read_text())
  runner=(ROOT/'RUN_PHASE225.py').read_text()
  self.assertIn('PHASE225_ORIGINAL_LIST_IRQ_HELPER_RETAIL.json',runner)
  self.assertIn('PHASE225_ORIGINAL_LIST_IRQ_HELPER_SYNTHETIC.json',runner)
  self.assertNotIn('RUN_PHASE224.py',runner)
  manifest=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text())
  for need in ('RUN_PHASE225.py','tests/test_phase225.py','src/phase225_original_list_to_irq_helper.cpp',
               '_engine/phase225_list_irq_chain.py','DOUBLE_CLICK_PHASE225.cmd'):
   self.assertIn(need,manifest)
  for name,digest in manifest.items():
   self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),digest,name)
   self.assertFalse(name.lower().endswith(('.iso','.elf','.exe','.rar','.bin','.o','.obj','.pdb')))

if __name__=='__main__':unittest.main()
