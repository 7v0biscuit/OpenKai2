"""Phase221 independent composition oracle: original handler JAL -> wrapper ->
virtual shared helper. Guest ISA words are SHA-pinned for retail comparisons.
Never claims to execute actual guest IRQ, scheduler or PS2 hardware.
"""
from __future__ import annotations
import subprocess
from .helper_boundary_oracle import CASES, expected as handler_expected
from .entry_oracle import s32
from .phase217_semantics import oracle as chain_oracle
from .phase217_semantics import synthetic_instructions

VIRTUAL_CPU_STATES=(
 (0,0,0,0x80000000,0x12345678),
 (0x10000,0,0,0x80000044,0x87654321),
 (0x10000,0x10000,0,0x80000080,0x1234),
 (0x10000,0x10000,0x10000,0x80000084,0xf0000000),
 (4,4,4,0x800000a0,0xbadf00d),
 (2,2,2,0x800000b0,0x123),
)
MASK=(1<<64)-1

def bridge_cases(handler_words=None):
    # Include every available real handler call boundary for BOTH originals.
    for c in CASES:
        b=handler_expected(c,handler_words)
        if b['external_call_pending']:
            for snapshot in VIRTUAL_CPU_STATES:
                yield c,snapshot,b

def oracle(handler_words,chain_words,case,snapshot,boundary):
    entry=int(boundary['external_call_target'],16)
    site=int(boundary['external_call_site'],16)
    h_a0=int(boundary['a0'],16)
    h_a1=s32((int(boundary['s1'],16)&0xffffffff)<<2) if entry==0x113410 else 0
    ra=int(boundary['ra'],16)
    h_v0=int(boundary['v0'],16)
    h_v1=int(boundary['v1'],16)
    h_a2=int(boundary['a2'],16)
    sp=int(boundary['sp'],16)
    init,e1,e2,epc,old=snapshot
    translated=chain_oracle(chain_words,(entry,init,e1,e2,h_v0,h_v1,h_a2,
                    h_a0,h_a1,ra,sp,epc,old))
    return (site,entry,ra,h_a0,h_a1,h_v0,h_v1,h_a2,sp,*translated[1:])

def compare_native(exe,handler_words=None,chain_words=None):
    if chain_words is None:chain_words=synthetic_instructions()
    cases=list(bridge_cases(handler_words))
    mismatches=[];counts={'0x00113400':0,'0x00113410':0};returns=0;polls=0
    for ix,(c,snapshot,b) in enumerate(cases):
        args=[str(exe),c[0],*(hex(x) for x in c[1:]),*(hex(x) for x in snapshot)]
        p=subprocess.run(args,capture_output=True,text=True,timeout=15)
        if p.returncode or 'PHASE221_CHAIN ' not in p.stdout:
            raise ValueError(f'Case {c[0]} native failure rc={p.returncode}: {p.stderr[:160]}')
        fields=p.stdout.split('PHASE221_CHAIN ',1)[1].splitlines()[0].split()
        got=tuple(int(s,16) for s in fields)
        want=oracle(handler_words,chain_words,c,snapshot,b)
        if got!=want:mismatches.append({'case':c[0],'snapshot':ix%len(VIRTUAL_CPU_STATES),
                  'expected':list(map(hex,want)),'actual':list(map(hex,got))})
        counts[f'0x{want[1]:08X}']+=1
        if want[-5]==1:returns+=1 # shared stop code: modeled ERET
        if want[-5]==2:polls+=1
    return {'phase':221,'case_count':len(cases),'passed':not mismatches and len(cases)>0,
      'mismatches':mismatches[:5],'by_handler_external_target':counts,
      'scope':'Original handler bounded call sites -> two retail wrappers -> virtual shared helper',
      'guest_irq_handler_complete':False,'original_timer2_irq11_dispatch_observed':False,
      'hardware_timing_verified':False,'host_mmio_writes':0,
      'evidence':'retail opcode interpreter' if handler_words is not None else 'synthetic fixture oracle',
      'modeled_eret_cases':returns,'bounded_poll_stops':polls}
