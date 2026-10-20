#!/usr/bin/env python3
"""Phase 226 fresh-install native loop probe + inherited virtual handler regressions."""
from __future__ import annotations
import argparse,hashlib,json,os,platform,shutil,subprocess,sys,zipfile
from pathlib import Path
# Explicit local imports also work under Python isolated (-I) mode.
sys.path.insert(0,str(Path(__file__).resolve().parent))
from _engine.retail_elf import load_retail_elf,search_elf,EXPECTED_SHA256,AuditError
from _engine.phase213_semantics import ENTRIES,synthetically_assembled,retail_instructions,compare_native as compare_wrappers
from _engine.phase214_semantics import synthesize_translated_words,compare_native as compare_shared,read_retail_window,WINDOW_DIGEST
from _engine.phase215_semantics import synthetic_words as cp0_words,checked_retail as cp0_retail,compare_native as compare_cp0
from _engine.phase216_semantics import synthetic_words as mmio_words,checked_retail as mmio_retail,compare_native as compare_mmio
from _engine.phase217_semantics import synthetic_instructions, pinned_retail, compare_native as compare_chain
from _engine.phase218_semantics import compare_native as compare_ee_model
from _engine.phase219_observations import native_cases,run_native as run_observations,verify_trace
from _engine.phase220_handoff import compare_native as compare_handoff
from _engine.handler_slice import handler_snapshot
from _engine.helper_boundary_oracle import run_suite as run_handler_suite, verified_rows as verified_handler_rows
from _engine.phase221_bridge import compare_native as compare_handler_bridge
from _engine.phase222_continuation import compare_native as compare_double_helper
from _engine.phase223_original_irq_chain import compare_native as compare_irq_original_chain
from _engine.phase224_list_walk import compare_native as compare_list_walk, checked_retail as checked_list_words, WIN_SHA as LIST_WINDOW_SHA
from _engine.phase225_list_irq_chain import compare_native as compare_list_irq_chain
from _engine.phase226_live_guest_memory import compare_native as compare_live_memory
ROOT=Path(__file__).resolve().parent
REPORT=ROOT/'phase226_reports';BUILD=ROOT/'build_phase226_standalone'
TARGETS={
 'handler':'budokai2_handler_probe',
 'handler_bridge':'budokai2_phase221_handler_helper_bridge',
 'double_helper':'budokai2_phase222_handler_double_helper',
 'original_irq_chain':'budokai2_phase223_virtual_irq_original_handler',
 'original_list_walk':'budokai2_phase224_original_list_walk',
 'list_irq_chain':'budokai2_phase225_original_list_irq_chain',
 'live_memory':'budokai2_phase226_live_guest_memory_chain',
 'handoff':'budokai2_phase220_irq11_handoff',
 'ee_model':'budokai2_phase218_interrupt_transition_chain',
 'observation':'budokai2_phase219_cp0_observation_replay',
 'chain':'budokai2_phase217_wrapper_shared_chain',
 'mmio':'budokai2_phase216_virtual_mmio_eret',
 'cp0':'budokai2_phase215_virtual_cp0',
 'shared':'budokai2_phase214_shared_entry',
 'wrapper':'budokai2_phase214_tail_wrappers',
}

def write(name,obj):
 REPORT.mkdir(exist_ok=True)
 (REPORT/name).write_text(json.dumps(obj,sort_keys=True,indent=2)+'\n',encoding='utf8')

def cmd(args,stem,seconds):
 REPORT.mkdir(exist_ok=True)
 with (REPORT/(stem+'.stdout.txt')).open('w',encoding='utf8') as o, (REPORT/(stem+'.stderr.txt')).open('w',encoding='utf8') as e:
  try:
   proc=subprocess.Popen(args,cwd=ROOT,stdout=o,stderr=e)
   try:return proc.wait(timeout=seconds)
   except subprocess.TimeoutExpired:
    if os.name=='nt':subprocess.run(['taskkill','/F','/T','/PID',str(proc.pid)],capture_output=True,timeout=15)
    else:proc.kill()
    proc.wait();e.write('PROCESS_TIMEOUT\n');return 124
  except OSError as exc:e.write(type(exc).__name__+': '+str(exc));return 127

def find_exe(target):
 for p in (BUILD/'Release'/(target+'.exe'),BUILD/'Debug'/(target+'.exe'),BUILD/(target+'.exe'),BUILD/target):
  if p.is_file():return p
 return None

