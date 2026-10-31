"""Model-observed EE EI/DI *permission* and injected external CP0 changes.

Neither EI timing nor true interrupt arrival is inferred from the retail ELF.
Guest EI raises EIE subject to EDI / KSU privilege; the optional clear event
is an intentionally synthetic, independently controlled state observation.
"""
from __future__ import annotations
import subprocess
from .wrapper_chain import synthetic_instructions,pinned_retail,oracle as chain_oracle,PORTS
from .tail_wrappers import MASK
from .mmio_eret import STOP_ERET,STOP_EI_LIMIT
EIE=0x10000; EDI=0x20000; EXL=2; ERL=4; KSU=0x18
STATES=(0,0x10000,0x10001,0x10003,0x10004,0x1000c,0x30000,
        0x10010,0x10018,0x30010,0x30018,0x10012,0x1001c,0x1ffff,
        0xffff0000,0x80010000)

def can_execute_ei(status):
    return bool((status&EDI) or (status&(EXL|ERL)) or not (status&KSU))
def ei_effect(status):
    return (status|EIE) if can_execute_ei(status) else status
def observed_checkpoint(status,mask,index):
    if mask not in range(4) or index not in (1,2):raise ValueError('Invalid checkpoint mode')
    return status&~EIE if mask&(1<<(index-1)) else status

def transitions(status,mask):
    e1=observed_checkpoint(ei_effect(status),mask,1)
    e2=observed_checkpoint(ei_effect(e1),mask,2)
    return e1,e2

def scenarios():
    for entry in (0x113400,0x113410):
        for si,status in enumerate(STATES):
            for mode in range(4):
                for variant in range(3):
                    mix=(si*0x9e3779b97f4a7c15+mode*0xbf58476d1ce4e5b+
                         variant*0x94d049bb133111eb+(entry<<7))&MASK
                    vals=tuple(((mix>>(k+1))^(mix<<((k*3)%23))^
                                 (0xd1b54a32d192ed03*(k+1)))&MASK for k in range(7))
                    v0,v1,a2,a0,a1,ra,sp=vals
                    epc=(0x80004000 ^ (mix>>19))&0xffffffff
                    old_error_epc=(0x1234beef ^ (mix>>11))&0xffffffff
                    yield(entry,status,mode,v0,v1,a2,a0,a1,ra,sp,epc,old_error_epc)

def independent_oracle(words,case):
    entry,status,mode,v0,v1,a2,a0,a1,ra,sp,epc,old_eepc=case
    e1,e2=transitions(status,mode)
    previous=(entry,status,e1,e2,v0,v1,a2,a0,a1,ra,sp,epc,old_eepc)
    result=chain_oracle(words,previous)
    return (e1,e2,*result)

def compare_native(exe,words):
    cases=list(scenarios())
    payload=''.join(' '.join((*(f'{v:08X}' for v in c[:3]),
                 *(f'{v:016X}' for v in c[3:10]),
                 *(f'{v:08X}' for v in c[10:])))+'\n' for c in cases)
    run=subprocess.run([str(exe)],input=payload,text=True,capture_output=True,timeout=75)
    if run.returncode:
        raise ValueError(f'InterruptTransitions native return={run.returncode}: {run.stderr[:300]}')
    output=run.stdout.splitlines()
    if len(output)!=len(cases):
        raise ValueError(f'Native rows={len(output)}, input cases={len(cases)}')
    failures=[];returns=spins=0;one_ei=two_ei=none_ei=0
    mode_outcomes={str(mode):{'return':0,'poll_limit':0} for mode in range(4)}
    privileges={'permitted':0,'blocked':0}
    for i,(case,line) in enumerate(zip(cases,output)):
        fields=line.split()
        if len(fields)!=29:raise ValueError(f'Native row {i}: expected 29 output columns, got {len(fields)}')
        try:got=tuple(int(v,16) for v in fields)
        except ValueError as exc:raise ValueError(f'Malformed native hex in row {i}') from exc
        expected=independent_oracle(words,case)
        if got!=expected:
            failures.append({'index':i,'entry':hex(case[0]),'input_status':hex(case[1]),
                             'checkpoint_mode':case[2],
                             'expected':[hex(x) for x in expected],
                             'actual':[hex(x) for x in got]})
        stop=expected[24]
        eis=expected[19]
        if stop==STOP_ERET:returns+=1;mode_outcomes[str(case[2])]['return']+=1
        elif stop==STOP_EI_LIMIT:spins+=1;mode_outcomes[str(case[2])]['poll_limit']+=1
        else:raise ValueError(f'Unexpected stop={stop}')
        if eis==0:none_ei+=1
        elif eis==1:one_ei+=1
        elif eis==2:two_ei+=1
        else:raise ValueError('Unexpected EI count')
        privileges['permitted' if can_execute_ei(case[1]) else 'blocked']+=1
    return {'passed':not failures,'case_count':len(cases),'failures':failures[:6],
            'virtual_return_count':returns,'poll_limit_count':spins,
            'ei_count_distribution':{'0':none_ei,'1':one_ei,'2':two_ei},
            'mode_outcomes':mode_outcomes,'initial_privilege_cases':privileges,
            'retail_instruction_words_verified':False,
            'ei_privilege_contract_model_only':True,
            'external_event_clear_is_injected_fixture':True,
            'actual_PS2_interrupt_timing_verified':False,
            'guest_hardware_accesses_executed':0,'game_booted':False}
