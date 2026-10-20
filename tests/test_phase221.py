"""Standalone original-handler -> translated tail wrappers -> virtual helper."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from _engine.helper_boundary_oracle import CASES,expected
from _engine.phase221_bridge import bridge_cases,VIRTUAL_CPU_STATES

class Phase221(unittest.TestCase):
 def test_bridged_cases_reach_both_actual_handler_call_sites(self):
  cases=list(bridge_cases())
  self.assertEqual(len(cases),60)
  self.assertEqual({int(r['external_call_target'],16) for _,_,r in cases},{0x113400,0x113410})
  self.assertEqual({int(r['external_call_site'],16) for _,_,r in cases},{0x1138b8,0x113884})
 def test_privileged_operations_remain_virtual(self):
  cpp=(ROOT/'src/phase221_handler_helper_bridge.cpp').read_text()
  self.assertIn('translated_entry_prefix(cpu)',cpp)
  self.assertIn('translated_external_call_boundary(cpu)',cpp)
  self.assertIn('w=execute(w,',cpp)
  self.assertIn('s=run(s)',cpp)
  for forbidden in ('WriteProcessMemory','VirtualAlloc','registerFunction','addIrqHandler','outb('):
   self.assertNotIn(forbidden,cpp)
 def test_inherited_real_handler_source_is_separate_from_irq11_gate(self):
  s=(ROOT/'src/handler_list_probe.cpp').read_text()
  self.assertIn('B2_HANDLER_LIBRARY_ONLY',s)
  self.assertIn('cpu.external_call_pending=true',s)
  self.assertIn('cpu.external_call_executed=false',s) if 'cpu.external_call_executed=false' in s else self.assertIn('bool external_call_executed=false',s)
 def test_independent_runner_and_source_contract(self):
  runner=(ROOT/'RUN_PHASE221.py').read_text()
  self.assertIn('compare_handler_bridge',runner)
  self.assertIn('verified_handler_rows',runner)
  self.assertIn('PHASE221_HANDLER_TO_HELPER_RETAIL.json',runner)
  self.assertIn('original_guest_timer2_irq_handler_executed',runner)
  self.assertNotIn("cmd([sys.executable,'RUN_PHASE220.py'",runner)
  self.assertTrue((ROOT/'DOUBLE_CLICK_PHASE221.cmd').is_file())
  self.assertIn('budokai2_phase221_handler_helper_bridge',(ROOT/'standalone/CMakeLists.txt').read_text())
 def test_manifest_and_no_old_phase_dependency(self):
  m=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text())
  for name in ('RUN_PHASE221.py','src/handler_list_probe.cpp','src/phase221_handler_helper_bridge.cpp','_engine/helper_boundary_oracle.py'):
   self.assertIn(name,m)
  for name,digest in m.items():
   self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),digest,name)
   self.assertNotIn('UPLOAD_',name)
   self.assertNotIn('build_',name)
   self.assertFalse(name.lower().endswith(('.iso','.elf','.exe','.bin','.rar','.o','.obj')))
if __name__=='__main__':unittest.main()
