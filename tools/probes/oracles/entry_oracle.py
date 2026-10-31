#!/usr/bin/env python3
"""Differential verification of a native *prefix* against a separate MIPS word oracle.

Only a fresh exact-SHA retail RetailAudit report may qualify as retail verification.
This oracle does NOT execute the game's original handler, call other game routines,
or assert anything about the remainder of the handler.
"""
from __future__ import annotations
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
from .handler_cfg import PIN,ENTRY,hx,survey
MASK=(1<<64)-1
BASE=0x0033B288
PREFIX_LAST=0x00113798
PREFIX_SHA='64981e6e4c5a81ee14b058bb2bf1e6b493a6783c7cd9df686f3f22683c86690b'
FNV_OFFSET=14695981039346656037
FNV_PRIME=1099511628211
CASES=[
 ('negative',0x00200080,0x1234567887654321,0x1111000022220000,0x3333000044440000,0x5555000066660000,0x7777000088880000,0xffffffff,0x80000001),
 ('zero',0x00200100,0x1234abcd5432dcba,0xfeeddead04030201,0xdefeccabfeedface,0x0ff00ff00ff00ff0,0x0102030405060708,0x00000000,0x7fffffff),
 ('positive',0x00200180,0xfedcba9876543210,0x8877665544332211,0xffeeddccbbaa9988,0x0101010101010101,0x7fffffffffffffff,1,0xffffffff),
 ('min_signed',0x002000c0,0x0,0xffffffffffffffff,0x0,0x4444,0xeeeeeeeeeeeeeeee,0x80000000,0x80000000),
 ('max_signed',0x00200140,0xabcdef0123456789,0x42,0x1010,0x4545,0,0x7fffffff,0),
 ('negative_two',0x00200080,0x8899aabbccddeeff,0x123,0x456,0x789,0xabcdefabcdefabcd,0xfffffffe,0x00000010),
 ('signextend_sp',0xffffffff00200140,0x123456789abcdef0,0xabcd,0xabcdeffedcba,0x0123456789abcdef,0xabcdfefeefeeabcd,0x80000001,0x00000080),
 ('signextend_lw',0x002000c0,0x2222222222222222,0x3333333333333333,0x4444444444444444,0x5555555555555555,0xffffffffffffffff,0x00000000,0xffffffff),
]
def hex64(x):return f'0x{x&MASK:016X}'
def s32(v):v&=0xffffffff;return v|0xffffffff00000000 if v&0x80000000 else v
class Mem:
 def __init__(self):self.bytes={}
 def read(self,addr,n):
  if addr&(n-1):raise ValueError('unaligned read')
  return sum(self.get(addr+i)<<(i*8) for i in range(n))
 def get(self,addr):
  if not (0x001ffe00<=addr<0x00200300 or BASE<=addr<BASE+0x80):raise ValueError('unmapped guest read')
  return self.bytes.get(addr,0xa5 if 0x001ffe00<=addr<0x00200300 else 0)
 def write(self,addr,val,n):
  if addr&(n-1):raise ValueError('unaligned write')
  for i in range(n):
   if not (0x001ffe00<=addr+i<0x00200300 or BASE<=addr+i<BASE+0x80):raise ValueError('unmapped guest write')
   self.bytes[addr+i]=(val>>(i*8))&0xff
 def stack_hash(self,addr):
  v=FNV_OFFSET
  for i in range(128):v=((v^self.get(addr+i))*FNV_PRIME)&MASK
  return v

