"""Standalone Phase 219 native replay and honest external observation intake."""
from __future__ import annotations
import hashlib,json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from _engine.phase219_observations import (
 native_cases,expected,parse_observation,verify_trace,MAX_TRACE_BYTES)
class Phase219(unittest.TestCase):
 def test_contract_cases_and_coverage(self):
  cases=list(native_cases())
  self.assertEqual(len(cases),432)
  self.assertEqual({c[0] for c in cases},{1,2,3})
  self.assertEqual({c[3] for c in cases},{0,1,2})
 def test_ei_kernel_and_edi_permissions(self):
  self.assertEqual(expected(1,0), (0x10000,0x11339c))
  self.assertEqual(expected(1,0x10), (0x10,0x11339c))
  self.assertEqual(expected(1,0x20010), (0x30010,0x11339c))
 def test_mtc0_low_word_and_event(self):
  self.assertEqual(expected(2,0,0xfeed0003,1),(0xfeec0003,0x1133d4))
 def test_eret_both_paths(self):
  self.assertEqual(expected(3,0x4,0,0,0x1234,0x5678),(0,0x5678))
  self.assertEqual(expected(3,0x2,0,0,0x1234,0x5678),(0,0x1234))
 def test_known_synthetic_example_is_not_authenticated(self):
  sample=ROOT/'examples/SYNTHETIC_DO_NOT_SUBMIT_AS_HARDWARE_TRACE.jsonl'
  result=verify_trace(sample)
  self.assertEqual(result['matches'],3)
  self.assertEqual(result['mismatches'],0)
  self.assertFalse(result['trace_authenticity_verified'])
  self.assertFalse(result['actual_ps2_irq_timing_verified'])
 def test_declared_unknown_event_is_inconclusive(self):
  row={'source':'pcsx2','capture_id':'unverified','instruction':'ei',
       'pc':'0x00113398','status_before':'0x00000000',
       'status_after':'0x00000000','external_event':'unknown'}
  self.assertEqual(parse_observation(row,1)['verdict'],'INCONCLUSIVE_UNKNOWN_EXTERNAL_EVENT')
 def test_wrong_observed_result_is_flagged(self):
  row={'source':'pcsx2','capture_id':'unverified','instruction':'ei',
       'pc':'0x00113398','status_before':'0x00000000',
       'status_after':'0x00000000','external_event':'none'}
  self.assertEqual(parse_observation(row,1)['verdict'],'MISMATCH')
 def test_does_not_trust_reported_source_label(self):
  row={'source':'real_ps2','capture_id':'no-authentication','instruction':'eret',
       'pc':'0x001133E4','status_before':'0x00000004',
       'status_after':'0x00000000','external_event':'none',
       'epc':'0x00003000','error_epc':'0x80000000'}
  self.assertFalse(parse_observation(row,1)['independent_trace_authenticity_verified'])
 def test_error_on_wrong_pc(self):
  row={'source':'pcsx2','capture_id':'one','instruction':'ei',
       'pc':'0x00113400','status_before':'0x00000000',
       'status_after':'0x00010000','external_event':'none'}
  with self.assertRaises(ValueError):parse_observation(row,1)
 def test_missing_external_event_is_not_assumed_none(self):
  row={'source':'pcsx2','capture_id':'one','instruction':'ei',
       'pc':'0x00113398','status_before':'0x00000000','status_after':'0x00010000'}
  with self.assertRaises(ValueError):parse_observation(row,1)
 def test_multiline_replay_and_mismatch(self):
  rows=[{'source':'unverified_manual','capture_id':'one','instruction':'ei',
       'pc':'0x00113398','status_before':'0x00000000',
       'status_after':'0x00010000','external_event':'none'},
       {'source':'pcsx2','capture_id':'two','instruction':'ei',
       'pc':'0x00113398','status_before':'0x00000000',
       'status_after':'0x00000000','external_event':'none'}]
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)/'capture.jsonl';p.write_text('\n'.join(json.dumps(row) for row in rows))
   result=verify_trace(p)
  self.assertEqual(result['status'],'OBSERVATIONS_DISAGREE')
  self.assertEqual(result['matches'],1)
  self.assertEqual(result['mismatches'],1)
 def test_trace_size_limit(self):
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)/'too-large.jsonl';p.write_bytes(b' '* (MAX_TRACE_BYTES+1))
   with self.assertRaises(ValueError):verify_trace(p)
 def test_source_only_and_standalone(self):
  cmake=(ROOT/'standalone/CMakeLists.txt').read_text()
  self.assertIn('budokai2_phase219_cp0_observation_replay',cmake)
  runner=(ROOT/'RUN_PHASE219.py').read_text()
  self.assertIn('run_observations',runner)
  self.assertIn('verify_trace',runner)
  self.assertIn('EXPLICIT_TRACE_FILE_MISSING',runner)
  self.assertNotIn('RUN_PHASE218.py',runner)
  self.assertNotIn('register_functions_phase123',runner)
  src=(ROOT/'src/phase219_cp0_observation_replay.cpp').read_text()
  self.assertNotIn('WriteProcessMemory',src)
  self.assertNotIn('VirtualAlloc',src)
  self.assertNotIn('registerFunction',src)
 def test_manifest(self):
  m=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text())
  self.assertIn('_engine/phase219_observations.py',m)
  self.assertIn('RUN_PHASE219.py',m)
  for name,digest in m.items():
   self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),digest,name)
   self.assertNotIn('UPLOAD_',name)
   self.assertNotIn('build_',name)
   self.assertFalse(name.lower().endswith(('.iso','.elf','.exe','.bin','.rar')))
if __name__=='__main__':unittest.main()
