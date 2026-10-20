"""independent opcode-by-opcode MIPS EE list-walk interpreter.

Owned USA retail ELF identity and exact 27-word window digest must match
before any comparison is counted as retail-derived. Tests use tiny, virtual
aligned linked-node fixtures; no PS2 game/runtime/host-MMIO execution.
"""
from __future__ import annotations
import hashlib,struct,subprocess
from .retail_elf import EXPECTED_SHA256, MipsELF
M=(1<<64)-1
BASE=0x210040
START=0x1137f4
END=0x113860
WIN_SHA='e7e424288479bb43b5ceac2683a2d39d749072625ad42e16a8da863311047315'
WORDS=tuple(int(x,16) for x in '''
0xdcc20020 0x66037333 0xdcc40010 0xdcc50018 0x0044102d
0x0045102f 0x0043182b 0x10600010 0x32510003 0x0040802d
0x00000000 0x8cc60000 0x10c0000c 0x66037333 0xdcc20020
0xdcc40010 0xdcc50018 0x0044102d 0x0045102f 0x0043182b
0x50600005 0x0207102f 0x1000fff4 0x0040802d 0x32510003
0x0207102f 0x28427333
'''.split())
assert len(WORDS)==27
assert hashlib.sha256(b''.join(struct.pack('<I',w) for w in WORDS)).hexdigest()==WIN_SHA
SYNTHETIC={p:w for p,w in zip(range(START,END,4),WORDS)}

def checked_retail(blob):
    if hashlib.sha256(blob).hexdigest()!=EXPECTED_SHA256:raise ValueError('wrong USA game build')
    elf=MipsELF(blob)
    words={p:elf.word(p) for p in range(START,END,4)}
    raw=b''.join(struct.pack('<I',words[p]) for p in range(START,END,4))
    if hashlib.sha256(raw).hexdigest()!=WIN_SHA:raise ValueError('retail loop instruction digest mismatch')
    if words!=SYNTHETIC:raise ValueError('retail loop changed')
    return words

def s16(x):return x-65536 if x&0x8000 else x

def sext32(x):
    x&=0xffffffff
    return x | (M ^ 0xffffffff) if x & 0x80000000 else x

def nodes_for(code):
    # Format next pointer, +16 payload, +24 payload, +32 payload.
    z=BASE+0x40; t=BASE+0x80
    base=[z,0x10,0x20,0x210]
    second=[0,0x20,0x10,0x310]
    third=[0,0x40,0x30,0x410]
    if code==0:base=[0,0,0,0x9000]
    if code==1:base[0]=0
    if code==2:second=[0,0,0,0x9000]
    if code==3:second[0]=0
    if code==4:second[0]=t;third=[0,0,0,0x9000]
    if code==5:second[0]=t;third[0]=0
    if code==6:second[0]=z
    if code==7:second[0]=t;third[0]=z
    if code==8:base=[z,0xfffffffffffffff0,0x30,0x50];second[0]=0
    if code==9:base=[z,0,0,0x10];second=[t,0,0,0x20];third=[0,0,0,0x30]
    return tuple(base+second+third)

def cases():
    for code in range(10):
      for s0 in (0,0x100,0x7320,0x8000,0xffffffffffffe000,0xffffffffffffffff):
       for a3 in (0,0x100,0xffffffffffffffff):
        for s2 in (0,3):
         values=(s0,s2,a3,BASE,*nodes_for(code))
         yield (f'branch{code}_{s0:x}_{a3:x}_{s2:x}',*values)

