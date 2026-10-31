"""independently decode the pinned retail MIPS shared helper through ERET.

All CP0, MMIO and ERET effects are local state. Fixture-supplied EI transitions
ARE NOT an emulator implementation of actual PlayStation 2 EI behavior.
The privileged instructions are decoded and compared with a C++ state model,
never executed on the host; return handling assumes conventional MIPS ERET.
"""
from __future__ import annotations
import subprocess
from .virtual_cp0 import synthetic_words as cp0_words, checked_retail as cp0_retail,scenarios as previous_cases
from .shared_entry import read_retail_window
from .tail_wrappers import MASK

ENTRY=0x00113380
MMIO_ADDRS=(0xFFFFFFFFB0001000,0xFFFFFFFFB0001010,0xFFFFFFFFB0001020)
INVALID_ADDRS=(0xFFFFFFFFB0001030,0xFFFFFFFFB0001012)
STOP_ERET=1
STOP_EI_LIMIT=2
STOP_INVALID_MMIO=3

def synthetic_words():
    words=cp0_words()
    words.update({
        0x1133dc:(16<<26)|(4<<21)|(31<<16)|(30<<11),
        0x1133e0:(16<<6)|15,
        0x1133e4:(16<<26)|(16<<21)|0x18,
        0x1133e8:(31<<21)|8,
        0x1133ec:0,
    })
    assert len(words)==28
    return words

def checked_retail(blob):
    retail=read_retail_window(blob)
    model=synthetic_words()
    for pc,w in model.items():
        if retail[pc]!=w:
            raise ValueError('MmioEret translated opcode differs from retail at '+hex(pc))
    return {pc:retail[pc] for pc in model}

def scenarios():
    for i,previous in enumerate(previous_cases()):
        for variant,address in enumerate(MMIO_ADDRS+INVALID_ADDRS):
            init,e1,e2,v0,v1,a2,_a0,a1,ra,sp=previous
            mix=(i*0x9e3779b97f4a7c15+variant*0x123456789abcdef)&MASK
            ra=(ra^mix)&MASK
            epc=((mix>>8)^(0x80001234+variant*0x123))&0xffffffff
            old_error_epc=((mix>>24)^0x40506070)&0xffffffff
            yield(init,e1,e2,v0,v1,a2,address,a1,ra,sp,epc,old_error_epc)

def sx32(v):
    v&=0xffffffff
    return (v|0xffffffff00000000) if v&0x80000000 else v

