"""Independent opcode-driven oracle for tail-wrapper -> shared helper chain.

Uses the original MIPS instruction decoders, NOT the native C++ model.
The EI transition snapshots are synthetic fixtures, not actual PS2 EI behavior.
"""
from __future__ import annotations
import subprocess
from .tail_wrappers import ENTRIES,interpreter as wrapper_oracle,synthetically_assembled,retail_instructions,MASK
from .mmio_eret import (oracle as shared_oracle,synthetic_words as shared_synthetic,
 checked_retail as shared_checked,STOP_ERET,STOP_EI_LIMIT)
from .virtual_cp0 import scenarios as cp0_scenarios

PORTS={0x113400:0xffffffffb0001010,0x113410:0xffffffffb0001020}

def synthetic_instructions():
 return {e:synthetically_assembled(e) for e in ENTRIES},shared_synthetic()

def pinned_retail(blob):
 """Pins SHA-256 of ELF, both wrapper windows and 64-word shared window."""
 return retail_instructions(blob),shared_checked(blob)

def scenarios():
 for entry in ENTRIES:
  for idx,base in enumerate(cp0_scenarios()):
   initial,e1,e2,v0,v1,a2,input_a0,input_a1,ra,sp=base
   for variant in (0,1):
    mix=((idx+1)*0x9e3779b97f4a7c15+variant*0xd1b54a32d192ed03)&MASK
    original_a0=(input_a0 ^ mix)&MASK
    original_a1=(input_a1 ^ (mix>>7))&MASK
    original_ra=(ra ^ (mix<<11))&MASK
    epc=(0x80001234 ^ (mix>>27))&0xffffffff
    old_eepc=(0xdeadbeef ^ (mix>>5))&0xffffffff
    yield(entry,initial,e1,e2,v0,v1,a2,original_a0,original_a1,
          original_ra,sp,epc,old_eepc)

def oracle(words,case):
 wrapper_words,shared_words=words
 (entry,init,e1,e2,v0,v1,a2,input_a0,input_a1,ra,sp,epc,old_eepc)=case
 wrapper=wrapper_oracle(entry,wrapper_words[entry],input_a0,input_a1,ra,v0)
 if wrapper[0]!=0x113380 or wrapper[3]!=ra:
  raise ValueError('Wrapper did not forward to shared code preserving RA')
 if wrapper[1]!=PORTS[entry] or wrapper[2]!=input_a0:
  raise ValueError('Wrapper violated exact port/address contract')
 shared_case=(init,e1,e2,wrapper[4],v1,a2,wrapper[1],wrapper[2],wrapper[3],sp,epc,old_eepc)
 shared=shared_oracle(shared_words,shared_case)
 if shared[17]==STOP_ERET:
  if shared[18]!=PORTS[entry] or shared[19]!=(input_a0 & 0xffffffff):
   raise ValueError('Virtual MMIO store did not preserve wrapper address/data')
 elif shared[17]!=STOP_EI_LIMIT:
  raise ValueError('Unexpected stop in valid wrapper chain')
 return (entry,*wrapper[:4],*shared)

def compare_native(exe,words):
 cases=list(scenarios())
 payload=''.join(' '.join([*(f'{v:08X}' for v in c[:4]),
        *(f'{v:016X}' for v in c[4:11]),
        *(f'{v:08X}' for v in c[11:])])+'\n' for c in cases)
 p=subprocess.run([str(exe)],input=payload,text=True,capture_output=True,timeout=90)
 if p.returncode:
  raise ValueError(f'Combined native probe rc={p.returncode}: {p.stderr[:200]}')
 lines=p.stdout.splitlines()
 if len(lines)!=len(cases):raise ValueError(f'Combined native output count {len(lines)} != {len(cases)}')
 errors=[];stop_counts={STOP_ERET:0,STOP_EI_LIMIT:0};ports={hex(v):0 for v in PORTS.values()}
 for idx,(c,line) in enumerate(zip(cases,lines)):
  try:got=tuple(int(s,16) for s in line.split())
  except ValueError as exc:raise ValueError('Non-hex native output line '+str(idx)) from exc
  if len(got)!=27:raise ValueError(f'Native case {idx} fields={len(got)} expected 27')
  expected=oracle(words,c)
  if expected != got:
   errors.append({'case':idx,'entry':hex(c[0]),'expected':[hex(x) for x in expected],
                  'got':[hex(x) for x in got]})
  stop_counts[expected[22]]+=1
  if expected[22]==STOP_ERET:ports[hex(expected[23])]+=1
 return {'case_count':len(cases),'passed':not errors,'failures':errors[:8],
         'wrapper_entries':[f'0x{e:08X}' for e in ENTRIES],
         'shared_target':'0x00113380','return_count':stop_counts[STOP_ERET],
         'ei_poll_limit_count':stop_counts[STOP_EI_LIMIT],
         'virtual_writes_by_port':ports,
         'retail_functions_registered':0,'game_playable':False,
         'EI_transitions':'synthetic fixture snapshots','host_mmio_writes':0}
