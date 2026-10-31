"""Standalone MappedGuestMemory bounded guest virtual address mapping tests."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.mapped_guest_memory import (cases,expected,phys,Fault,
  GuestMemory,memory_cases,memory_expected,aliased)
from oracles.list_walk import SYNTHETIC,WIN_SHA

class MappedGuestMemoryTests(unittest.TestCase):
 def test_matrix_and_unique(self):
  cc=list(cases())
  self.assertEqual(len(cc),2880)
  self.assertEqual(len({(c[0],c[-2],c[-1]) for c in cc}),2880)
  self.assertTrue(all(len(c)==39 for c in cc))
  self.assertEqual(len(list(memory_cases())),144)
 def test_direct_mapped_aliases_are_coherent(self):
  g=GuestMemory()
  for a in (0x210040,0x80210040,0xa0210040):
   g.write(a,4,0x12345678)
   for b in (0x210040,0x80210040,0xa0210040):
    self.assertEqual(g.read(b,4),0x12345678)
 def test_invalid_segments_rejected(self):
  for a in (0x20000000,0x70000000,0xc0210040,0xf0210040,0x210100):
   with self.assertRaises(Fault) as exc:phys(a,4)
   self.assertEqual(exc.exception.code,4)
 def test_atomic_guest_writes(self):
  g=GuestMemory()
  g.write(0x2100fc,4,0x11223344)
  for a,w in ((0x2100fc,8),(0x210100,1),(0x802100fc,8)):
   with self.assertRaises(Fault):g.write(a,w,0xffffffffffffffff)
   self.assertEqual(g.read(0x2100fc,4),0x11223344)
 def test_misaligned_access_rejected(self):
  for a,w in ((0x210041,8),(0xa0210041,4),(0x210043,2)):
   with self.assertRaises(Fault) as exc:phys(a,w)
   self.assertEqual(exc.exception.code,3)
 def test_faults_do_not_enter_suppressed_dispatch(self):
  for c in cases():
   if (c[1]&7)!=7:
    got=expected(c)
    self.assertEqual(got[3],0)
    self.assertEqual(got[4],0)
    self.assertEqual(got[5],0)
 def test_opcode_corruption_is_rejected(self):
  bad=dict(SYNTHETIC);bad[0x1137f4]=0xffffffff
  candidate=next(c for c in cases() if c[1]==7 and c[-2]==1 and c[-1]==0)
  with self.assertRaises(ValueError):expected(candidate,bad)
 def test_alias_cross_segment_mutation_observable(self):
  cc=list(cases());ok=[c for c in cc if c[1]==7 and c[-1]==0 and c[-2]>0]
  self.assertTrue(ok)
  before={};changed=0
  for c in ok:
   key=(c[0].rsplit('_mem',1)[0],c[-2])
   got=expected(c)
   if c[0].endswith('_mem0'):before[key]=got
   elif key in before and got!=before[key]:changed+=1
  self.assertGreater(changed,0)
 def test_safe_cpp_integration_and_runner(self):
  cpp=(ROOT/'src/probes/mapped_guest_memory.cpp').read_text()
  mem=(ROOT/'src/probes/handler_list_probe.cpp').read_text()
  self.assertIn('translate_live_memory(c.r[16],c.r[18],c.r[7],c.r[6],c.memory,audit)',cpp)
  self.assertIn('translated_entry_prefix(c)',cpp)
  self.assertIn('translated_successor(c)',cpp)
  self.assertIn('const auto addresses=validate(addr,n)',mem)
  self.assertIn('return addr&0x1fffffffU;',mem)
  for banned in ('WriteProcessMemory','VirtualProtect(','registerFunction(','setIrqHandlerEnabled('):
   self.assertNotIn(banned,cpp)
  self.assertIn('tools/probes/run.py',(ROOT/'tools/probes/run.cmd').read_text())
  self.assertIn('tools/probes/collect.py',(ROOT/'tools/probes/collect.cmd').read_text())
  self.assertIn('openkai2_mapped_guest_memory',(ROOT/'CMakeLists.txt').read_text())
  runner=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn('MAPPED_GUEST_MEMORY_RETAIL.json',runner)
  self.assertIn(" 'mapped_memory':'openkai2_mapped_guest_memory'",runner)
if __name__=='__main__':unittest.main()
