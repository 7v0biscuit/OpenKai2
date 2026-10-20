"""bounded original-handler list-then-helper native integration."""
import hashlib,json,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.list_irq_chain import cases,expected
from oracles.list_walk import SYNTHETIC

class ListIrqChain(unittest.TestCase):
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
  source=(ROOT/'src/probes/list_irq_chain.cpp').read_text()
  for marker in ('translated_entry_prefix(c)','translated_successor(c)',
   'translate(c.r[16],c.r[18],c.r[7],c.r[6],nodes)',
   'translated_return_or_threshold(c)','translated_external_call_boundary(c)',
   'virtual_helper(c.next_pc,c','translated_post_call(c)'):
   self.assertIn(marker,source)
  self.assertIn('kNode+0xc0',(ROOT/'src/probes/handler_list_probe.cpp').read_text())
  self.assertIn('openkai2_list_irq_chain',(ROOT/'CMakeLists.txt').read_text())
  for dangerous in ('WriteProcessMemory','setIrqHandlerEnabled(','registerFunction('):
   self.assertNotIn(dangerous,source)
 def test_launcher_and_reports(self):
  self.assertIn('tools/probes/run.py',(ROOT/'tools/probes/run.cmd').read_text())
  self.assertIn('tools/probes/collect.py',(ROOT/'tools/probes/collect.cmd').read_text())
  runner=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('ORIGINAL_LIST_IRQ_HELPER_RETAIL.json',runner)
  self.assertIn('ORIGINAL_LIST_IRQ_HELPER_SYNTHETIC.json',runner)

if __name__=='__main__':unittest.main()
