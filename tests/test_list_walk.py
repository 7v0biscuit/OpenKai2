"""original guest-linked-list semantics, source-only release gates."""
import hashlib,json,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.list_walk import (SYNTHETIC,WIN_SHA,START,END,WORDS,cases,opcode_oracle,checked_retail)
class ListWalk(unittest.TestCase):
 def test_original_window_count_and_digest(self):
  self.assertEqual(len(WORDS),27)
  self.assertEqual((START,END),(0x1137f4,0x113860))
  self.assertEqual(len(SYNTHETIC),27)
  self.assertEqual(len(WIN_SHA),64)
 def test_cases_and_terminations(self):
  inputs=list(cases());self.assertEqual(len(inputs),360)
  outputs=[opcode_oracle(c) for c in inputs]
  self.assertTrue(all(o[-1] in (1,2) for o in outputs))
  for index in (10,11,12,13):self.assertTrue(any(o[index] for o in outputs))
  self.assertTrue(any(o[-1]==2 and o[0]==0x113820 for o in outputs))
  self.assertTrue(any(o[-1]==1 and o[0]==0x113860 for o in outputs))
 def test_likely_taken_delay_slot_annul_contract(self):
  by_name={c[0]:opcode_oracle(c) for c in cases()}
  self.assertEqual(by_name["branch2_0_0_0"][12:14],(1,0))
  self.assertEqual(by_name["branch3_0_0_0"][12:14],(0,1))
  self.assertEqual(by_name["branch6_0_0_0"][12:14],(0,4))
 def test_retail_requires_correct_executable(self):
  with self.assertRaises(ValueError):checked_retail(b'\x7fELFnottheownedUSAexecutable')
 def test_interpreter_rejects_corrupt_word(self):
  item=next(iter(cases()))
  corrupt=dict(SYNTHETIC);corrupt[START]=0xffffffff
  with self.assertRaises(ValueError):opcode_oracle(item,corrupt)
 def test_compiler_target_and_isolation(self):
  cm=(ROOT/'CMakeLists.txt').read_text()
  self.assertIn('openkai2_list_walk',cm)
  source=(ROOT/'src/probes/list_walk.cpp').read_text()
  for term in ('MAX_BACKEDGES','likely_annulled','o.likely_taken','o.first_branch_exit','o.null_link_exit','s32(prior.next)'):
   self.assertIn(term,source)
  for danger in ('WriteProcessMemory','registerFunction(','setIrqHandlerEnabled('):
   self.assertNotIn(danger,source)
 def test_runner_requires_no_history(self):
  runner=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('compare_list_walk',runner)
  self.assertIn('checked_list_words(blob)',runner)
  self.assertIn('ORIGINAL_LIST_WALK_RETAIL.json',runner)
  self.assertIn('tools/probes/run.py',(ROOT/'tools/probes/run.cmd').read_text())
if __name__=='__main__':unittest.main()
