"""standalone integration and no-external-runner contracts."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.wrapper_chain import (
 ENTRIES, PORTS,scenarios,synthetic_instructions,oracle,STOP_ERET,STOP_EI_LIMIT)

class WrapperChainTests(unittest.TestCase):
 def test_all_448_scenarios(self):
  cases=list(scenarios())
  self.assertEqual(len(cases),448)
  self.assertEqual({c[0] for c in cases},set(ENTRIES))
  self.assertEqual({len(c) for c in cases},{13})
 def test_wrapper_to_helper_port_and_data(self):
  words=synthetic_instructions()
  stops={1:0,2:0}
  for case in scenarios():
   ans=oracle(words,case)
   self.assertEqual(len(ans),27)
   self.assertEqual(ans[0],case[0])
   self.assertEqual(ans[1],0x113380)
   self.assertEqual(ans[2],PORTS[case[0]])
   self.assertEqual(ans[3],case[7])
   self.assertEqual(ans[4],case[9])
   self.assertEqual(ans[12],PORTS[case[0]]) # integrated v0/v1/a2/a0/a1 positions
   self.assertEqual(ans[13],case[7])
   self.assertEqual(ans[14],case[9])
   self.assertEqual(ans[15],case[10])
   stops[ans[22]]+=1
   if ans[22]==STOP_ERET:
    self.assertEqual(ans[23],PORTS[case[0]])
    self.assertEqual(ans[24],case[7]&0xffffffff)
    self.assertEqual(ans[25],1)
   else:
    self.assertEqual(ans[22],STOP_EI_LIMIT)
    self.assertEqual(ans[23],0)
    self.assertEqual(ans[25],0)
  self.assertEqual(stops,{1:384,2:64})
 def test_per_entry_virtual_store(self):
  words=synthetic_instructions()
  counts={p:0 for p in PORTS.values()}
  for case in scenarios():
   ans=oracle(words,case)
   if ans[22]==STOP_ERET:counts[ans[23]]+=1
  self.assertEqual(counts,{p:192 for p in PORTS.values()})
 def test_corrupt_wrapper_op_rejected(self):
  wrappers,shared=synthetic_instructions()
  altered=dict(wrappers)
  altered[ENTRIES[0]]=(0,)+wrappers[ENTRIES[0]][1:]
  with self.assertRaises(ValueError):oracle((altered,shared),next(c for c in scenarios() if c[0]==ENTRIES[0]))
 def test_corrupt_shared_privileged_instruction_rejected(self):
  wrappers,shared=synthetic_instructions();bad=dict(shared);bad[0x1133e4]=0
  with self.assertRaises(ValueError):oracle((wrappers,bad),next(c for c in scenarios() if c[1]==0))
 def test_cpp_links_both_unchanged_models(self):
  cpp=(ROOT/'src/probes/wrapper_chain.cpp').read_text()
  self.assertIn('#include "tail_wrappers.cpp"',cpp)
  self.assertIn('#include "mmio_eret.cpp"',cpp)
  self.assertIn('wrapper=execute(wrapper,entry)',cpp)
  self.assertIn('s=run(s)',cpp)
  self.assertNotIn('registerFunction(',cpp)
 def test_cmake_integration_target(self):
  cmake=(ROOT/'CMakeLists.txt').read_text()
  self.assertIn('openkai2_wrapper_chain',cmake)
  self.assertIn('wrapper_chain.cpp',cmake)
 def test_runner_integration(self):
  runner=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('compare_chain',runner)
  self.assertIn('pinned_retail(blob)',runner)
 def test_no_real_hardware_implementations(self):
  src=(ROOT/'src/probes/wrapper_chain.cpp').read_text()
  self.assertNotIn('VirtualAlloc',src)
  self.assertNotIn('WriteProcessMemory',src)
  self.assertIn('local test state',src)

if __name__=='__main__':unittest.main()
