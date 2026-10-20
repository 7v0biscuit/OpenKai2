"""Bundled, source-only MmioEret tests. The user's retail ELF is not needed."""
import hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/probes'))
from oracles.mmio_eret import (
 synthetic_words,oracle,scenarios,MMIO_ADDRS,INVALID_ADDRS,
 STOP_ERET,STOP_EI_LIMIT,STOP_INVALID_MMIO)
from oracles.shared_entry import validate_window
from oracles.retail_elf import AuditError

class MmioEretTests(unittest.TestCase):
 def test_isa_assembled_suffix(self):
  words=synthetic_words()
  self.assertEqual(len(words),28)
  self.assertEqual(words[0x1133d8],0xAC850000)
  self.assertEqual(words[0x1133dc],0x409FF000)
  self.assertEqual(words[0x1133e0],0x0000040F)
  self.assertEqual(words[0x1133e4],0x42000018)
  self.assertEqual(words[0x1133e8],0x03E00008)
 def test_case_matrix(self):
  arr=list(scenarios())
  self.assertEqual(len(arr),560)
  self.assertEqual({len(v) for v in arr},{12})
  self.assertEqual({v[6] for v in arr},set(MMIO_ADDRS+INVALID_ADDRS))
 def test_all_paths_and_guest_address_guard(self):
  counts={1:0,2:0,3:0}
  for case in scenarios():
   ans=oracle(synthetic_words(),case)
   counts[ans[17]]+=1
   self.assertEqual(len(ans),22)
   if ans[17]==STOP_ERET:
    self.assertIn(ans[18],MMIO_ADDRS)
    self.assertEqual(ans[15],1)
    self.assertEqual(ans[20],1)
    self.assertEqual(ans[21],1)
    self.assertEqual(ans[19],case[7]&0xffffffff)
    self.assertEqual(ans[0],case[8]&0xffffffff)
    self.assertEqual(ans[2],case[8]&0xffffffff)
    self.assertEqual(ans[1]&4,0)
   else:
    self.assertEqual(ans[15],0)
    self.assertEqual(ans[20],0)
    self.assertEqual(ans[21],0)
  self.assertEqual(counts,{1:288,2:80,3:192})
 def test_no_ei_eret(self):
  initial=(0,0,0,0,0,0,MMIO_ADDRS[0],0xFFFF123489ABCDEF,0xFEDCBA989ABCDEF0,0,0x12004455,0x88887777)
  got=oracle(synthetic_words(),initial)
  self.assertEqual(got[0],0x9ABCDEF0)
  self.assertEqual(got[2],0x9ABCDEF0)
  self.assertEqual(got[15],1)
  self.assertEqual(got[12:16],(0,1,3,1))
  self.assertEqual(got[16],0x113435637)
 def test_status_erL_after_eret(self):
  got=oracle(synthetic_words(),(2,0,0,0,0,0,MMIO_ADDRS[1],3,0x12345678,0,0xAAAAAAAA,0x55555555))
  self.assertEqual(got[17],STOP_ERET)
  self.assertEqual(got[0],0x12345678)
  self.assertFalse(got[1]&4)
 def test_ei_limit_does_not_store(self):
  got=oracle(synthetic_words(),(0x10000,0x10000,0x10000,0,0,0,MMIO_ADDRS[0],4,5,0,6,7))
  self.assertEqual(got[17],STOP_EI_LIMIT)
  self.assertEqual(got[0],0x113398)
  self.assertEqual(got[15],0)
 def test_nonlisted_address_rejected(self):
  for x in INVALID_ADDRS:
   got=oracle(synthetic_words(),(0,0,0,0,0,0,x,0xABCD,0x2345,0,0,0))
   self.assertEqual(got[17],STOP_INVALID_MMIO)
   self.assertEqual(got[0],0x1133d8)
   self.assertEqual(got[15],0)
 def test_store_width(self):
  got=oracle(synthetic_words(),(0,0,0,0,0,0,MMIO_ADDRS[2],0xFFFFABCD12345678,0xFFFFFFFFFFFFFFFF,0,0,0))
  self.assertEqual(got[19],0x12345678)
  self.assertEqual(got[2],0xFFFFFFFF)
 def test_bad_eret_opcode_rejected(self):
  words=synthetic_words();words[0x1133e4]=0
  with self.assertRaises(ValueError):oracle(words,(0,0,0,0,0,0,MMIO_ADDRS[0],0,0,0,0,0))
 def test_bad_error_epc_opcode_rejected(self):
  words=synthetic_words();words[0x1133dc]=0x409FE800
  with self.assertRaises(ValueError):oracle(words,(0,0,0,0,0,0,MMIO_ADDRS[0],0,0,0,0,0))
 def test_full_window_digest_negative(self):
  with self.assertRaises(AuditError):validate_window([0]*64)
 def test_self_contained_and_no_host_mmio(self):
  code=(ROOT/'tools/probes/run.py').read_text()
  self.assertIn("write('RETAIL_'+key",code)
  cpp=(ROOT/'src/probes/mmio_eret.cpp').read_text()
  self.assertIn('allowed_virtual_address',cpp)
  self.assertNotIn('registerFunction(',cpp)
  self.assertIn('0x1133E4 ERET',cpp)
 def test_cmake_target_coverage(self):
  c=(ROOT/'CMakeLists.txt').read_text()
  for x in ('mmio_eret.cpp','virtual_cp0.cpp','shared_entry.cpp','tail_wrappers.cpp'):
   self.assertIn(x,c)

if __name__=='__main__':unittest.main()
