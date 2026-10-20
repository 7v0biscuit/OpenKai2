"""Phase220: admission policy is virtual, helper oracle still opcode-driven."""
from __future__ import annotations
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from _engine.phase220_handoff import cases,expected
from _engine.phase217_semantics import synthetic_instructions

class Phase220(unittest.TestCase):
 def test_case_count_and_all_gates(self):
  all_cases=list(cases())
  self.assertEqual(len(all_cases),1792)
  self.assertEqual({c[0] for c in all_cases},{0x113400,0x113410})
  self.assertEqual({c[1] for c in all_cases},set(range(16)))
 def test_not_dispatched_if_queued_but_route_masked(self):
  words=synthetic_instructions();case=next(c for c in cases() if c[1]==5)
  result=expected(words,case)
  self.assertEqual(result[:4],(case[0],5,0,1))
  self.assertEqual(result[4:],(0,)*27)
 def test_not_dispatched_if_handler_not_registered(self):
  words=synthetic_instructions();case=next(c for c in cases() if c[1]==3)
  result=expected(words,case)
  self.assertEqual(result[2:4],(0,1))
 def test_no_event_means_no_dispatch(self):
  words=synthetic_instructions();case=next(c for c in cases() if c[1]==14)
  self.assertEqual(expected(words,case)[2:4],(0,0))
 def test_dispatch_retains_pending_without_ack(self):
  words=synthetic_instructions();case=next(c for c in cases() if c[1]==7)
  result=expected(words,case)
  self.assertEqual(result[2:4],(1,1))
  self.assertEqual(len(result),31)
  self.assertIn(result[26],(1,2))
 def test_acknowledge_is_distinct_from_dispatch(self):
  words=synthetic_instructions();case=next(c for c in cases() if c[1]==15)
  result=expected(words,case)
  self.assertEqual(result[2:4],(1,0))
  self.assertEqual(len(result),31)
 def test_no_original_irq_handler_or_host_irq_access(self):
  cpp=(ROOT/'src/phase220_irq11_handoff.cpp').read_text()
  self.assertIn('wrapper=execute(wrapper,entry)',cpp)
  self.assertIn('s=run(s)',cpp)
  self.assertIn('if(!dispatch)',cpp)
  for forbidden in ('WriteProcessMemory','VirtualAlloc','__writecr','registerFunction','addIrqHandler','outb('):
   self.assertNotIn(forbidden,cpp)
 def test_runner_independent(self):
  runner=(ROOT/'RUN_PHASE220.py').read_text()
  self.assertIn('compare_handoff',runner)
  self.assertIn('PHASE220_RETAIL_IRQ11_HANDOFF.json',runner)
  self.assertIn("z.write(p,'phase220/'+p.name)",runner)
  self.assertNotIn('RUN_PHASE219.py',runner)
  self.assertNotIn('stage/register_functions_phase123',runner)
  self.assertTrue((ROOT/'DOUBLE_CLICK_PHASE220.cmd').is_file())
  self.assertTrue((ROOT/'ONE_CLICK_PHASE220_DIAGNOSTICS.cmd').is_file())
 def test_manifest_and_source_only(self):
  m=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text())
  self.assertIn('src/phase220_irq11_handoff.cpp',m)
  self.assertIn('_engine/phase220_handoff.py',m)
  self.assertIn('RUN_PHASE220.py',m)
  self.assertIn('COLLECT_PHASE220_DIAGNOSTICS.py',m)
  for name,d in m.items():
   self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),d,name)
   self.assertNotIn('UPLOAD_',name)
   self.assertNotIn('build_',name)
   self.assertFalse(name.lower().endswith(('.iso','.elf','.exe','.bin','.rar','.o','.obj')))

if __name__=='__main__':unittest.main()
