#!/usr/bin/env python3
import json,platform,zipfile
from pathlib import Path
root=Path(__file__).resolve().parent
report=root/'phase226_reports';report.mkdir(exist_ok=True)
try:st=json.loads((report/'PHASE226_STATUS.json').read_text())
except Exception:st={'status':'RUNNER_NOT_COMPLETED'}
(report/'PHASE226_DIAGNOSTIC_SUMMARY.json').write_text(json.dumps({
 'phase':226,'status':st.get('status'),'platform':platform.system(),
 'previous_phases_required':False,'msvc_compiler_verified':st.get('msvc_compiler_verified'),
 'retail_status':st.get('retail_status'),
 'ee_transition_synthetic_pass':st.get('ee_transition_model_pass'),
 'retail_ee_transition_pass':st.get('retail_ee_transition_model_pass'),
 'guest_interrupt_timing_verified':False,
 'live_guest_memory_synthetic_pass':st.get('live_guest_memory_synthetic_pass'),
 'live_guest_memory_retail_pass':st.get('retail_live_guest_memory_pass'),
 'live_guest_memory_cases':st.get('live_guest_memory_cases'),
 'live_guest_memory_observable_changes':st.get('live_guest_memory_changes_observed'),
 'original_list_walk_synthetic_pass':st.get('original_list_walk_pass'),
 'original_list_walk_retail_pass':st.get('retail_original_list_walk_pass'),
 'original_list_walk_cases':st.get('original_list_walk_cases'),
 'original_list_irq_helper_synthetic_pass':st.get('original_list_irq_chain_synthetic_pass'),
 'original_list_irq_helper_retail_pass':st.get('retail_original_list_irq_chain_pass'),
 'original_list_irq_helper_cases':st.get('original_list_irq_chain_cases'),
 'original_list_irq_helper_outcomes':st.get('original_list_irq_chain_outcomes'),
 'original_list_walk_outcomes':st.get('original_list_walk_outcomes'),
 'irq11_handoff_pass':st.get('irq11_handoff_pass'),
 'original_handler_boundary_pass':st.get('original_handler_boundary_synthetic_pass'),
 'original_handler_to_helper_bridge_pass':st.get('handler_to_helper_pass'),
 'original_handler_to_helper_retail_pass':st.get('retail_handler_to_helper_pass'),
 'post_helper_continuation_synthetic_pass':st.get('double_helper_synthetic_pass'),
 'post_helper_continuation_retail_pass':st.get('retail_double_helper_pass'),
 'virtual_original_handler_irq11_chain_pass':st.get('original_irq11_chain_synthetic_pass'),
 'virtual_original_handler_irq11_chain_retail_pass':st.get('retail_original_irq11_chain_pass'),
 'bounded_original_handler_dispatches':st.get('original_irq11_bounded_handler_dispatches'),
 'original_handler_registered':False,
 'game_function_registry_bindings_added':0,
 'retail_irq11_handoff_pass':st.get('retail_irq11_handoff_pass'),
 'virtual_irq11_dispatch_count':st.get('irq11_handoff_synthetic_dispatch_count'),
 'real_ps2_timer2_irq11_dispatch_verified':False,
 'external_observation_status':st.get('external_observation_status'),
 'external_trace_authenticity_verified':False,
 'observation_model_pass':st.get('observation_model_pass'),
 'chain_synthetic_pass':st.get('wrapper_shared_chain_pass'),
 'retail_chain_pass':st.get('retail_wrapper_shared_chain_pass'),
 'integrated_virtual_wrapper_to_shared':True,
 'virtual_mmio_synthetic_pass':st.get('virtual_mmio_eret_pass'),
 'retail_virtual_mmio_pass':st.get('retail_virtual_mmio_eret_pass'),
 'hardware_effects':'local in-memory snapshots only',
 'host_privileged_instructions_executed':False,
 'host_hardware_registers_written':False,'game_booted':False,
},indent=2)+'\n',encoding='utf8')
with zipfile.ZipFile(root/'UPLOAD_PHASE226_DIAGNOSTICS.zip','w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(report.iterdir()):
  if p.is_file() and p.suffix in ('.json','.txt','.log') and p.stat().st_size<2_000_000:
   z.write(p,'phase226/'+p.name)
print('UPLOAD_PHASE226_DIAGNOSTICS.zip collected')