def choose_game():
 if os.name!='nt':return None
 try:
  import tkinter as tk
  from tkinter import filedialog
  root=tk.Tk();root.withdraw();root.attributes('-topmost',True)
  path=filedialog.askopenfilename(title='Select your OWNED Budokai 2 USA ISO or extracted ELF',filetypes=[('Owned game','*.iso *.bin *.zip *.elf *.79'),('All files','*.*')]);root.destroy()
  return Path(path) if path else None
 except Exception:return None

def pack():
 with zipfile.ZipFile(ROOT/'UPLOAD_PHASE226_RESULTS.zip','w',zipfile.ZIP_DEFLATED) as z:
  for p in sorted(REPORT.iterdir()):
   if p.is_file() and p.suffix in ('.json','.txt','.log') and p.stat().st_size<2_000_000:
    z.write(p,'phase226/'+p.name)

def main():
 parser=argparse.ArgumentParser()
 parser.add_argument('--game',default='',help='Owned Budokai 2 USA ISO / extracted SLUS ELF / supported ZIP')
 parser.add_argument('--choose-game',action='store_true')
 parser.add_argument('--skip-retail',action='store_true')
 parser.add_argument('--trace',default='',help='Optional external instruction observations JSONL (not a generated test fixture)')
 args=parser.parse_args()
 if REPORT.exists():shutil.rmtree(REPORT)
 REPORT.mkdir()
 status={
  'phase':226,'status':'STARTED','platform':platform.system(),
  'fresh_folder_mode':True,'previous_phases_required':False,
  'staged_registry_required':False,'ps2runtime_required':False,
  'retail_executable_bundled':False,'virtual_mmio_only':True,'virtual_cp0_only':True,
  'virtual_eret_only':True,'host_privileged_instructions_executed':False,
  'host_hardware_registers_written':False,'original_game_function_bindings_added':0,
  'original_shared_helper_integrated':False,'full_game_recompiled':False,
  'game_booted':False,'game_playable':False,
  'guest_EI_transitions_fixture_supplied':True,
  'ee_ei_privilege_contract_enabled':True,
  'cp0_external_event_checkpoint_injected':True,
  'actual_ps2_irq_timing_verified':False,
  'external_trace_supplied':False,'external_trace_authenticity_verified':False,
  'real_guest_instruction_observation_verified':False,
  'real_guest_eret_behavior_verified':False,
  'pinned_retail_shared_window_sha256':WINDOW_DIGEST,
  'integrated_wrapper_to_shared':True,'guest_EI_behavior_verified':False,
  'original_handler_to_helper_bridge_is_virtual':True,'post_helper_native_continuations_translated':True,
  'second_helper_invocation_virtual_only':True,
  'virtual_irq_dispatch_executes_bounded_original_handler':True,'original_guest_irq11_handler_fully_executed':False,
  'virtual_irq11_handoff_only':True,'real_ps2_timer2_irq11_dispatch_verified':False,
  'original_guest_timer2_irq_handler_executed':False,
  'original_list_irq_helper_chain_integrated':True,'live_guest_byte_memory_used_for_list':True,'guest_memory_patch_after_prefix':True,'real_ps2_irq_schedule_verified':False,
 }
 try:
  manifest=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text(encoding='utf8'))
  errors=[n for n,d in manifest.items() if not (ROOT/n).is_file() or hashlib.sha256((ROOT/n).read_bytes()).hexdigest()!=d]
  status['source_integrity_errors']=errors
  if errors:status['status']='SOURCE_INTEGRITY_FAILED';return 2
  cmake=shutil.which('cmake')
  if not cmake:status['status']='CMAKE_NOT_FOUND';return 2
  status['cmake_configure_rc']=cmd([cmake,'-S',str(ROOT/'standalone'),'-B',str(BUILD)],'PHASE226_CMAKE_CONFIGURE',180)
  if status['cmake_configure_rc']:status['status']='CMAKE_CONFIGURE_FAILED';return 2
  cache=(BUILD/'CMakeCache.txt').read_text(errors='replace')
  status['generator_visual_studio']='CMAKE_GENERATOR:INTERNAL=Visual Studio' in cache
  status['msvc_compiler_verified']=bool(os.name=='nt' and status['generator_visual_studio'] and (BUILD/(TARGETS['handoff']+'.vcxproj')).is_file())
  status['native_build_rc']=cmd([cmake,'--build',str(BUILD),'--config','Release','--target',*TARGETS.values()],'PHASE226_NATIVE_BUILD',600)
  if status['native_build_rc']:status['status']='NATIVE_BUILD_FAILED';return 2
  exe={k:find_exe(v) for k,v in TARGETS.items()}
  if not all(exe.values()):status['status']='NATIVE_EXECUTABLE_MISSING';return 2
  list_result=compare_list_walk(exe['original_list_walk'])
  write('PHASE226_ORIGINAL_LIST_WALK_SYNTHETIC.json',list_result)
  status['original_list_walk_cases']=list_result['case_count']
  status['original_list_walk_pass']=list_result['passed']
  status['original_list_walk_outcomes']=list_result['outcomes']
  status['original_list_walk_window_sha256']=LIST_WINDOW_SHA
  status['original_list_walk_standalone_only']=True
  if not list_result['passed']:
   status['status']='PHASE226_LIST_WALK_SYNTHETIC_DIFFERENCE';return 2
  live=compare_live_memory(exe['live_memory'])
  write('PHASE226_LIVE_GUEST_MEMORY_SYNTHETIC.json',live)
  status['live_guest_memory_cases']=live['case_count']
  status['live_guest_memory_synthetic_pass']=live['passed']
  status['live_guest_memory_changes_observed']=live['observable_changed_cases']
  if not live['passed']:
   status['status']='PHASE226_LIVE_GUEST_MEMORY_SYNTHETIC_DIFFERENCE';return 2
  new_chain=compare_list_irq_chain(exe['list_irq_chain'])
  write('PHASE226_ORIGINAL_LIST_IRQ_HELPER_SYNTHETIC.json',new_chain)
  status['original_list_irq_chain_cases']=new_chain['case_count']
  status['original_list_irq_chain_synthetic_pass']=new_chain['passed']
  status['original_list_irq_chain_outcomes']={k:new_chain[k] for k in (
    'virtual_dispatch_suppressed','original_list_walk_executed','bounded_list_stops',
    'first_helper_call_400','first_helper_call_410','first_helper_poll_stops',
    'second_helper_poll_stops','second_helper_modeled_returns')}
  if not new_chain['passed']:
   status['status']='PHASE226_ORIGINAL_LIST_IRQ_HELPER_SYNTHETIC_DIFFERENCE';return 2
  # Unlike the synthetic IRQ11 gate, this explicitly executes the bounded
  # translated ORIGINAL handler blocks and passes their real native register
  # outputs to the existing compiled helper wrappers in the same host process.
  handler_rc=run_handler_suite(exe['handler'],None,REPORT/'PHASE226_HANDLER_BOUNDARY_SYNTHETIC.json',offline=True)
  status['original_handler_boundary_synthetic_pass']=handler_rc==0
  if handler_rc:
   status['status']='ORIGINAL_HANDLER_BOUNDARY_SYNTHETIC_DIFFERENCE';return 2
  bridged=compare_handler_bridge(exe['handler_bridge'])
  write('PHASE226_HANDLER_TO_HELPER_SYNTHETIC.json',bridged)
  status['handler_to_helper_cases']=bridged['case_count']
  status['handler_to_helper_pass']=bridged['passed']
  if not bridged['passed']:
   status['status']='PHASE226_ORIGINAL_HANDLER_TO_HELPER_SYNTHETIC_DIFFERENCE';return 2
  doubled=compare_double_helper(exe['double_helper'])
  write('PHASE226_DOUBLE_HELPER_SYNTHETIC.json',doubled)
  status['double_helper_cases']=doubled['case_count']
  status['double_helper_synthetic_pass']=doubled['passed']
  if not doubled['passed']:
   status['status']='PHASE226_DOUBLE_HELPER_SYNTHETIC_DIFFERENCE';return 2
  admitted=compare_irq_original_chain(exe['original_irq_chain'])
  write('PHASE226_ORIGINAL_IRQ11_CHAIN_SYNTHETIC.json',admitted)
  status['original_irq11_chain_cases']=admitted['case_count']
  status['original_irq11_chain_synthetic_pass']=admitted['passed']
  status['original_irq11_bounded_handler_dispatches']=admitted['dispatched_original_handler_bounded_blocks']
  status['original_irq11_suppressed_dispatches']=admitted['suppressed_without_handler_execution']
  if not admitted['passed']:
   status['status']='PHASE226_VIRTUAL_IRQ_ORIGINAL_HANDLER_DIFFERENCE';return 2
  handoff=compare_handoff(exe['handoff'],synthetic_instructions())
  write('PHASE226_IRQ11_HANDOFF_SYNTHETIC.json',handoff)
  status['irq11_handoff_cases']=handoff['case_count']
  status['irq11_handoff_pass']=handoff['passed']
  status['irq11_handoff_synthetic_dispatch_count']=handoff['dispatched']
  status['irq11_handoff_did_not_execute_original_handler']=True
  if not handoff['passed']:
   status['status']='PHASE226_IRQ11_HANDOFF_SYNTHETIC_DIFFERENCE';return 2
  observed_native=run_observations(exe['observation'],native_cases())
  write('PHASE226_CP0_OBSERVATION_SYNTHETIC.json',observed_native)
  status['observation_model_case_count']=observed_native['case_count']
  status['observation_model_pass']=observed_native['passed']
  if not observed_native['passed']:
   status['status']='PHASE226_CP0_OBSERVATION_NATIVE_DIFFERENCE';return 2
  # A real trace is never fabricated or inferred from retail source identity.
  optional_trace=Path(args.trace).expanduser() if args.trace else ROOT/'PHASE226_EXTERNAL_EE_OBSERVATIONS.jsonl'
  if args.trace and not optional_trace.is_file():
   status['status']='PHASE226_EXPLICIT_TRACE_FILE_MISSING'
   status['external_trace_error']='Requested external trace file does not exist'
   return 2
  if optional_trace.is_file():
   status['external_trace_supplied']=True
   try:
    observation=verify_trace(optional_trace)
   except (ValueError,OSError,UnicodeError) as exc:
    status['external_trace_error']=type(exc).__name__+': '+str(exc)[:250]
    status['status']='PHASE226_EXTERNAL_TRACE_REJECTED';return 2
   write('PHASE226_EXTERNAL_TRACE_COMPARISON.json',observation)
   status['external_observation_status']=observation['status']
   status['external_observation_count']=observation['observation_count']
   status['external_observation_mismatches']=observation['mismatches']
   status['external_observation_inconclusive']=observation['inconclusive']
   if observation['mismatches']:
    status['status']='PHASE226_EXTERNAL_OBSERVATION_DIFFERENCE';return 2
  else:
   status['external_observation_status']='NO_EXTERNAL_TRACE_SUPPLIED'
  ei_model=compare_ee_model(exe['ee_model'],synthetic_instructions())
  chain=compare_chain(exe['chain'],synthetic_instructions())
  mmio=compare_mmio(exe['mmio'],mmio_words())
  cp0=compare_cp0(exe['cp0'],cp0_words())
  shared=compare_shared(exe['shared'],synthesize_translated_words())
  wrappers=compare_wrappers(exe['wrapper'],{entry:synthetically_assembled(entry) for entry in ENTRIES})
  for key,result in [('EE_TRANSITION_MODEL',ei_model),('WRAPPER_SHARED_CHAIN',chain),('VIRTUAL_MMIO_ERET',mmio),('PREVIOUS_CP0',cp0),('PREVIOUS_SHARED',shared),('PREVIOUS_WRAPPERS',wrappers)]:
   write('PHASE226_'+key+'_SYNTHETIC.json',result)
   status[key.lower()+'_cases']=result['case_count']
   status[key.lower()+'_pass']=result['passed']
  if not all(x['passed'] for x in (ei_model,chain,mmio,cp0,shared,wrappers)):
   status['status']='NATIVE_SYNTHETIC_DIFFERENCE';return 2
  if args.skip_retail:
   status['retail_status']='SKIPPED_BY_USER'
   status['status']='PHASE226_SYNTHETIC_PASS_RETAIL_SKIPPED_TRACE_OPTIONAL_NOT_BOOTABLE';return 0
  game=Path(args.game).expanduser() if args.game else search_elf('',[ROOT])
  if not game and args.choose_game:game=choose_game()
  if not game:
   status['retail_status']='NO_OWNED_GAME_INPUT'
   status['status']='PHASE226_SYNTHETIC_PASS_RETAIL_NOT_CHECKED_TRACE_OPTIONAL_NOT_BOOTABLE';return 0
  status['owned_game_type']='ISO' if game.suffix.lower()=='.iso' else 'ZIP' if game.suffix.lower()=='.zip' else 'ELF_OR_BIN'
  try:
   blob,kind=load_retail_elf(game)
   list_rows=checked_list_words(blob)
   retail_list=compare_list_walk(exe['original_list_walk'],list_rows)
   write('PHASE226_ORIGINAL_LIST_WALK_RETAIL.json',{**retail_list,'retail_elf_sha256':EXPECTED_SHA256})
   status['retail_original_list_walk_cases']=retail_list['case_count']
   status['retail_original_list_walk_pass']=retail_list['passed']
   if not retail_list['passed']:
    status['retail_status']='DIFFERENCE'
    status['status']='PHASE226_RETAIL_LIST_WALK_DIFFERENCE';return 2
   pinned_handler_snapshot=handler_snapshot(blob)
   pinned_handler_rows=verified_handler_rows(pinned_handler_snapshot,allow_stage_absent=True)
   rlive=compare_live_memory(exe['live_memory'],list_rows,pinned_retail(blob),pinned_handler_rows,pinned_handler_rows)
   write('PHASE226_LIVE_GUEST_MEMORY_RETAIL.json',{**rlive,'retail_elf_sha256':EXPECTED_SHA256})
   status['retail_live_guest_memory_cases']=rlive['case_count']
   status['retail_live_guest_memory_pass']=rlive['passed']
   if not rlive['passed']:
    status['retail_status']='DIFFERENCE'
    status['status']='PHASE226_LIVE_GUEST_MEMORY_RETAIL_DIFFERENCE';return 2
   rnew=compare_list_irq_chain(exe['list_irq_chain'],list_rows,pinned_retail(blob),pinned_handler_rows,pinned_handler_rows)
   write('PHASE226_ORIGINAL_LIST_IRQ_HELPER_RETAIL.json',{
    **rnew,'retail_elf_sha256':EXPECTED_SHA256,
    'verified_guest_interrupt_timing':False})
   status['retail_original_list_irq_chain_cases']=rnew['case_count']
   status['retail_original_list_irq_chain_pass']=rnew['passed']
   if not rnew['passed']:
    status['retail_status']='DIFFERENCE'
    status['status']='PHASE226_ORIGINAL_LIST_IRQ_HELPER_RETAIL_DIFFERENCE';return 2
   # Native original handler call-boundary tests: SHA-pinned real opcodes.
   retail_handler_rc=run_handler_suite(exe['handler'],pinned_handler_snapshot,
     REPORT/'PHASE226_HANDLER_BOUNDARY_RETAIL.json',allow_stage_absent=True)
   rbridge=compare_handler_bridge(exe['handler_bridge'],pinned_handler_rows,pinned_retail(blob))
   write('PHASE226_HANDLER_TO_HELPER_RETAIL.json',{
      **rbridge,'retail_elf_sha256':EXPECTED_SHA256,'actual_game_irq11_handler_registered':False})
   status['retail_handler_boundary_pass']=retail_handler_rc==0
   status['retail_handler_to_helper_pass']=rbridge['passed']
   status['retail_handler_to_helper_cases']=rbridge['case_count']
   if retail_handler_rc or not rbridge['passed']:
    status['retail_status']='DIFFERENCE'
    status['status']='PHASE226_ORIGINAL_HANDLER_BRIDGE_RETAIL_DIFFERENCE'
    return 2
   pinned_chain=pinned_retail(blob)
   # Original 19 post-JAL instruction words are checked against two independent
   # retail window hashes; the new native continuation is opcode-interpreted.
   rdoubled=compare_double_helper(exe['double_helper'],pinned_handler_rows,pinned_chain,pinned_handler_rows)
   write('PHASE226_DOUBLE_HELPER_RETAIL.json',{
      **rdoubled,'retail_elf_sha256':EXPECTED_SHA256,'real_ps2_eret_semantics_verified':False})
   status['retail_double_helper_cases']=rdoubled['case_count']
   status['retail_double_helper_pass']=rdoubled['passed']
   if not rdoubled['passed']:
    status['retail_status']='DIFFERENCE'
    status['status']='PHASE226_DOUBLE_HELPER_RETAIL_DIFFERENCE'
    return 2
   radmitted=compare_irq_original_chain(exe['original_irq_chain'],pinned_handler_rows,pinned_chain,pinned_handler_rows)
   write('PHASE226_ORIGINAL_IRQ11_CHAIN_RETAIL.json',{
       **radmitted,'retail_elf_sha256':EXPECTED_SHA256,'hardware_irq11_verified':False})
   status['retail_original_irq11_chain_cases']=radmitted['case_count']
   status['retail_original_irq11_chain_pass']=radmitted['passed']
   if not radmitted['passed']:
    status['retail_status']='DIFFERENCE'
    status['status']='PHASE226_ORIGINAL_IRQ11_CHAIN_RETAIL_DIFFERENCE'
    return 2
   pinned_mmio=mmio_retail(blob)    # SHA + entire retail window and translated instruction parity
   pinned_cp0=cp0_retail(blob)
   pinned_shared=read_retail_window(blob)
   pinned_wrappers=retail_instructions(blob)
   rhand=compare_handoff(exe['handoff'],pinned_chain)
   ree_model=compare_ee_model(exe['ee_model'],pinned_chain)
   rchain=compare_chain(exe['chain'],pinned_chain)
   rmmio=compare_mmio(exe['mmio'],pinned_mmio)
   rcp0=compare_cp0(exe['cp0'],pinned_cp0)
   rshared=compare_shared(exe['shared'],pinned_shared)
   rwrappers=compare_wrappers(exe['wrapper'],pinned_wrappers)
  except (AuditError,OSError,ValueError,KeyError,zipfile.BadZipFile) as exc:
   status['retail_status']='REJECTED'
   status['status']='PHASE226_RETAIL_INPUT_REJECTED'
   status['retail_error']=type(exc).__name__+': '+str(exc)[:250]
   return 2
  write('PHASE226_RETAIL_IRQ11_HANDOFF.json',{**rhand,'retail_elf_sha256':EXPECTED_SHA256,'actual_ps2_irq11_dispatch_verified':False})
  status['retail_irq11_handoff_cases']=rhand['case_count']
  status['retail_irq11_handoff_pass']=rhand['passed']
  for key,result in [('EE_TRANSITION_MODEL',ree_model),('WRAPPER_SHARED_CHAIN',rchain),('VIRTUAL_MMIO_ERET',rmmio),('PREVIOUS_CP0',rcp0),('PREVIOUS_SHARED',rshared),('PREVIOUS_WRAPPERS',rwrappers)]:
   write('PHASE226_RETAIL_'+key+'.json',{**result,'retail_elf_sha256':EXPECTED_SHA256,
      'shared_window_sha256':WINDOW_DIGEST,'actual_host_mmio_writes':0,'actual_guest_eret_semantics_verified':False,'actual_PS2_interrupt_timing_verified':False})
   status['retail_'+key.lower()+'_cases']=result['case_count']
   status['retail_'+key.lower()+'_pass']=result['passed']
  status.update({'retail_elf_sha256':EXPECTED_SHA256,'retail_source_kind':kind})
  status['retail_status']='PASS' if (status.get('retail_live_guest_memory_pass') and status.get('retail_original_list_irq_chain_pass') and status.get('retail_original_list_walk_pass') and status.get('retail_double_helper_pass') and status.get('retail_original_irq11_chain_pass') and status.get('retail_handler_to_helper_pass') and status.get('retail_handler_boundary_pass') and all(x['passed'] for x in (rhand,ree_model,rchain,rmmio,rcp0,rshared,rwrappers))) else 'DIFFERENCE'
  status['status']='PHASE226_LIVE_MEMORY_RETAIL_PASS_NOT_BOOTABLE' if status['retail_status']=='PASS' else 'PHASE226_RETAIL_DIFFERENCE'
  return 0 if status['retail_status']=='PASS' else 2
 except Exception as exc:
  status['status']='PHASE226_RUNNER_ERROR'
  status['error']=type(exc).__name__+': '+str(exc)[:300]
  return 2
 finally:
  write('PHASE226_STATUS.json',status)
  pack()
  print('PHASE226:',status['status'])
  print('Upload UPLOAD_PHASE226_RESULTS.zip and UPLOAD_PHASE226_DIAGNOSTICS.zip')
if __name__=='__main__':sys.exit(main())