def oracle(words,case):
    """Execute actual 28 pinned ISA words (no native helper code reuse)."""
    init,e1,e2,v0,v1,a2,a0,a1,ra,sp,epc,old_error_epc=case
    regs=[0]*32
    for reg,value in zip((2,3,6,4,5,31,29),(v0,v1,a2,a0,a1,ra,sp)):
        regs[reg]=value&MASK
    cp0={12:init&0xffffffff,14:epc&0xffffffff,30:old_error_epc&0xffffffff}
    pc=ENTRY
    pending=None
    reads=eis=status_writes=syncs=virtual_stores=eret_count=error_epc_writes=0
    trace=store_addr=store_value=0
    def event(code):
        nonlocal trace
        trace=((trace<<4)|code)&MASK
    def output(stop):
        return (pc,cp0[12],cp0[30],cp0[14],
            *(regs[r] for r in (2,3,6,4,5,31,29)),
            reads,eis,status_writes,syncs,virtual_stores,trace,stop,
            store_addr,store_value,eret_count,error_epc_writes)
    for step in range(80):
        if pc==0x113398 and eis==2:
            if pending is not None:raise ValueError('EI guard inside branch delay slot')
            return output(STOP_EI_LIMIT)
        if pc not in words:
            raise ValueError('Unmapped instruction '+hex(pc))
        w=words[pc]
        op=(w>>26)&63;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31
        imm=w&0xffff;funct=w&63
        if pc==0x1133d8 and (regs[rs]+((imm-65536) if imm&0x8000 else imm))&MASK not in MMIO_ADDRS:
            if pending is not None:raise ValueError('MMIO inside branch delay slot')
            return output(STOP_INVALID_MMIO)
        old_pending=pending;pending=None
        if op==16 and rs==0 and rd==12:
            regs[rt]=sx32(cp0[12]);reads+=1;event(1)
        elif op==15 and rs==0:regs[rt]=sx32(imm<<16)
        elif op==0 and funct==36:regs[rd]=regs[rs]&regs[rt]
        elif op==0 and funct==37:regs[rd]=(regs[rs]|regs[rt])&MASK
        elif op in (4,5):
            if old_pending is not None:raise ValueError('Nested branch delay slot')
            cond=regs[rs]==regs[rt]
            if op==5:cond=not cond
            offset=((imm-65536) if imm&0x8000 else imm)<<2
            pending=pc+4+offset if cond else pc+8
        elif w==0:pass
        elif w==0x42000039:
            if eis>=2:raise ValueError('Unbounded EI scenario')
            cp0[12]=((e1 if eis==0 else e2)&0xffffffff)
            eis+=1;event(2)
        elif op==0 and funct==15:syncs+=1;event(3)
        elif op==13:regs[rt]=(regs[rs]|imm)&MASK
        elif op==14:regs[rt]=(regs[rs]^imm)&MASK
        elif op==16 and rs==4 and rd==12:
            cp0[12]=regs[rt]&0xffffffff;status_writes+=1;event(4)
        elif op==43:
            store_addr=(regs[rs]+((imm-65536) if imm&0x8000 else imm))&MASK
            store_value=regs[rt]&0xffffffff
            virtual_stores+=1;event(5)
        elif op==16 and rs==4 and rd==30:
            cp0[30]=regs[rt]&0xffffffff;error_epc_writes+=1;event(6)
        elif w==0x42000018:
            if old_pending is not None:raise ValueError('ERET in branch delay slot')
            eret_count+=1;event(7)
            if cp0[12]&4:pc=cp0[30];cp0[12]&=~4
            else:pc=cp0[14];cp0[12]&=~2
            return output(STOP_ERET)
        else:raise ValueError('Unimplemented opcode '+hex(w)+' at '+hex(pc))
        regs[0]=0
        pc=old_pending if old_pending is not None else pc+4
    raise ValueError('Instruction step cap exceeded')

def compare_native(exe,words):
    cases=list(scenarios())
    payload=''.join(' '.join([*(f'{v:08X}' for v in c[:3]),*(f'{v:016X}' for v in c[3:10]),*(f'{v:08X}' for v in c[10:])])+'\n' for c in cases)
    p=subprocess.run([str(exe)],input=payload,text=True,capture_output=True,timeout=45)
    if p.returncode:raise ValueError(f'native rc {p.returncode}: {p.stderr[:250]}')
    lines=p.stdout.splitlines()
    if len(lines)!=len(cases):raise ValueError(f'Output count {len(lines)} != {len(cases)}')
    failures=[];stops={1:0,2:0,3:0};virtual_addresses=set();stores=0
    for idx,(case,line) in enumerate(zip(cases,lines)):
        pieces=line.split()
        if len(pieces)!=22:raise ValueError(f'Output field count {len(pieces)} at {idx} != 22')
        got=tuple(int(p,16) for p in pieces)
        expected=oracle(words,case)
        if got!=expected:
            failures.append({'case':idx,'initial_status':hex(case[0]),'mmio_addr':hex(case[6]),
                             'expected':[hex(v) for v in expected], 'got':[hex(v) for v in got]})
        stops[expected[17]]+=1
        stores+=expected[15]
        if expected[15]:virtual_addresses.add(expected[18])
    return {'passed':not failures,'case_count':len(cases),'failures':failures[:10],
        'returned_through_virtual_eret':stops[1],
        'stopped_before_third_virtual_ei':stops[2],
        'rejected_unmapped_or_unaligned_virtual_store':stops[3],
        'virtual_store_count':stores,'distinct_virtual_store_addresses':[f'0x{x:016X}' for x in sorted(virtual_addresses)],
        'virtual_mmio_only':True,'virtual_cp0_only':True,'actual_guest_cp0_eret_semantics_verified':False,
        'host_privileged_operations_executed':False,'host_hardware_writes':0,
        'shared_function_integrated':False,'game_playable':False}
