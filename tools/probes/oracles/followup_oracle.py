#!/usr/bin/env python3
"""Followup separate word-decoding oracle for the original BEQ/DSUBU block.
All verified original instruction words remain in memory, never in the package/reports.
"""
from __future__ import annotations
import hashlib, json, struct, subprocess
from . import list_oracle
from .entry_oracle import hex64

MASK=(1<<64)-1
PC_SEQ=(0x1137ec,0x1137f0,0x113854,0x113858,0x11385c)
FOLLOWUP_SHA='31e23718a94ce2ba8dbd707e7b403053414d8d3ecc361ae990afcafa9a5aa517'
CASES=list(list_oracle.CASES)+[
 ('list_zero_mmio_lowbits',0x200080,0x1,0x2,0x3,0x4,0x5,0xffffffff,0x210040,0x80000003,0x1020304050607080,0x345,0xffff000000000000,0),
 ('list_zero_negative_diff',0x200100,0x11,0xffffffffffffffff,0x0,0x0,0x23,0xffffffff,0x210040,0x00000002,0x0,0x8000000000000000,0x1111111111111111,0),
 ('list_nonzero_signextended',0x2000c0,0x11,0xaa,0xbb,0xcc,0xdd,0xffffffff,0x210040,0x3,0xa,0xb,0xc,0xffffffff),
]
FIELDS=list_oracle.FIELDS+['s0','s1','followup_branch_taken','followup_delay_executed','zero_exit_tested']

def verified_rows(report,*,allow_stage_absent=False):
 rows=list_oracle.verified_rows(report,allow_stage_absent=allow_stage_absent)
 word_bytes=b''.join(struct.pack('<I',rows[p]) for p in PC_SEQ)
 if hashlib.sha256(word_bytes).hexdigest()!=FOLLOWUP_SHA:
  raise ValueError('retail followup word SHA mismatch')
 a,b,c,d,e=(rows[p] for p in PC_SEQ)
 def reg(w,shift):return (w>>shift)&31
 if (a>>26,reg(a,21),reg(a,16))!=(4,6,0):raise ValueError('BEQ signature mismatch')
 imm=a&65535
 if imm&32768:imm-=65536
 if 0x1137ec+4+(imm<<2)!=0x113854:raise ValueError('BEQ target mismatch')
 if (b>>26,b&63,reg(b,21),reg(b,16),reg(b,11))!=(0,0x2f,2,4,16):raise ValueError('DSUBU slot mismatch')
 if (c>>26,reg(c,21),reg(c,16),c&65535)!=(0x0c,18,17,3):raise ValueError('ANDI exit mismatch')
 if (d>>26,d&63,reg(d,21),reg(d,16),reg(d,11))!=(0,0x2f,16,7,2):raise ValueError('exit DSUBU mismatch')
 if (e>>26,reg(e,21),reg(e,16),e&65535)!=(0x0a,2,2,0x7333):raise ValueError('exit SLTI mismatch')
 return rows

