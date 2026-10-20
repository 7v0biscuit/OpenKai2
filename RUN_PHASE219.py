#!/usr/bin/env python3
"""Phase 219 clean-install Windows/Linux native probe; entirely virtual PS2 hardware."""
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
ROOT=Path(__file__).resolve().parent
REPORT=ROOT/'phase219_reports';BUILD=ROOT/'build_phase219_standalone'
TARGETS={
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
 with zipfile.ZipFile(ROOT/'UPLOAD_PHASE219_RESULTS.zip','w',zipfile.ZIP_DEFLATED) as z:
  for p in sorted(REPORT.iterdir()):
   if p.is_file() and p.suffix in ('.json','.txt','.log') and p.stat().st_size<2_000_000:
    z.write(p,'phase218/'+p.name)

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
  'phase':219,'status':'STARTED','platform':platform.system(),
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
 }
 try:
  manifest=json.loads((ROOT/'RELEASE_MANIFEST_SHA256.json').read_text(encoding='utf8'))
  errors=[n for n,d in manifest.items() if not (ROOT/n).is_file() or hashlib.sha256((ROOT/n).read_bytes()).hexdigest()!=d]
  status['source_integrity_errors']=errors
  if errors:status['status']='SOURCE_INTEGRITY_FAILED';return 2
  cmake=shutil.which('cmake')
  if not cmake:status['status']='CMAKE_NOT_FOUND';return 2
  status['cmake_configure_rc']=cmd([cmake,'-S',str(ROOT/'standalone'),'-B',str(BUILD)],'PHASE219_CMAKE_CONFIGURE',180)
  if status['cmake_configure_rc']:status['status']='CMAKE_CONFIGURE_FAILED';return 2
  cache=(BUILD/'CMakeCache.txt').read_text(errors='replace')
  status['generator_visual_studio']='CMAKE_GENERATOR:INTERNAL=Visual Studio' in cache
  status['msvc_compiler_verified']=bool(os.name=='nt' and status['generator_visual_studio'] and (BUILD/(TARGETS['ee_model']+'.vcxproj')).is_file())
  status['native_build_rc']=cmd([cmake,'--build',str(BUILD),'--config','Release','--target',*TARGETS.values()],'PHASE219_NATIVE_BUILD',600)
  if status['native_build_rc']:status['status']='NATIVE_BUILD_FAILED';return 2
  exe={k:find_exe(v) for k,v in TARGETS.items()}
  if not all(exe.values()):status['status']='NATIVE_EXECUTABLE_MISSING';return 2
  observed_native=run_observations(exe['observation'],native_cases())
  write('PHASE219_CP0_OBSERVATION_SYNTHETIC.json',observed_native)
  status['observation_model_case_count']=observed_native['case_count']
  status['observation_model_pass']=observed_native['passed']
  if not observed_native['passed']:
   status['status']='PHASE219_CP0_OBSERVATION_NATIVE_DIFFERENCE';return 2
  # A real trace is never fabricated or inferred from retail source identity.
  optional_trace=Path(args.trace).expanduser() if args.trace else ROOT/'PHASE219_EXTERNAL_EE_OBSERVATIONS.jsonl'
  if args.trace and not optional_trace.is_file():
   status['status']='PHASE219_EXPLICIT_TRACE_FILE_MISSING'
   status['external_trace_error']='Requested external trace file does not exist'
   return 2
  if optional_trace.is_file():
   status['external_trace_supplied']=True
   try:
    observation=verify_trace(optional_trace)
   except (ValueError,OSError,UnicodeError) as exc:
    status['external_trace_error']=type(exc).__name__+': '+str(exc)[:250]
    status['status']='PHASE219_EXTERNAL_TRACE_REJECTED';return 2
   write('PHASE219_EXTERNAL_TRACE_COMPARISON.json',observation)
   status['external_observation_status']=observation['status']
   status['external_observation_count']=observation['observation_count']
   status['external_observation_mismatches']=observation['mismatches']
   status['external_observation_inconclusive']=observation['inconclusive']
   if observation['mismatches']:
    status['status']='PHASE219_EXTERNAL_OBSERVATION_DIFFERENCE';return 2
  else:
   status['external_observation_status']='NO_EXTERNAL_TRACE_SUPPLIED'
  ei_model=compare_ee_model(exe['ee_model'],synthetic_instructions())
  chain=compare_chain(exe['chain'],synthetic_instructions())
  mmio=compare_mmio(exe['mmio'],mmio_words())
  cp0=compare_cp0(exe['cp0'],cp0_words())
  shared=compare_shared(exe['shared'],synthesize_translated_words())
  wrappers=compare_wrappers(exe['wrapper'],{entry:synthetically_assembled(entry) for entry in ENTRIES})
  for key,result in [('EE_TRANSITION_MODEL',ei_model),('WRAPPER_SHARED_CHAIN',chain),('VIRTUAL_MMIO_ERET',mmio),('PREVIOUS_CP0',cp0),('PREVIOUS_SHARED',shared),('PREVIOUS_WRAPPERS',wrappers)]:
   write('PHASE219_'+key+'_SYNTHETIC.json',result)
   status[key.lower()+'_cases']=result['case_count']
   status[key.lower()+'_pass']=result['passed']
  if not all(x['passed'] for x in (ei_model,chain,mmio,cp0,shared,wrappers)):
   status['status']='NATIVE_SYNTHETIC_DIFFERENCE';return 2
  if args.skip_retail:
   status['retail_status']='SKIPPED_BY_USER'
   status['status']='PHASE219_SYNTHETIC_PASS_RETAIL_SKIPPED_TRACE_OPTIONAL_NOT_BOOTABLE';return 0
  game=Path(args.game).expanduser() if args.game else search_elf('',[ROOT])
  if not game and args.choose_game:game=choose_game()
  if not game:
   status['retail_status']='NO_OWNED_GAME_INPUT'
   status['status']='PHASE219_SYNTHETIC_PASS_RETAIL_NOT_CHECKED_TRACE_OPTIONAL_NOT_BOOTABLE';return 0
  status['owned_game_type']='ISO' if game.suffix.lower()=='.iso' else 'ZIP' if game.suffix.lower()=='.zip' else 'ELF_OR_BIN'
  try:
   blob,kind=load_retail_elf(game)
   pinned_chain=pinned_retail(blob)
   pinned_mmio=mmio_retail(blob)    # SHA + entire retail window and translated instruction parity
   pinned_cp0=cp0_retail(blob)
   pinned_shared=read_retail_window(blob)
   pinned_wrappers=retail_instructions(blob)
   ree_model=compare_ee_model(exe['ee_model'],pinned_chain)
   rchain=compare_chain(exe['chain'],pinned_chain)
   rmmio=compare_mmio(exe['mmio'],pinned_mmio)
   rcp0=compare_cp0(exe['cp0'],pinned_cp0)
   rshared=compare_shared(exe['shared'],pinned_shared)
   rwrappers=compare_wrappers(exe['wrapper'],pinned_wrappers)
  except (AuditError,OSError,ValueError,KeyError,zipfile.BadZipFile) as exc:
   status['retail_status']='REJECTED'
   status['status']='PHASE219_RETAIL_INPUT_REJECTED'
   status['retail_error']=type(exc).__name__+': '+str(exc)[:250]
   return 2
  for key,result in [('EE_TRANSITION_MODEL',ree_model),('WRAPPER_SHARED_CHAIN',rchain),('VIRTUAL_MMIO_ERET',rmmio),('PREVIOUS_CP0',rcp0),('PREVIOUS_SHARED',rshared),('PREVIOUS_WRAPPERS',rwrappers)]:
   write('PHASE219_RETAIL_'+key+'.json',{**result,'retail_elf_sha256':EXPECTED_SHA256,
      'shared_window_sha256':WINDOW_DIGEST,'actual_host_mmio_writes':0,'actual_guest_eret_semantics_verified':False,'actual_PS2_interrupt_timing_verified':False})
   status['retail_'+key.lower()+'_cases']=result['case_count']
   status['retail_'+key.lower()+'_pass']=result['passed']
  status.update({'retail_elf_sha256':EXPECTED_SHA256,'retail_source_kind':kind})
  status['retail_status']='PASS' if all(x['passed'] for x in (ree_model,rchain,rmmio,rcp0,rshared,rwrappers)) else 'DIFFERENCE'
  status['status']='PHASE219_VIRTUAL_MODEL_RETAIL_PASS_TRACE_OPTIONAL_NOT_BOOTABLE' if status['retail_status']=='PASS' else 'PHASE219_RETAIL_DIFFERENCE'
  return 0 if status['retail_status']=='PASS' else 2
 except Exception as exc:
  status['status']='PHASE219_RUNNER_ERROR'
  status['error']=type(exc).__name__+': '+str(exc)[:300]
  return 2
 finally:
  write('PHASE219_STATUS.json',status)
  pack()
  print('PHASE219:',status['status'])
  print('Upload UPLOAD_PHASE219_RESULTS.zip and UPLOAD_PHASE219_DIAGNOSTICS.zip')
if __name__=='__main__':sys.exit(main())
