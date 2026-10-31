"""compare explicit, untrusted external observations with modeled EE CP0.

Real capture is OPTIONAL and never inferred from synthetic data, user supplied
source labels, retail ELF identity, or successful C++ model tests.
"""
from __future__ import annotations
import json,subprocess
from pathlib import Path
from .interrupt_transitions import STATES,ei_effect,EIE

PCS={'ei':(1,0x113398),'mtc0_status':(2,0x1133d0),'eret':(3,0x1133e4)}
EVENTS={'none':0,'clear_eie':1,'set_eie':2,'unknown':None}
LABELS={'pcsx2','real_ps2','other_emulator','unverified_manual'}
MAX_TRACE_BYTES=1024*1024

def expected(kind,before,value=0,event=0,epc=0,error_epc=0):
    if kind==1:after=ei_effect(before);pc=0x11339c
    elif kind==2:after=value&0xffffffff;pc=0x1133d4
    elif kind==3:
        if before&4:after=before&~4;pc=error_epc
        else:after=before&~2;pc=epc
    else:raise ValueError('Unknown opcode contract')
    if event==1:after &=~EIE
    elif event==2:after |=EIE
    elif event!=0:raise ValueError('Invalid event')
    return after&0xffffffff,pc&0xffffffff

def native_cases():
    for k in (1,2,3):
        for si,s in enumerate(STATES):
            for event in (0,1,2):
                for variant in range(3):
                    value=(0xdeadbeef ^ (si*0x9e3779b9) ^ (variant*0x01234567))&0xffffffff
                    epc=(0x80001000 + si*0x40 + variant*4)&0xffffffff
                    eepc=(0x80100000 + si*0x50 + variant*8)&0xffffffff
                    yield (k,s,value,event,epc,eepc)

def run_native(exe,cases):
    cases=list(cases)
    if len(cases)>4096:raise ValueError('Too many observation cases')
    payload=''.join(' '.join(f'{v:08X}' for v in c)+'\n' for c in cases)
    proc=subprocess.run([str(exe)],input=payload,text=True,capture_output=True,timeout=45)
    if proc.returncode:raise ValueError(f'Cp0Observations native returned {proc.returncode}: {proc.stderr[:160]}')
    lines=proc.stdout.splitlines()
    if len(lines)!=len(cases):raise ValueError('Incorrect observation response count')
    mismatches=[]
    for i,(c,line) in enumerate(zip(cases,lines)):
        columns=line.split()
        if len(columns)!=2 or any(len(x)>8 or not x for x in columns):
            raise ValueError('Malformed Cp0Observations native response')
        result=tuple(int(v,16) for v in columns)
        oracle=expected(*c)
        if result!=oracle:mismatches.append({'row':i,'got':[hex(v) for v in result],
                                            'expected':[hex(v) for v in oracle]})
    return {'case_count':len(cases),'passed':not mismatches,'mismatches':mismatches[:8],
            'native_semantics_differential_only':True,
            'real_guest_trace_verified':False,'real_ps2_timing_verified':False}

def parse_u32(v,key):
    if not isinstance(v,str) or not v.startswith('0x') or not 1<=len(v[2:])<=8:
        raise ValueError(f'{key} must be 0x-prefixed hexadecimal string, <= 32 bits')
    try:x=int(v,16)
    except ValueError as e:raise ValueError(f'Invalid {key}') from e
    if not 0<=x<=0xffffffff:raise ValueError('Out of range '+key)
    return x

def parse_observation(row,index):
    if not isinstance(row,dict):raise ValueError('Trace row must be an object')
    if row.get('source') not in LABELS:raise ValueError('Invalid source label')
    if not isinstance(row.get('capture_id'),str) or not 1<=len(row['capture_id'])<=128:
        raise ValueError('capture_id required, 1 to 128 characters')
    kind=row.get('instruction')
    if kind not in PCS:raise ValueError('Unknown instruction name')
    opcode,pc=PCS[kind]
    observed_pc=parse_u32(row.get('pc'),'pc')
    if observed_pc!=pc:raise ValueError('Wrong instruction address for '+kind)
    before=parse_u32(row.get('status_before'),'status_before')
    after=parse_u32(row.get('status_after'),'status_after')
    external=row.get('external_event')
    if external not in EVENTS:raise ValueError('external_event must be declared')
    value=parse_u32(row.get('write_value','0x00000000'),'write_value')
    epc=parse_u32(row.get('epc','0x00000000'),'epc')
    eepc=parse_u32(row.get('error_epc','0x00000000'),'error_epc')
    after_pc=row.get('pc_after')
    after_pc=parse_u32(after_pc,'pc_after') if after_pc is not None else None
    known=EVENTS[external] is not None
    predicted=expected(opcode,before,value,EVENTS[external] or 0,epc,eepc) if known else None
    verdict='INCONCLUSIVE_UNKNOWN_EXTERNAL_EVENT' if not known else (
            'MATCH' if predicted[0]==after and (after_pc is None or after_pc==predicted[1]) else 'MISMATCH')
    return {'row_index':index,'capture_id':row['capture_id'],'reported_source':row['source'],
            'instruction':kind,'verdict':verdict,'observed_status_after':f'0x{after:08X}',
            'predicted_status_after':f'0x{predicted[0]:08X}' if predicted else None,
            'observed_pc_after':f'0x{after_pc:08X}' if after_pc is not None else None,
            'predicted_pc_after':f'0x{predicted[1]:08X}' if predicted else None,
            'independent_trace_authenticity_verified':False}

def verify_trace(path):
    p=Path(path)
    if p.stat().st_size>MAX_TRACE_BYTES:raise ValueError('Trace file exceeds 1 MiB limit')
    raw=p.read_text(encoding='utf-8-sig')
    rows=[]
    for index,line in enumerate(raw.splitlines()):
        if not line.strip():continue
        if len(rows)>=500:raise ValueError('Trace exceeds 500 observations')
        try:row=json.loads(line)
        except (TypeError,ValueError) as exc:raise ValueError(f'Invalid JSON at line {index+1}') from exc
        try:rows.append(parse_observation(row,index+1))
        except ValueError as exc:raise ValueError(f'Line {index+1}: {exc}') from exc
    if not rows:raise ValueError('Empty trace file')
    matches=sum(r['verdict']=='MATCH' for r in rows)
    diffs=sum(r['verdict']=='MISMATCH' for r in rows)
    inconclusive=len(rows)-matches-diffs
    return {'status':'OBSERVATIONS_DISAGREE' if diffs else (
            'OBSERVATIONS_CONSISTENT_UNAUTHENTICATED' if not inconclusive else 'OBSERVATIONS_PARTLY_INCONCLUSIVE'),
            'observation_count':len(rows),'matches':matches,'mismatches':diffs,'inconclusive':inconclusive,
            'captures':sorted({r['capture_id'] for r in rows}),
            'reported_sources':sorted({r['reported_source'] for r in rows}),
            'trace_provided':True,'trace_authenticity_verified':False,
            'actual_ps2_irq_timing_verified':False,
            'records':rows[:60]}