def expected(case,rows=None):
 old=list_oracle.expected(case,rows)
 out=dict(old)
 out.update(s0=hex64(case[3]),s1=hex64(case[4]),
            followup_branch_taken=False,followup_delay_executed=False,
            zero_exit_tested=False)
 if int(old['next_pc'],16)!=0x1137ec:return out
 r=[0]*32
 r[2]=int(old['v0'],16);r[4]=int(old['a0'],16)
 r[6]=int(old['a2'],16);r[7]=int(old['a3'],16)
 r[16]=case[3];r[17]=case[4];r[18]=int(old['s2'],16)
 if rows is None:
  beq_zero=r[6]==0
  r[16]=(r[2]-r[4])&MASK
 else:
  beq=rows[0x1137ec]
  beq_zero=r[(beq>>21)&31]==r[(beq>>16)&31]
  slot=rows[0x1137f0];rs=(slot>>21)&31;rt=(slot>>16)&31;rd=(slot>>11)&31
  if rd:r[rd]=(r[rs]-r[rt])&MASK
 out['followup_branch_taken']=beq_zero
 out['followup_delay_executed']=True
 out['s0']=hex64(r[16]);out['next_pc']=hex64(0x113854 if beq_zero else 0x1137f4)
 if not beq_zero:return out
 if rows is None:
  r[17]=r[18]&3
  r[2]=(r[16]-r[7])&MASK
  r[2]=int(bool(r[2]>>63 or r[2]<0x7333))
 else:
  for pc in (0x113854,0x113858,0x11385c):
   word=rows[pc];op=word>>26;rs=(word>>21)&31;rt=(word>>16)&31;rd=(word>>11)&31
   if op==0x0c:
    if rt:r[rt]=r[rs]&(word&65535)
   elif op==0 and (word&63)==0x2f:
    if rd:r[rd]=(r[rs]-r[rt])&MASK
   elif op==0x0a:
    imm=word&65535
    if imm&32768:imm-=65536
    u=r[rs]&MASK
    signed=u-(1<<64) if u>>63 else u
    if rt:r[rt]=int(signed<imm)
   else:raise ValueError('unrecognized original exit opcode')
   r[0]=0
 out.update(s1=hex64(r[17]),v0=hex64(r[2]),zero_exit_tested=True,next_pc=hex64(0x113860))
 return out

def run_suite(exe,report,output,*,offline=False,allow_stage_absent=False):
 result={'component':'followup','status':'STARTED',
         'scope':'RETAIL_HANDLER_BEQ_DS_DELAY_PLUS_ZERO_POINTER_EXIT',
         'evidence_type':'synthetic_semantic_model' if offline else 'pinned_usa_retail_opcode_interpreter',
         'retail_words_bundled':False,'original_handler_complete':False,
         'original_handler_registered':False,'game_booted':False,'cases':[]}
 try:
  rows=None if offline else verified_rows(report,allow_stage_absent=allow_stage_absent)
  for case in CASES:
   p=subprocess.run([str(exe),case[0],*[hex(x) for x in case[1:]]],capture_output=True,text=True,timeout=10)
   if p.returncode or 'FOLLOWUP_BRANCH ' not in p.stdout:
    raise RuntimeError(f'{case[0]}: native rc={p.returncode} stderr={p.stderr[:120]}')
   obs=json.loads(p.stdout.split('FOLLOWUP_BRANCH ',1)[1].splitlines()[0]);want=expected(case,rows)
   diff={k:{'expected':want[k],'actual':obs.get(k)} for k in FIELDS if want[k]!=obs.get(k)}
   result['cases'].append({'case':case[0],'pass':not diff,'next_pc':obs.get('next_pc'),'mismatches':diff})
  result['all_cases_pass']=all(c['pass'] for c in result['cases'])
  result['observed_followup_zero_path_cases']=sum(1 for c in result['cases'] if c['next_pc']==hex64(0x113860) and c['pass'])
  result['observed_followup_nonzero_path_cases']=sum(1 for c in result['cases'] if c['next_pc']==hex64(0x1137f4) and c['pass'])
  if not result['observed_followup_zero_path_cases'] or not result['observed_followup_nonzero_path_cases']:
   raise ValueError('new branch outcome not covered')
  result['status']=('FOLLOWUP_SYNTHETIC_PASS_NOT_RETAIL' if offline else 'FOLLOWUP_PINNED_RETAIL_DIFFERENTIAL_PASS_NOT_FULL_HANDLER') if result['all_cases_pass'] else 'FOLLOWUP_DIFFERENTIAL_MISMATCH'
 except Exception as e:
  result.update(status='FOLLOWUP_ORACLE_OR_NATIVE_REJECTED',error=str(e)[:400],all_cases_pass=False)
 output.parent.mkdir(parents=True,exist_ok=True)
 output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 return 0 if result.get('all_cases_pass') else 2
