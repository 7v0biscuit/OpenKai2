"""standalone live guest byte-memory/native integration regression tests."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.live_guest_memory import cases,expected,mutated_original
from oracles.list_irq_chain import expected as old_expected
from oracles.list_walk import SYNTHETIC
class LiveGuestMemory(unittest.TestCase):
 def test_patch_matrix(self):
  caseset=list(cases())
  self.assertEqual(len(caseset),1440)
  self.assertEqual(len({x[0] for x in caseset}),1440)
  for c in caseset:self.assertEqual(len(c),37)
 def test_unpatched_identical_to_list_irq_chain(self):
  for c in cases():
   if c[-3]==0:
    self.assertEqual(expected(c)[:46],old_expected(c[:-3]))
 def test_live_mutation_is_detectable(self):
  before={};changed=0;admitted=0;reads=0
  for c in cases():
   result=expected(c)
   self.assertEqual(len(result),48)
   self.assertGreaterEqual(result[-2],0)
   self.assertEqual(result[-1]%4,0)
   reads+=result[-2]
   if result[1]:admitted+=1
   k=c[0].rsplit('_mem',1)[0]
   if c[-3]==0:before[k]=result
   elif result!=before[k]:changed+=1
  self.assertGreater(changed,50)
  self.assertEqual(admitted,960)
  self.assertGreater(reads,0)
 def test_suppressed_never_reads_or_patches_guest_memory(self):
  for c in cases():
   if c[1]&7!=7:
    value=expected(c)
    self.assertEqual(value[-2:],(0,0))
    self.assertEqual(value[3],0)
 def test_opcodes_detect_corruption(self):
  bad=dict(SYNTHETIC);bad[0x1137f4]=0xffffffff
  example=next(c for c in cases() if c[1]==7)
  with self.assertRaises(ValueError):expected(example,list_words=bad)
 def test_cpp_uses_cpu_memory_and_enforces_bounds(self):
  s=(ROOT/'src/probes/live_guest_memory.cpp').read_text()
  self.assertIn('translate_live_memory(c.r[16],c.r[18],c.r[7],c.r[6],c.memory,reads)',s)
  self.assertIn('c.memory.write(kNode+patch_node*0x40U+off',s)
  self.assertIn('guest.read(static_cast<uint32_t>(location),8)',s)
  self.assertIn('if(location>0xffffffffULL)',s)
  self.assertIn('guest.read(lo,4)',s)
  self.assertNotIn('Result traversal=translate(',s)
  for unsafe in ('WriteProcessMemory','registerFunction(','setIrqHandlerEnabled('):self.assertNotIn(unsafe,s)
 def test_standalone_source_integrity(self):
  self.assertIn('tools/probes/run.py',(ROOT/'tools/probes/run.cmd').read_text())
  self.assertIn('tools/probes/collect.py',(ROOT/'tools/probes/collect.cmd').read_text())
  runner=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('LIVE_GUEST_MEMORY_SYNTHETIC.json',runner)
  self.assertIn('LIVE_GUEST_MEMORY_RETAIL.json',runner)
  cmake=(ROOT/'CMakeLists.txt').read_text()
  self.assertIn('openkai2_live_guest_memory',cmake)
if __name__=='__main__':unittest.main()
