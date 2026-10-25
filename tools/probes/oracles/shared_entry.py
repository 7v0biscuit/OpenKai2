"""Bounded SharedEntry model of retail shared routine at 0x00113380.

Never performs COP0 state changes, interrupts, I/O, or the real game function.
Native behavior is compared against an independent instruction interpreter and
pinned executable-owned instruction windows. No retail instruction bytes bundled.
"""
from __future__ import annotations
import hashlib,struct,subprocess
from .retail_elf import MipsELF,EXPECTED_SHA256,AuditError
from .tail_wrappers import MASK

ENTRY=0x00113380
WINDOW_SIZE=64
WINDOW_DIGEST='5dfa5d70b392a903c5635ea4b815e43cd60050f7429368cf82205748e096141e'
SEGMENT_DIGESTS={
    0x00113380:'ab779b3821a716b1255e191f1c28ba6f38bf9e9305f987b1bd2960a46288730c',
    0x001133bc:'d898278b56a2041bfe325426e6abefd6604e6749415654f6f70c8936f77a44b9',
}
STOP_BEFORE_EI=0x00113398
STOP_BEFORE_MTC0=0x001133d0

def _hash_words(words):
    return hashlib.sha256(b''.join(struct.pack('<I',w) for w in words)).hexdigest()

def validate_window(words):
    """All 64 instruction words must match the actual SHA-verified capture."""
    if len(words)!=WINDOW_SIZE or _hash_words(words)!=WINDOW_DIGEST:
        raise AuditError('Shared routine window digest/size mismatch')
    mapping={ENTRY+4*i:words[i] for i in range(WINDOW_SIZE)}
    for start,digest in SEGMENT_DIGESTS.items():
        if _hash_words([mapping[start+4*i] for i in range(5)])!=digest:
            raise AuditError('Translated shared routine segment digest mismatch')
    return mapping

def read_retail_window(blob):
    if hashlib.sha256(blob).hexdigest()!=EXPECTED_SHA256:
        raise AuditError('Unexpected retail ELF SHA-256')
    elf=MipsELF(blob)
    for i in range(WINDOW_SIZE):
        pc=ENTRY+4*i
        if not any(s['vaddr']<=pc and pc+4<=s['vaddr']+s['filesz'] and s['flags']&1 for s in elf.segments):
            raise AuditError('Shared window outside executable file-backed segment')
    return validate_window(tuple(elf.word(ENTRY+4*i) for i in range(WINDOW_SIZE)))

def synthesize_translated_words():
    """Encode supported instructions from ISA fields, not verbatim retail words."""
    def r(rs,rt,rd,fn):return (rs<<21)|(rt<<16)|(rd<<11)|fn
    def i(op,rs,rt,imm):return (op<<26)|(rs<<21)|(rt<<16)|(imm&0xffff)
    def mfc0(rt,rd):return (16<<26)|(rt<<16)|(rd<<11)
    w={
       0x113380:mfc0(3,12),
       0x113384:i(15,0,2,1),
       0x113388:r(3,2,3,36),
       0x11338c:i(4,3,0,11),
       0x113390:i(15,0,6,1),
       0x113394:0,
       0x1133bc:mfc0(2,12),
       0x1133c0:(16<<6)|15, # SYNC with stype=0x10, no guest hardware performed
       0x1133c4:i(13,2,2,6),
       0x1133c8:i(14,2,2,2),
       0x1133cc:r(2,3,2,37),
    }
    for start,digest in SEGMENT_DIGESTS.items():
        actual=_hash_words([w[start+4*i] for i in range(5)])
        if actual!=digest:raise AssertionError('ISA assembler mismatch at '+hex(start))
    return w

def scenarios():
    status_values=[0,1,2,4,6,0xffff,0x10000,0x10001,0x10006,0x10008,
       0xffff0000,0xffffffff,0x80000000,0x80010000,0x7fffffff,
       0xfffeffff,0x20000,0x30000,0xaaaa5555,0x5555aaaa,0x0000fffa]
    for idx,status in enumerate(status_values):
        for variant in range(3):
            seed=((idx+17)*0x9e3779b97f4a7c15+variant*0x123456789abced)&MASK
            x=[((seed>>(q%13))^(seed<<(q%11))^(0x1122334455667788*(q+1)))&MASK for q in range(7)]
            yield (status,*x)  # input registers v0, v1, a2, a0, a1, ra, sp

