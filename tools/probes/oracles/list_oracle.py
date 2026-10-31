#!/usr/bin/env python3
"""Independent word-driven oracle for 4 extra retail instructions on taken bnel.
Full SHA-256 of pinned 144-word window rejects altered or synthetic prior reports.
A verified owned-ELF snapshot does not need the unrelated stage registry.
"""
from __future__ import annotations
import hashlib,json,struct,subprocess
from pathlib import Path
from .handler_cfg import PIN,ENTRY,hx
from .retail_elf import decode_mips
from .branch_oracle import CASES as BASE_CASES,expected as previous_expected
from .entry_oracle import hex64,s32
WINDOW_SHA='84b4f0ac32f269c80364bae831a68fc90437fde90d6a58c2409b51f178133ba4'
FOUR_SHA='70e260c111a772e9c5f68fd8cde70ceb20312aedfaf11d3ba81cf776f6dccd13'
CASES=[c+(0x1111222233334444,0x5555666677778888,0x210040) for c in BASE_CASES]
CASES += [
 ('list_carry',0x200080,0xabcdef,0,1,2,3,0xffffffff,0x210040,0x55aa,0xffffffffffffffff,1,2,0x80000000),
 ('list_zero',0x200100,0x111,2,3,4,5,0xffffffff,0x210040,0,0,0,0,0),
]
FIELDS=['case','status','next_pc','sp','v0','v1','a2','a0','a3','s2',
 'delay_executed','branch_likely_taken','mmio_read','list_head_read',
 'saved_ra','saved_s2','saved_s1','saved_s0','stack_checksum']
def verified_rows(report, *, allow_stage_absent=False):
 if not isinstance(report,dict) or report.get('status')!='PINNED_RETAIL_HANDLER_BYTES_AVAILABLE_NOT_RECOMPILED' or report.get('retail_elf_sha256')!=PIN or report.get('expected_retail_elf_sha256')!=PIN or report.get('target_pc')!=hx(ENTRY):
  raise ValueError('unverified retail executable identity')
 if report.get('stage_registry_audit')=='PASS':
  if report.get('stage_registry_sha256')!='710d6b5f01af29285c8830f3cb021ee4e072fda2baad28bdf261ddad08b1ef8a' or report.get('handler_registered') is not False:raise ValueError('stage registry mismatch')
 elif not allow_stage_absent or 'stage_registry_audit' in report:raise ValueError('no trusted pinned registry attestation')
 data=report.get('rows')
 if not isinstance(data,list) or len(data)!=144:raise ValueError('wrong retail window length')
 rows={}
 for row in data:
  pc=int(row['pc'],16);w=int(row['word'],16)
  if pc in rows or pc&3 or not 0<=w<=0xffffffff:raise ValueError('bad retail row')
  decode=decode_mips(pc,w)
  if any(row.get(k)!=decode.get(k) for k in ('kind','target','has_delay_slot')):raise ValueError('control metadata mismatch')
  rows[pc]=w
 if hashlib.sha256(b''.join(struct.pack('<I',rows[x]) for x in sorted(rows))).hexdigest()!=WINDOW_SHA:raise ValueError('window digest mismatch')
 four=[rows.get(x) for x in range(0x1137dc,0x1137ec,4)]
 if any(v is None for v in four) or hashlib.sha256(b''.join(struct.pack('<I',w) for w in four)).hexdigest()!=FOUR_SHA:raise ValueError('taken path word digest mismatch')
 return rows

def expected(case,rows=None):
 prior=previous_expected(case[:11],rows)
 out=dict(prior)
 out['a0']=hex64(case[6]);out['list_head_read']=False
 if int(prior['next_pc'],16)!=0x1137dc:return out
 if rows is None:
  out['v1']=hex64(case[11]);out['a0']=hex64(case[12]);out['v0']=hex64((case[10]+case[11]) & ((1<<64)-1));out['a2']=hex64(s32(case[13]))
 else:
  r=[0]*32;r[2]=int(prior['v0'],16);r[3]=int(prior['v1'],16);r[4]=case[6];r[6]=int(prior['a2'],16)
  node=case[8]&0xffffffff
  mem={}
  for off,width,val in [(16,8,case[11]),(24,8,case[12]),(0,4,case[13])]:
   for j in range(width):mem[node+off+j]=(val>>(8*j))&255
  for pc in range(0x1137dc,0x1137ec,4):
   w=rows[pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;imm=w&65535
   if imm>=32768:imm-=65536
   if op==0x37 or op==0x23:
    width=8 if op==0x37 else 4;addr=(r[rs]+imm)&0xffffffff
    if addr%width or any(addr+j not in mem for j in range(width)):raise ValueError('unmapped/unaligned interpreter node read')
    val=sum(mem[addr+j]<<(8*j) for j in range(width))
    if rt:r[rt]=val if width==8 else s32(val)
   elif op==0 and (w&63)==0x2d:
    if rd:r[rd]=(r[rs]+r[rt])&((1<<64)-1)
   else:raise ValueError('unexpected original node opcode')
   r[0]=0
  out['v1']=hex64(r[3]);out['a0']=hex64(r[4]);out['v0']=hex64(r[2]);out['a2']=hex64(r[6])
 out['list_head_read']=True;out['next_pc']=hex64(0x1137ec)
 return out

def run_suite(exe,report,output,*,offline=False,allow_stage_absent=False):
 result={'component':'list_branch','status':'STARTED','scope':'ENTRY_PLUS_BNEL_AND_FOUR_TAKEN_NODE_WORDS',
         'evidence_type':'synthetic_model' if offline else 'pinned_retail_opcode_oracle',
         'function_registered':False,'game_booted':False,'original_handler_complete':False,'cases':[]}
 try:
  rows=None if offline else verified_rows(report,allow_stage_absent=allow_stage_absent)
  for c in CASES:
   p=subprocess.run([str(exe),c[0],*[hex(x) for x in c[1:]]],capture_output=True,text=True,timeout=10)
   if p.returncode or 'LIST_BRANCH_BRANCH ' not in p.stdout:raise ValueError(f'{c[0]}: process rc={p.returncode} {p.stderr[:180]}')
   obs=json.loads(p.stdout.split('LIST_BRANCH_BRANCH ',1)[1].splitlines()[0]);want=expected(c,rows)
   diffs={k:{'expected':want[k],'actual':obs.get(k)} for k in FIELDS if want[k]!=obs.get(k)}
   result['cases'].append({'case':c[0],'pass':not diffs,'next_pc':obs.get('next_pc'),'mismatches':diffs})
  result['all_cases_pass']=all(x['pass'] for x in result['cases'])
  result['status']=('LIST_BRANCH_SYNTHETIC_PASS_NOT_RETAIL' if offline else 'LIST_BRANCH_PINNED_RETAIL_DIFFERENTIAL_PASS_NOT_FULL_HANDLER') if result['all_cases_pass'] else 'LIST_BRANCH_DIFFERENTIAL_FAILED'
 except Exception as e:result.update(status='LIST_BRANCH_ORACLE_OR_NATIVE_REJECTED',error=str(e)[:500],all_cases_pass=False)
 output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n')
 return 0 if result['all_cases_pass'] else 2