def opcode_oracle(case,words=None):
    if words is None:words=SYNTHETIC
    label,s0,s2,a3,start,*node_words=case
    if len(node_words)!=12:raise ValueError('bad test fixture')
    n=[node_words[i:i+4] for i in range(0,12,4)]
    r=[0]*32;r[16]=s0;r[18]=s2;r[7]=a3;r[6]=start
    counters={'first_branch_exit':0,'null_link_exit':0,'likely_taken':0,'likely_annulled':0,'backedges':0,'loaded_nodes':0,'stop_code':0}
    def mem(address,length):
        low=address&0xffffffff
        if address!=sext32(low) or low<BASE or ((low-BASE)//0x40)>=3:
            raise ValueError('guest memory outside bounded virtual node region')
        idx=(low-BASE)//0x40;offset=(low-BASE)%0x40
        if length==8 and offset in (16,24,32):
            if offset==32:counters['loaded_nodes']+=1
            return n[idx][{16:1,24:2,32:3}[offset]]
        if length==4 and offset==0:return sext32(n[idx][0])
        raise ValueError('bad virtual node field')
    pc=START
    # Generic decoder dispatch. Delay slots are separately decoded, not
    # simulated by the native C++ control-flow implementation.
    def step(pc):
        w=words[pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;im=w&65535;fn=w&63
        if op==0:
            if w==0:pass
            elif fn==0x2d:r[rd]=(r[rs]+r[rt])&M
            elif fn==0x2f:r[rd]=(r[rs]-r[rt])&M
            elif fn==0x2b:r[rd]=int(r[rs]<r[rt])
            else:raise ValueError(f'unexpected R function {fn:#x} at {pc:#x}')
        elif op==0x19:r[rt]=(r[rs]+s16(im))&M # DADDIU, 64-bit
        elif op==0x37:r[rt]=mem((r[rs]+s16(im))&M,8) # LD
        elif op==0x23:r[rt]=mem((r[rs]+s16(im))&M,4) # LW signed
        elif op==0x0c:r[rt]=r[rs]&im # ANDI, unsigned imm
        elif op==0x0a:
            val=r[rs]-(1<<64) if r[rs]>>63 else r[rs]
            r[rt]=int(val<s16(im)) # SLTI signed 64-bit
        else:raise ValueError(f'unexpected non-control opcode {op:#x} at {pc:#x}')
        r[0]=0
    for tick in range(250):
        if pc==END:
            counters['stop_code']=1
            break
        if pc not in words:raise ValueError(f'PC outside expected 27-word window {pc:#x}')
        w=words[pc];op=w>>26
        if op in (4,0x14): # BEQ or BEQL, with true MIPS branch-delay semantics
            rs=(w>>21)&31;rt=(w>>16)&31
            taken=r[rs]==r[rt]
            if pc==0x113810 and taken:counters['first_branch_exit']=1
            if pc==0x113824 and taken:counters['null_link_exit']=1
            if pc==0x113844:
                counters['likely_taken']+=int(taken)
                counters['likely_annulled']+=int(not taken)
            if op==4 or taken:step(pc+4)
            target=(pc+4+s16(w&65535)*4) if taken else pc+8
            if pc==0x11384c:
                counters['backedges']+=1
                if counters['backedges']>=4:
                    pc=0x113820;counters['stop_code']=2;break
            pc=target
        else:
            step(pc);pc+=4
    else:raise ValueError('oracle traversal ran past safety instruction budget')
    if counters['stop_code']==0:raise ValueError('oracle failed to stop')
    return (pc,r[2],r[3],r[4],r[5],r[6],r[7],r[16],r[17],r[18],
            counters['first_branch_exit'],counters['null_link_exit'],
            counters['likely_taken'],counters['likely_annulled'],counters['backedges'],
            counters['loaded_nodes'],counters['stop_code'])

def compare_native(exe,words=None):
    if words is None:words=SYNTHETIC
    cases_all=list(cases())
    payload=''.join(' '.join((case[0],*(hex(x) for x in case[1:])))+'\n' for case in cases_all)
    proc=subprocess.run([str(exe)],input=payload,capture_output=True,text=True,timeout=120)
    if proc.returncode:raise ValueError('ListWalk native error: '+proc.stderr[:320])
    lines=proc.stdout.splitlines()
    if len(lines)!=len(cases_all):raise ValueError(f'native outputs {len(lines)} expected {len(cases_all)}')
    mismatches=[];outcomes={'first_branch_exit':0,'null_link_exit':0,'likely_taken':0,'likely_annulled':0,'bounded_loop':0,'threshold':0}
    for idx,(case,line) in enumerate(zip(cases_all,lines)):
        tokens=line.split()
        if len(tokens)!=18 or tokens[0]!='LIST_WALK':raise ValueError(f'malformed output case {idx}: {line[:100]}')
        got=tuple(int(s,16) for s in tokens[1:]);want=opcode_oracle(case,words)
        if got!=want:mismatches.append({'case':case[0],'expected':list(map(hex,want)), 'observed':list(map(hex,got))})
        for k,i in [('first_branch_exit',10),('null_link_exit',11),('likely_taken',12),('likely_annulled',13)]:outcomes[k]+=int(want[i]>0)
        outcomes['bounded_loop']+=int(want[-1]==2)
        outcomes['threshold']+=int(want[-1]==1)
    return {'component':'list_walk','case_count':len(cases_all),'passed':not mismatches,
            'mismatches':mismatches[:8], 'outcomes':outcomes,
            'first_pc':'0x001137F4','end_pc_exclusive':'0x00113860',
            'source_window_sha256':WIN_SHA,'instruction_count':len(WORDS),
            'reference':'verified_retail_opcode_interpreter' if words is not SYNTHETIC else 'independent_synthetic_opcode_interpreter',
            'hardware':'virtual_memory_only','complete_original_handler':False,'game_booted':False}
