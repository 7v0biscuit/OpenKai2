"""clean-install regression for EE interrupt-mode contracts."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.interrupt_transitions import (STATES,can_execute_ei,ei_effect,
 observed_checkpoint,transitions,scenarios,independent_oracle,synthetic_instructions)
from oracles.mmio_eret import STOP_ERET,STOP_EI_LIMIT

class InterruptTransitionsTests(unittest.TestCase):
 def test_matrix(self):
  cases=list(scenarios())
  self.assertEqual(len(cases),384)
  self.assertEqual({x[0] for x in cases},{0x113400,0x113410})
  self.assertEqual({x[2] for x in cases},{0,1,2,3})
  self.assertEqual({x[1] for x in cases},set(STATES))
 def test_kernel_ei_enables_eie(self):
  self.assertTrue(can_execute_ei(0))
  self.assertEqual(ei_effect(0),0x10000)
  self.assertEqual(ei_effect(0x5),0x10005)
 def test_user_supervisor_ei_respects_edi(self):
  for ksu in (0x8,0x10,0x18):
   self.assertFalse(can_execute_ei(ksu))
   self.assertEqual(ei_effect(ksu),ksu)
   self.assertTrue(can_execute_ei(ksu|0x20000))
   self.assertEqual(ei_effect(ksu|0x20000),ksu|0x30000)
 def test_exception_level_is_effective_kernel(self):
  for flag in (2,4):
   self.assertTrue(can_execute_ei(0x10|flag))
   self.assertEqual(ei_effect(0x10|flag),0x10010|flag)
 def test_external_event_is_not_ei(self):
  self.assertEqual(transitions(0x10001,0),(0x10001,0x10001))
  self.assertEqual(transitions(0x10001,1),(0x1,0x10001))
  self.assertEqual(transitions(0x10001,2),(0x10001,0x1))
  self.assertEqual(transitions(0x10001,3),(0x1,0x1))
 def test_checked_event_modes_only(self):
  with self.assertRaises(ValueError):observed_checkpoint(0,4,1)
  with self.assertRaises(ValueError):observed_checkpoint(0,1,3)
 def test_no_external_change_can_poll_limit(self):
  words=synthetic_instructions()
  test=next(c for c in scenarios() if c[1]==0x10000 and c[2]==0)
  result=independent_oracle(words,test)
  self.assertEqual(result[24],STOP_EI_LIMIT)
 def test_without_eie_skips_poll(self):
  words=synthetic_instructions()
  test=next(c for c in scenarios() if c[1]==0)
  result=independent_oracle(words,test)
  self.assertEqual(result[24],STOP_ERET)
  self.assertEqual(result[19],0)
 def test_injected_clear_can_end_poll(self):
  words=synthetic_instructions()
  test=next(c for c in scenarios() if c[1]==0x10000 and c[2]==1)
  result=independent_oracle(words,test)
  self.assertEqual(result[24],STOP_ERET)
  self.assertEqual(result[19],1)
 def test_all_cases_validate_with_opcode_oracle(self):
  words=synthetic_instructions();stops={1:0,2:0}
  for x in scenarios():
   result=independent_oracle(words,x)
   self.assertEqual(len(result),29)
   stops[result[24]]+=1
  self.assertEqual(stops,{1:294,2:90})
 def test_cmake_and_fresh_runner(self):
  cmake=(ROOT/'CMakeLists.txt').read_text()
  self.assertIn('openkai2_interrupt_transitions',cmake)
  runner=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('compare_ee_model',runner)
 def test_never_dereference_mmio_host_address(self):
  code=(ROOT/'src/probes/interrupt_transitions.cpp').read_text()
  self.assertIn('B2_WRAPPER_CHAIN_LIBRARY_ONLY',code)
  self.assertIn('checkpoint(',code)
  self.assertNotIn('WriteProcessMemory',code)
  self.assertNotIn('VirtualAlloc',code)
  self.assertNotIn('registerFunction(',code)

if __name__=='__main__':unittest.main()