def signed32(x):
    x&=0xffffffff
    return x | (0xffffffff00000000 if x&0x80000000 else 0)

def interpret(words,scenario):
    """Independent per-word MIPS scalar/CP0-read interpreter, bounded before privileged ops."""
    cp0status,*vals=scenario
    regs=[0]*32
    for name,value in zip((2,3,6,4,5,31,29),vals):regs[name]=value&MASK
    pc=ENTRY; pending=None; syncs=0; visited=[]
    for _ in range(24):
        if pc in (STOP_BEFORE_EI,STOP_BEFORE_MTC0):
            if pending is not None:raise ValueError('Pending delay slot at a stop barrier')
            return (pc,regs[2],regs[3],regs[6],regs[4],regs[5],regs[31],regs[29],syncs,
                    1 if pc==STOP_BEFORE_EI else 2)
        if pc not in words:raise ValueError('Out-of-bound instruction '+hex(pc))
        w=words[pc];visited.append(pc)
        op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;fn=w&63;imm=w&0xffff
        next_pc=pc+4
        if op==16 and rs==0 and rd==12 and (w&7)==0: # MFC0 GPR,Status
            regs[rt]=signed32(cp0status)
        elif op==15 and rs==0:
            regs[rt]=signed32(imm<<16)
        elif op==0 and fn==36 and (w&0x7c0)==0:
            regs[rd]=regs[rs]&regs[rt]
        elif op==4:
            if pending is not None:raise ValueError('Branch in delay slot unsupported')
            displacement=imm-0x10000 if imm&0x8000 else imm
            pending=(pc+4+(displacement<<2)) if regs[rs]==regs[rt] else pc+8
        elif w==0:
            pass
        elif op==0 and fn==15: # SYNC: ordering fence, modeled event only
            syncs+=1
        elif op==13:
            regs[rt]=(regs[rs]|imm)&MASK
        elif op==14:
            regs[rt]=(regs[rs]^imm)&MASK
        elif op==0 and fn==37 and (w&0x7c0)==0:
            regs[rd]=(regs[rs]|regs[rt])&MASK
        else:raise ValueError('Privileged/unsupported opcode at '+hex(pc)+' word='+hex(w))
        regs[0]=0
        if pending is not None and op!=4:
            pc=pending;pending=None
        else:pc=next_pc
    raise ValueError('Instruction execution limit exceeded')

def compare_native(exe,words):
    cases=list(scenarios())
    payload=''.join('%08X '%case[0]+' '.join('%016X'%x for x in case[1:])+'\n' for case in cases)
    cp=subprocess.run([str(exe)],input=payload,text=True,capture_output=True,timeout=30)
    if cp.returncode:raise ValueError(f'Native shared probe rc={cp.returncode}: {cp.stderr[:250]}')
    output=cp.stdout.strip().splitlines()
    if len(output)!=len(cases):raise ValueError(f'Native count {len(output)} != expected {len(cases)}')
    errors=[];stops={1:0,2:0}
    for case,line in zip(cases,output):
        want=interpret(words,case)
        pieces=line.split()
        if len(pieces)!=10:raise ValueError('Invalid native shared output: '+repr(line[:130]))
        got=tuple(int(v,16) for v in pieces)
        if got!=want:errors.append({'cp0_status':hex(case[0]),'expected':[hex(x) for x in want], 'actual':[hex(x) for x in got]})
        stops[want[9]]+=1
    return {'passed':not errors,'case_count':len(cases),'stop_before_ei_cases':stops[1],
       'stop_before_mtc0_cases':stops[2],'failures':errors[:10],
       'privileged_instruction_executed':False,'hardware_register_written':False,
       'shared_function_complete':False,'game_playable':False}
