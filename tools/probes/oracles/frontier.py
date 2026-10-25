#!/usr/bin/env python3
"""Read-only data-driven EE recompile frontier from user's staged retail generated code.
No executable synthesis, ROM extraction, game distribution or unverified function binding.
"""
from __future__ import annotations
import argparse,collections,hashlib,json,re
from pathlib import Path
from .registry_audit import audit,ROW,BASE,END,SLOTS
JAL_PATTERN=re.compile(r'\bjal\s+(?:0x)?([0-9a-fA-F]{6,8})\b',re.I)
CALLER_PATTERN=re.compile(r'\b(?:0x)?([0-9a-fA-F]{6,8})\s*:\s*0x[0-9a-fA-F]{8}\s+jal\b',re.I)
DEFAULT_CAP=500

def frontier(stage:Path,source_root:Path|None=None,limit:int=DEFAULT_CAP)->dict:
 result={'component':'frontier','kind':'read_only_recompilation_frontier','status':'INPUT_MISSING',
   'game_booted':False,'game_playable':False,'real_native_game_executable_built':False,
   'new_retail_function_compiled_by_frontier':False,'guest_interrupt_handler_retail_complete':False,
   'executable_function_count_known':False,'unbound_slots_are_not_function_counts':True,
   'source_scan_completed':False,'issues':[]}
 if not stage.is_file():
  result['issues'].append('STAGED_RETAIL_FUNCTION_REGISTRY_MISSING')
  return result
 data=stage.read_bytes();txt=data.decode('utf8','replace')
 d=audit(txt);result['stage_audit_result']=d['result'];result['stage_sha256']=hashlib.sha256(data).hexdigest()
 result['table_start']=d['table_base'];result['table_end_exclusive']=d['table_end_exclusive']
 result['table_slot_count']=SLOTS;result['registered_addresses']=sorted('0x%08X'%int(pc,16) for _,_,pc in ROW.findall(txt))
 result['original_registry_binding_count']=len(result['registered_addresses'])
 result['unbound_table_slots']=SLOTS-len(result['registered_addresses'])
 result['binding_coverage_fraction_of_dispatch_slots']=round(len(result['registered_addresses'])/SLOTS,8)
 result['source_audit_errors']=d['errors']
 result['unregistered_known_handler']='0x00113770' not in result['registered_addresses']
 if d['result']!='PASS':
  result['issues'].append('STAGED_REGISTRY_DID_NOT_MATCH_KNOWN_65_BINDING_BASELINE')
  result['status']='BASELINE_MISMATCH';return result
 source_root=(source_root or stage.parent).resolve()
 # Scope is deliberately constrained to the actual staged files; never scan user's full disk.
 candidates=[];inspected=[];seen=set();counts=collections.Counter()
 for p in sorted(source_root.rglob('*.cpp')):
  if len(inspected)>=limit:break
  if not p.is_file() or p.stat().st_size>3_000_000:continue
  # Reject sources which are unrelated to the locally staged translation.
  if not (p.name.startswith('B2_') or 'recompiled' in p.name.lower()):continue
  try:body=p.read_text(encoding='utf8',errors='replace')
  except OSError:continue
  inspected.append(p.relative_to(source_root).as_posix())
  for line in body.splitlines():
   if 'jal' not in line.lower() or '// ' not in line:continue
   m=JAL_PATTERN.search(line)
   if not m:continue
   t=int(m.group(1),16)
   if not BASE<=t<END or (t-BASE)%4:continue
   counts[t]+=1
   if t in seen:continue
   seen.add(t)
   candidates.append({'address':'0x%08X'%t,'registered':f'0x{t:08X}' in result['registered_addresses'],
     'source_file':p.relative_to(source_root).as_posix(),
     'candidate_only':True,'function_start_verified':False,
     'original_guest_instructions_recompiled':False})
 candidates.sort(key=lambda x:(x['registered'], -counts[int(x['address'],16)], x['address']))
 for c in candidates:c['observed_callsite_mentions']=counts[int(c['address'],16)]
 result['source_scan_completed']=True
 result['scan_file_count']=len(inspected)
 result['scan_basenames']=inspected[:100] # Do not include absolute user paths
 result['observed_direct_jal_candidate_count']=len(candidates)
 result['unregistered_call_candidates']=[c for c in candidates if not c['registered']][:100]
 result['registered_call_candidates']=[c for c in candidates if c['registered']][:40]
 result['next_priority']=[]
 result['next_priority'].append({'pc':'0x00113770',
   'reason':'Timer2 retail handler entry absent; isolated test callback is not original handler',
   'can_be_bound_safely_now':False,'needs_full_retail_instruction_translation_and_tests':True})
 for c in result['unregistered_call_candidates'][:10]:
  if c['address']!='0x00113770':result['next_priority'].append(dict(c))
 result['required_followup_evidence']=['Retail ELF disassembly and function boundaries, derived locally from the user-owned ISO',
  'Call graph / landing address checks for candidates',
  'Original MIPS register and delay-slot equivalence tests',
  'Native game startup, graphics, audio, input and full gameplay tests']
 result['status']='STAGED_FRONTIER_INVENTORIED_NO_NEW_GAME_TRANSLATION'
 return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--stage',default='stage/register_functions_registry_snapshot.cpp')
 p.add_argument('--source-root',default='');p.add_argument('--output',default='frontier_reports/FRONTIER_RECOMPILATION_FRONTIER.json')
 a=p.parse_args(); stage=Path(a.stage)
 report=frontier(stage,Path(a.source_root) if a.source_root else None)
 out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
 out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
 print('FRONTIER_RECOMPILATION_FRONTIER',report['status'],report.get('original_registry_binding_count','none'))
 return 0 if report['status'].startswith('STAGED_FRONTIER') else 2
if __name__=='__main__':raise SystemExit(main())
