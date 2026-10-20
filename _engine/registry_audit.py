#!/usr/bin/env python3
"""Read-only stage/dispatch coverage audit. Never patches a guest registry."""
from __future__ import annotations
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent
BASE=0x001097D0; END=0x00123C1C; SLOTS=(END-BASE)//4
ROW=re.compile(r'g_ps2RecompiledFunctionTable\[(\d+)\]\s*=\s*&(\w+);\s*//\s*0x([0-9A-Fa-f]+)')
REQUIRED={
 0x001136C4:'B2_UPSTREAM_Timer2Init_0x113660',
 0x00113C60:'B2_CPU_Timer2Timestamp_0x113C60',
 0x001136BC:'B2_UPSTREAM_Timer2Init_0x113660',
}
CHECKPOINTS={
 0x001136C4:'Timer2 JAL call-site in translated initializer',
 0x00113770:'Timer2 event handler entry (unregistered boot blocker)',
 0x001136CC:'return continuation (may be internal to a registered function)',
 0x00113C60:'Timer2 timestamp helper',
}
def audit(text:str,previous_status:dict|None=None)->dict:
 errors=[];rows=ROW.findall(text)
 mappings={};occupied={}
 for index,name,pctext in rows:
  pc=int(pctext,16);slot=int(index)
  if pc in mappings:errors.append('DUPLICATE_GUEST_PC_0x%08X'%pc)
  if slot in occupied:errors.append('DUPLICATE_SLOT_%d'%slot)
  if not BASE<=pc<END or (pc-BASE)%4 or slot!=(pc-BASE)//4:
   errors.append('MISMATCHED_INDEX_PC_%d_0x%08X'%(slot,pc))
  mappings[pc]=name;occupied[slot]=pc
 for declaration in (f'g_ps2RecompiledFunctionTableBase = 0x{BASE:08X}u',
   f'g_ps2RecompiledFunctionTableEnd = 0x{END:08X}u',
   f'g_ps2RecompiledFunctionTableSlotCount = {SLOTS}u',
   f'g_ps2RecompiledFunctionTable[{SLOTS}u] = {{}}'):
  if declaration not in text:errors.append('REGISTRY_DECLARATION_MISMATCH_'+declaration.split('=')[0].strip())
 if len(mappings)!=65:errors.append('EXPECTED_65_STAGED_BINDINGS_GOT_%d'%len(mappings))
 for pc,expected in REQUIRED.items():
  if mappings.get(pc)!=expected:errors.append('MISSING_REQUIRED_BINDING_0x%08X'%pc)
 if 0x00113770 in mappings:
  errors.append('UNREVIEWED_TIMER2_HANDLER_ALREADY_REGISTERED')
 checkpoints=[{'pc':'0x%08X'%pc,'description':desc,'registered':pc in mappings,
               'bound_function':mappings.get(pc),'is_known_blocker':pc==0x00113770}
              for pc,desc in CHECKPOINTS.items()]
 return {'phase':171,'result':'PASS' if not errors else 'FAIL',
  'audit_kind':'read_only_generated_stage', 'table_base':'0x%08X'%BASE,
  'table_end_exclusive':'0x%08X'%END,'slot_count':SLOTS,'bound_entry_count':len(mappings),
  'null_slot_count':SLOTS-len(occupied),
  'timer2_entry_registered':0x00113770 in mappings,
  'next_boot_blocker':'0x00113770: retail handler is not safely registered',
  'timer2_shadow_probe_is_not_retail_dispatch':True,
  'checkpoint_coverage':checkpoints,
  'previous_windows_phase170_pass':previous_status.get('status')=='PHASE170_MSVC_REPAIRED_COMPOSED_PASS' if previous_status else False,
  'game_executable_built':False,'game_booted':False,'errors':errors}

def main():
 a=argparse.ArgumentParser()
 a.add_argument('--stage',default=str(ROOT/'stage/register_functions_phase123.cpp'))
 a.add_argument('--output',default=str(ROOT/'phase171_reports'))
 a.add_argument('--phase170-status',default=str(ROOT/'phase170_reports/PHASE170_WINDOWS_STATUS.json'))
 opts=a.parse_args();out=Path(opts.output);out.mkdir(parents=True,exist_ok=True)
 p=Path(opts.stage)
 try:
  body=p.read_text(encoding='utf8')
 except OSError as exc:
  result={'phase':171,'result':'UNAVAILABLE','reason':f'STAGED_REGISTRY_NOT_AVAILABLE: {exc}',
          'actual_stage_audited':False,'game_executable_built':False,'game_booted':False}
  rc=3
 else:
  try:previous=json.loads(Path(opts.phase170_status).read_text(encoding='utf8'))
  except (OSError,ValueError):previous={}
  result=audit(body,previous)
  result['actual_stage_audited']=True
  result['stage_file_sha256']=hashlib.sha256(body.encode('utf8')).hexdigest()
  result['phase170_result_from_reports']=previous.get('status','NOT_AVAILABLE')
  rc=0 if result['result']=='PASS' else 2
 (out/'PHASE171_RETAIL_DISPATCH_GAP_AUDIT.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 with (out/'PHASE171_NEXT_BOOT_BLOCKERS.txt').open('w',encoding='utf8') as f:
  f.write('PHASE171 READ-ONLY DISPATCH REPORT\n'+
          '1. Phase170 compiled and exercised bounded Timer2 paths in a standalone executable.\n'+
          '2. Phase171 shadow dispatch uses a ONE-SLOT registry, not the game registry.\n'+
          '3. Generated game registry at 0x00113770 is intentionally NOT registered.\n'+
          '4. Complete IRQ behavior, full Timer2 guest semantics, startup chain, rendering/audio/input and a game executable remain unproven.\n'+
          '5. Next implementation must validate complete real handler and return/IRQ semantics BEFORE retail binding.\n'+
          'RESULT: '+result['result']+'\n')
 print('PHASE171_REGISTRY_AUDIT_'+result['result'])
 return rc
if __name__=='__main__':raise SystemExit(main())
