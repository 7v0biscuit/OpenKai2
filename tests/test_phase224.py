"""Phase224 original guest-linked-list semantics, source-only release gates."""
import hashlib,json,subprocess,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from _engine.phase224_list_walk import (SYNTHETIC,WIN_SHA,START,END,WORDS,cases,opcode_oracle,checked_retail)
class Phase224(unittest.TestCase):
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
  cm=(ROOT/'standalone/CMakeLists.txt').read_text()
  self.assertIn('budokai2_phase224_original_list_walk',cm)
  source=(ROOT/'src/phase224_original_list_walk.cpp').read_text()
  for term in ('MAX_BACKEDGES','likely_annulled','o.likely_taken','o.first_branch_exit','o.null_link_exit','s32(prior.next)'):
   self.assertIn(term,source)
  for danger in ('WriteProcessMemory','registerFunction(','setIrqHandlerEnabled('):
   self.assertNotIn(danger,source)
 def test_runner_requires_no_history(self):
  runner=(ROOT/'RUN_PHASE224.py').read_text()
  self.assertIn('compare_list_walk',runner)
  self.assertIn('checked_list_words(blob)',runner)
  self.assertIn('PHASE224_ORIGINAL_LIST_WALK_RETAIL.json',runner)
  self.assertNotIn('RUN_PHASE223.py',runner)
  self.assertIn('RUN_PHASE224.py',(ROOT/'DOUBLE_CLICK_PHASE224.cmd').read_text())
 def test_manifest_integrity(self):
  manifest=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text())
  for name in ('RUN_PHASE224.py','src/phase224_original_list_walk.cpp',
   '_engine/phase224_list_walk.py','COLLECT_PHASE224_DIAGNOSTICS.py',
   'DOUBLE_CLICK_PHASE224.cmd','tests/test_phase224.py'):
   self.assertIn(name,manifest)
  for name,d in manifest.items():
   file=ROOT/name
   self.assertTrue(file.is_file(),name)
   self.assertEqual(hashlib.sha256(file.read_bytes()).hexdigest(),d,name)
   self.assertFalse(name.startswith('UPLOAD_'))
   self.assertFalse(name.lower().endswith(('.elf','.iso','.exe','.pdb','.obj','.o','.bin','.rar')))
if __name__=='__main__':unittest.main()
