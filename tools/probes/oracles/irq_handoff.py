"""Independent retail-opcode oracle for a VIRTUAL IRQ11 handoff test adapter.

IRQ11 gate flags and pending-bit acknowledgement are arbitrary named fixture
inputs, not validated PS2 INTC behavior or native runtime registration.
A dispatched event uses the original retail instruction interpreter for the
*actual* previously translated tail-wrapper -> shared helper code path.
"""
from __future__ import annotations
import subprocess
from .wrapper_chain import scenarios as integrated_cases,oracle as retail_oracle

FLAGS_ALL=tuple(range(16))

def cases():
    for i,chain_case in enumerate(integrated_cases()):
        for flag in (i%16,(i+7)%16,7,15):
            yield (chain_case[0],flag,*chain_case[1:])

def expected(words,case):
    entry,flag,*payload=case
    requested=bool(flag&1)
    dispatched=int(requested and bool(flag&2) and bool(flag&4))
    remaining=int(requested and not (dispatched and bool(flag&8)))
    native_chain=retail_oracle(words,(entry,*payload)) if dispatched else (0,)*27
    return (entry,flag,dispatched,remaining,*native_chain)

def compare_native(executable,words):
    inputs=list(cases())
    data=[]
    for c in inputs:
        assert len(c)==14,(len(c),c)
        data.append(' '.join([*(f'{v:08X}' for v in c[:5]),
                  *(f'{v:016X}' for v in c[5:12]),
                  *(f'{v:08X}' for v in c[12:])])+'\n')
    p=subprocess.run([str(executable)],input=''.join(data),text=True,
                     capture_output=True,timeout=120)
    if p.returncode:raise ValueError(f'IrqHandoff native exit {p.returncode}: {p.stderr[:300]}')
    lines=p.stdout.splitlines()
    if len(lines)!=len(inputs):raise ValueError(f'Output lines {len(lines)} != {len(inputs)}')
    problems=[];dispatched=0;denied=0;ack=0;return_count=0;poll_count=0
    by_entry={'0x00113400':0,'0x00113410':0}
    for idx,(c,line) in enumerate(zip(inputs,lines)):
        fields=line.split()
        if len(fields)!=31:raise ValueError(f'Case {idx} expected 31 fields, got {len(fields)}')
        try:got=tuple(int(s,16) for s in fields)
        except ValueError as exc:raise ValueError(f'Invalid hex in line {idx}') from exc
        predicted=expected(words,c)
        if got!=predicted:problems.append({'case':idx,'entry':hex(c[0]),'flags':c[1],
                        'expected':[hex(x) for x in predicted],
                        'observed':[hex(x) for x in got]})
        if predicted[2]:
            dispatched+=1
            by_entry[f'0x{c[0]:08X}']+=1
            if predicted[26]==1:return_count+=1
            elif predicted[26]==2:poll_count+=1
        else:denied+=1
        if predicted[2] and not predicted[3] and (c[1]&8):ack+=1
    return {'case_count':len(inputs),'passed':not problems,'mismatches':problems[:5],
       'dispatched':dispatched,'suppressed':denied,'explicit_acknowledgments':ack,
       'modeled_returns':return_count,'bounded_poll_stops':poll_count,
       'dispatch_by_wrapper':by_entry,'verified_with':
       'bounded retail instruction interpreter + harness gating contract',
       'guest_irq11_handler_executed':False,'actual_ps2_interrupt_behavior_verified':False,
       'hardware_effects':'in-memory virtual CP0/MMIO only'}
