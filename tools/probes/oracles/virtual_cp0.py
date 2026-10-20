"""virtual COP0 differential oracle for exact retail code at 0x113380.

Virtual EI outputs are *supplied by each fixture*, not claims about EE EI/DI
hardware semantics. MTC0 writes a local 32-bit register. All MMIO is blocked.
Python runs the actual pinned ISA words; C++ is an independent high-level model.
"""
from __future__ import annotations
import subprocess
from .shared_entry import read_retail_window,synthesize_translated_words,WINDOW_DIGEST
from .tail_wrappers import MASK
ENTRY=0x113380
LAST=0x1133d8
INTERPRETED_PCS=tuple(range(ENTRY,LAST+4,4))

def enc_i(op,rs,rt,imm):return (op<<26)|(rs<<21)|(rt<<16)|(imm&0xffff)
def enc_r(rs,rt,rd,fn):return (rs<<21)|(rt<<16)|(rd<<11)|fn
def cp0_move(rs,rt,rd):return (16<<26)|(rs<<21)|(rt<<16)|(rd<<11)

def synthetic_words():
 d=dict(synthesize_translated_words())
 d.update({
  0x113394:0,
  0x113398:(16<<26)|(16<<21)|0x39, # EI, modeled as external transition
  0x11339c:(16<<6)|15, # SYNC, event only
  0x1133a0:cp0_move(0,2,12),
  0x1133a4:enc_r(2,6,2,36),
  0x1133a8:0,0x1133ac:0,0x1133b0:0,
  0x1133b4:enc_i(5,2,0,-8), # bne followed by NOP
  0x1133b8:0,
  0x1133d0:cp0_move(4,2,12),
  0x1133d4:(16<<6)|15,
  0x1133d8:enc_i(43,4,5,0), # store barrier, never executed
 })
 assert set(d)==set(INTERPRETED_PCS)
 return d

def checked_retail(blob):
 words=read_retail_window(blob) # verifies SHA, full 64-word window hash
 expected=synthetic_words()
 for pc,word in expected.items():
  if words[pc]!=word:raise ValueError('VirtualCp0 pin differs from retail at '+hex(pc))
 return {pc:words[pc] for pc in expected}

def scenarios():
 initial=[0,1,2,0xfffeffff,0x20000,0x10000,0x10001,0x10006,
  0x80010000,0xffff0000,0xffffffff,0x30000,0x55555555,0xaaaa5555]
 changes=[(0,0),(0x10000,0),(0x10000,0x10000),(0x10001,0),(0,0x10000),
   (0x20000,0x10000),(0xffffffff,0x7fffffff),(0x10000,0x20000)]
 for i,s in enumerate(initial):
  for j,(a,b) in enumerate(changes):
   seed=(i*UINT_SEED+j*0x125789abcdef+0x1122334455667788)&MASK
   vals=tuple(((seed<<((k+1)%17))^(seed>>((k+2)%13))^(0x9e3779b97f4a7c15*(k+1)))&MASK for k in range(7))
   yield (s,a,b,*vals)
UINT_SEED=0x9e3779b97f4a7c15

def sx32(v):
 v&=0xffffffff
 return (v|(0xffffffff00000000 if v&0x80000000 else 0))

def oracle(words,case):
 init,after1,after2,*vals=case
 regs=[0]*32
 for idx,value in zip((2,3,6,4,5,31,29),vals):regs[idx]=value&MASK
 cp0=init&0xffffffff; pc=ENTRY
 pending=None;reads=eis=mtc0s=syncs=0;events=0
 def event(code):
  nonlocal events
  events=((events<<4)|code)&MASK
 for _ in range(70):
  if pc==0x1133d8:
   if pending is not None:raise ValueError('Hardware barrier in delay slot')
   return (pc,cp0,*(regs[x] for x in (2,3,6,4,5,31,29)),reads,eis,mtc0s,syncs,0,events,1)
  if pc==0x113398 and eis==2:
   if pending is not None:raise ValueError('Pending delay at EI guard')
   return (pc,cp0,*(regs[x] for x in (2,3,6,4,5,31,29)),reads,eis,mtc0s,syncs,0,events,2)
  if pc not in words:raise ValueError('Unmapped instruction at '+hex(pc))
  w=words[pc]; op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;imm=w&65535;fn=w&63
  old_pending=pending;pending=None
  if op==16 and rs==0 and rd==12:
   regs[rt]=sx32(cp0);reads+=1;event(1)
  elif op==15 and rs==0:regs[rt]=sx32(imm<<16)
  elif op==0 and fn==36:regs[rd]=regs[rs]&regs[rt]
  elif op==0 and fn==37:regs[rd]=(regs[rs]|regs[rt])&MASK
  elif op==4 or op==5:
   if old_pending is not None:raise ValueError('Branch in delay slot')
   offset=(imm-65536 if imm&32768 else imm)<<2
   cond=(regs[rs]==regs[rt]); cond=cond if op==4 else not cond
   pending=pc+4+offset if cond else pc+8
  elif w==0:pass
  elif op==16 and w==0x42000039:
   if eis>=2:raise ValueError('EI count guard')
   cp0=(after1 if eis==0 else after2)&0xffffffff
   eis+=1;event(2)
  elif op==0 and fn==15:
   syncs+=1;event(3)
  elif op==13:regs[rt]=(regs[rs]|imm)&MASK
  elif op==14:regs[rt]=(regs[rs]^imm)&MASK
  elif op==16 and rs==4 and rd==12:
   cp0=regs[rt]&0xffffffff;mtc0s+=1;event(4)
  else:raise ValueError('Unimplemented ISA word '+hex(w)+' at '+hex(pc))
  regs[0]=0
  if old_pending is not None:
   if pending is not None:raise ValueError('Nested delay slot')
   pc=old_pending
  else:pc+=4
 raise ValueError('Instruction limit exceeded')

def compare_native(exe,words):
 tests=list(scenarios());lines=[]
 for x in tests:
  lines.append(' '.join([*(f'{v:08X}' for v in x[:3]),*(f'{v:016X}' for v in x[3:])]))
 cp=subprocess.run([str(exe)],input='\n'.join(lines)+'\n',text=True,capture_output=True,timeout=45)
 if cp.returncode:raise ValueError('native subprocess rc '+str(cp.returncode)+' '+cp.stderr[:200])
 results=cp.stdout.strip().splitlines()
 if len(results)!=len(tests):raise ValueError(f'Result count {len(results)} != {len(tests)}')
 errors=[];stops={1:0,2:0};counts={'ei0':0,'ei1':0,'ei2':0}
 for case,line in zip(tests,results):
  fields=line.split()
  if len(fields)!=16:raise ValueError('Wrong native output format '+repr(line[:130]))
  got=tuple(int(v,16) for v in fields)
  want=oracle(words,case)
  if got!=want:errors.append({'input':[hex(v) for v in case[:3]],'expected':[hex(v) for v in want],'actual':[hex(v) for v in got]})
  stops[want[-1]]+=1;counts['ei'+str(want[10])]+=1
 return {'passed':not errors,'case_count':len(tests),'failures':errors[:10],
  'stopped_before_mmio_write':stops[1],'stopped_before_third_ei':stops[2],
  'ei_distribution':counts,'virtual_cp0_status_writes_only':True,
  'real_privileged_instructions_executed':False,'real_hardware_writes':0,
  'full_helper_translated':False,'game_booted':False}