def extract_verified_words(report):
 if (report.get('status')!='PINNED_RETAIL_HANDLER_BYTES_AVAILABLE_NOT_RECOMPILED' or
     report.get('retail_elf_sha256')!=PIN or report.get('expected_retail_elf_sha256')!=PIN or
     report.get('target_pc')!=hx(ENTRY) or report.get('handler_registered') is not False or
     report.get('stage_registry_audit')!='PASS'):
  raise ValueError('retail identity or stage registry mismatch')
 cfg=survey(report)
 if cfg.get('status')!='PINNED_ENTRY_CFG_SURVEY_COMPLETE_NOT_NATIVE_TRANSLATION' or cfg.get('reachable_word_count')!=100:
  raise ValueError('retail source window failed cfg validation')
 rows={int(r['pc'],16):int(r['word'],16) for r in report['rows']}
 positions=list(range(ENTRY,PREFIX_LAST+1,4))
 if not all(x in rows for x in positions):raise ValueError('missing entry prefix words')
 words=[rows[x] for x in positions]
 if hashlib.sha256(b''.join(struct.pack('<I',w) for w in words)).hexdigest()!=PREFIX_SHA:
  raise ValueError('entry word sequence mismatches approved SHA')
 return rows

def oracle_words(rows,case):
 name,sp,ra,s0,s1,s2,a0,global_sign,global_delay=case
 r=[0]*32;r[29]=sp;r[31]=ra;r[16]=s0;r[17]=s1;r[18]=s2;r[4]=a0
 r[2]=0xdeadbeef;r[3]=0xdeaddead;r[6]=0x7777777777777777;r[7]=0x8888888888888888
 m=Mem();m.write(BASE+28,global_sign,4);m.write(BASE+24,global_delay,4)
 pc=ENTRY;count=0
 while pc<=PREFIX_LAST:
  w=rows[pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;imm=w&0xffff
  signimm=imm-0x10000 if imm&0x8000 else imm
  next_pc=pc+4
  if op==0x09:
   if rt:r[rt]=s32(r[rs]+signimm)
  elif op==0x0f:
   if rt:r[rt]=s32(imm<<16)
  elif op==0x3f:
   m.write((r[rs]+signimm)&0xffffffff,r[rt],8)
  elif op==0x23:
   if rt:r[rt]=s32(m.read((r[rs]+signimm)&0xffffffff,4))
  elif op==0 and (w&0x3f)==0x2d:
   if rd:r[rd]=(r[rs]+r[rt])&MASK
  elif op==1 and rt==1:
   target=(pc+4+(signimm<<2))&0xffffffff
   taken=(r[rs]>>63)==0
   next_pc=target if taken else pc+8
   slot=rows.get(pc+4)
   if slot is None or slot>>26!=0x23:raise ValueError('expected lw delay slot missing')
   d_rt=(slot>>16)&31;d_rs=(slot>>21)&31;d_imm=slot&0xffff
   if d_imm&0x8000:d_imm-=0x10000
   if d_rt:r[d_rt]=s32(m.read((r[d_rs]+d_imm)&0xffffffff,4))
   pc=next_pc
   count+=2
   break
  else:raise ValueError(f'unsupported original opcode {op} at {pc:#x}')
  r[0]=0;pc=next_pc;count+=1
 if count!=11 or pc not in (0x001138e8,0x0011379c):raise ValueError('prefix did not reach expected successor')
 stack=r[29]&0xffffffff
 return {'case':name,'status':'TRANSLATED_PREFIX_RETURNED','next_pc':hex64(pc),'sp':hex64(r[29]),
  'v0':hex64(r[2]),'v1':hex64(r[3]),'a2':hex64(r[6]),'a3':hex64(r[7]),
  'saved_ra':hex64(m.read(stack+48,8)),'saved_s2':hex64(m.read(stack+32,8)),
  'saved_s1':hex64(m.read(stack+16,8)),'saved_s0':hex64(m.read(stack,8)),
  'stack_checksum':hex64(m.stack_hash(stack))}

def standalone_oracle(case):
 """Offline local smoke model only, NOT independently grounded in retail opcode words."""
 n,sp,ra,s0,s1,s2,a0,sign,delay=case
 m=Mem();m.write(BASE+28,sign,4);m.write(BASE+24,delay,4)
 sp=s32(sp-64);m.write((sp&0xffffffff)+48,ra,8);m.write((sp&0xffffffff)+32,s2,8)
 m.write((sp&0xffffffff)+16,s1,8);m.write(sp&0xffffffff,s0,8)
 return {'case':n,'status':'TRANSLATED_PREFIX_RETURNED','next_pc':hex64(0x1138e8 if sign<0x80000000 else 0x11379c),
  'sp':hex64(sp),'v0':hex64(BASE),'v1':hex64(s32(sign)),'a2':hex64(s32(delay)),'a3':hex64(a0),
  'saved_ra':hex64(m.read((sp&0xffffffff)+48,8)),'saved_s2':hex64(m.read((sp&0xffffffff)+32,8)),
  'saved_s1':hex64(m.read((sp&0xffffffff)+16,8)),'saved_s0':hex64(m.read(sp&0xffffffff,8)),
  'stack_checksum':hex64(m.stack_hash(sp&0xffffffff))}

def run_suite(exe:Path,source:Path|None,output:Path,offline=False):
 result={'component':'entry_prefix','status':'STARTED','scope':'FIRST_ELEVEN_ENTRY_INSTRUCTIONS_ONLY',
  'entry_pc':'0x00113770','last_included_pc':'0x00113798',
  'first_untranslated_pc':'0x0011379C','original_handler_registered':False,
  'full_handler_compiled':False,'guest_handler_executed':False,'function_boundary_verified':False,
  'guest_upper_64_bit_register_halves_verified':False,'native_runtime_integration':False,
  'source_kind':'synthetic_semantic_model' if offline else 'retail_exact_sha_mips_word_oracle',
  'test_cases':[]}
 try:
  if not offline:
   if source is None or not source.is_file():raise ValueError('missing verified RetailAudit report')
   rows=extract_verified_words(json.loads(source.read_text(encoding='utf-8-sig')))
  else:rows={}
  for case in CASES:
   argv=[str(exe),case[0]]+[hex(x) for x in case[1:]]
   child=subprocess.run(argv,text=True,capture_output=True,timeout=7)
   if child.returncode or 'ENTRY_PREFIX_ENTRY ' not in child.stdout:raise ValueError(f'{case[0]} child returned {child.returncode}: {child.stderr[:300]}')
   got=json.loads(child.stdout.split('ENTRY_PREFIX_ENTRY ',1)[1].splitlines()[0])
   want=standalone_oracle(case) if offline else oracle_words(rows,case)
   matches=got==want
   result['test_cases'].append({'case':case[0],'pass':matches,'expected_next_pc':want['next_pc'],
     'observed_next_pc':got.get('next_pc'),'mismatches':{k:{'expected':v,'actual':got.get(k)} for k,v in want.items() if got.get(k)!=v}})
  result['all_cases_pass']=all(r['pass'] for r in result['test_cases'])
  result['status']=('ENTRY_PREFIX_OFFLINE_SYNTHETIC_SEMANTICS_PASS_NOT_RETAIL' if offline else 'ENTRY_PREFIX_RETAIL_PINNED_ENTRY_PREFIX_EQUIVALENCE_PASS_NOT_FULL_HANDLER') if result['all_cases_pass'] else 'ENTRY_PREFIX_DIFFERENTIAL_MISMATCH'
 except Exception as exc:
  result['status']='ENTRY_PREFIX_REJECTED_OR_NATIVE_FAILED';result['error']=str(exc)[:500];result['all_cases_pass']=False
 output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 return 0 if result['all_cases_pass'] else 2

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--exe',required=True);p.add_argument('--retail_audit-report');p.add_argument('--output',required=True);p.add_argument('--offline-only',action='store_true');a=p.parse_args()
 code=run_suite(Path(a.exe),Path(a.retail_audit_report) if a.retail_audit_report else None,Path(a.output),offline=a.offline_only)
 print('ENTRY_PREFIX_ENTRY_EQUIVALENCE',code);raise SystemExit(code)
