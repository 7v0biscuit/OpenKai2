"""Independent retail-word list interpreter with a bounded guest address map.

This is an explicitly synthetic sandbox with low physical fixture regions and
KSEG0/KSEG1 direct-map aliases, NOT a PS2 TLB or running CPU. Original list MIPS
opcodes are decoded, rather than importing the MappedGuestMemory native translation.
"""
from __future__ import annotations
import subprocess
from .live_guest_memory import cases as base_cases
from .list_walk import SYNTHETIC, BASE
from .retail_elf import EXPECTED_SHA256
MASK=(1<<64)-1

def sx32(value):
 value &= 0xffffffff
 return value | 0xffffffff00000000 if value&0x80000000 else value

def aliased(value,mode,index):
 lo=value&0xffffffff
 if not lo:return 0
 if lo>=0x20000000:raise ValueError('fixture bad raw pointer')
 route=(index%2)+1 if mode==3 else mode
 return lo | (0x80000000 if route==1 else 0xa0000000 if route==2 else 0)

def phys(addr,width):
 if width not in (1,2,4,8):raise Fault(4)
 if addr&(width-1):raise Fault(3)
 if addr<0 or addr+width>1<<32:raise Fault(4)
 resolved=[]
 for i in range(width):
  p=addr+i
  if 0x80000000<=p<0xc0000000:p &= 0x1fffffff
  elif p>=0x20000000:raise Fault(4)
  allowed=(0x1ffe00<=p<0x200300 or
     0x33b288<=p<0x33b308 or
     0x210040<=p<0x210100 or
     0x10001000<=p<0x10001004 or
     0x10001010<=p<0x10001014)
  if not allowed:raise Fault(4)
  resolved.append(p)
 return resolved

class Fault(Exception):
 def __init__(self,code):self.code=code

class GuestMemory:
 def __init__(self):self.mem={}
 def read(self,addr,width):
  resolved=phys(addr,width)
  return sum((self.mem.get(a,0xa5 if 0x1ffe00<=a<0x200300 else 0)<<(i*8)) for i,a in enumerate(resolved))
 def write(self,addr,width,value):
  resolved=phys(addr,width)
  for i,a in enumerate(resolved):self.mem[a]=(value>>(i*8))&255

def cases():
 seen=set()
 for c in base_cases():
  label=c[0]
  code=label.split('_')[0]
  gate=c[1]
  patch=label.rsplit('_mem',1)[1]
  key=(code,gate,patch)
  if key in seen:continue
  seen.add(key)
  for mode in range(4):
   for fault in range(6):
    yield (*c,mode,fault)

def opcode_walk(s0,s2,a3,head,guest,words):
 r=[0]*32;r[16]=s0;r[18]=s2;r[7]=a3;r[6]=head
 loaded=0;calls=0;byte_count=0
 counter=dict(first_branch_exit=0,null_link_exit=0,likely_taken=0,
   likely_annulled=0,backedges=0,loaded_nodes=0,stop_code=0)
 def sign16(v):return v-(1<<16) if v&0x8000 else v
 def step(pc):
  nonlocal loaded,calls,byte_count
  w=words[pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;imm=w&0xffff;fun=w&63
  if op==0:
   if w==0:pass
   elif fun==0x2d:r[rd]=(r[rs]+r[rt])&MASK
   elif fun==0x2f:r[rd]=(r[rs]-r[rt])&MASK
   elif fun==0x2b:r[rd]=int(r[rs]<r[rt])
   else:raise ValueError('unexpected retail R opcode')
  elif op==0x19:r[rt]=(r[rs]+sign16(imm))&MASK
  elif op in (0x37,0x23):
   address=(r[rs]+sign16(imm))&MASK
   if address!=sx32(address):raise Fault(5)
   n=8 if op==0x37 else 4
   value=guest.read(address&0xffffffff,n)
   calls+=1;byte_count+=n
   if op==0x23:value=sx32(value)
   if pc in (0x1137f4,0x113830):loaded+=1
   r[rt]=value
  elif op==0xc:r[rt]=r[rs]&imm
  elif op==0xa:
   value=r[rs]-(1<<64) if r[rs]>>63 else r[rs]
   r[rt]=int(value<sign16(imm))
  else:raise ValueError('unexpected opcode')
  r[0]=0
 pc=0x1137f4
 for _ in range(250):
  if pc==0x113860:
   counter['stop_code']=1
   break
  if pc not in words:raise ValueError('retail PC out of bounds')
  w=words[pc];op=w>>26
  if op in (4,0x14):
   take=r[(w>>21)&31]==r[(w>>16)&31]
   if pc==0x113810 and take:counter['first_branch_exit']=1
   if pc==0x113824 and take:counter['null_link_exit']=1
   if pc==0x113844:
    counter['likely_taken']+=int(take)
    counter['likely_annulled']+=int(not take)
   if op==4 or take:step(pc+4)
   target=pc+4+sign16(w&0xffff)*4 if take else pc+8
   if pc==0x11384c:
    counter['backedges']+=1
    if counter['backedges']>=4:
     pc=0x113820;counter['stop_code']=2;break
   pc=target
  else:step(pc);pc+=4
 else:raise ValueError('reference loop bound exceeded')
 if counter['stop_code']==0:raise ValueError('no modeled stop')
 counter['loaded_nodes']=loaded
 return ((pc,r[2],r[3],r[4],r[5],r[6],r[7],r[16],r[17],r[18],
    counter['first_branch_exit'],counter['null_link_exit'],counter['likely_taken'],
    counter['likely_annulled'],counter['backedges'],counter['loaded_nodes'],counter['stop_code']),calls,byte_count)

def expected(c,words=None):
 if words is None:words=SYNTHETIC
 label,*args=c
 x=args[:33];node,field,patchval=args[33:36];mode,fault=args[36:]
 flags=x[0];dispatch=int(flags&7==7)
 zero=(0,)*17
 if not dispatch:return (flags,mode,fault,0,0,0,*zero,0,0,0)
 entered=0
 try:
  g=GuestMemory()
  g.write(0x33b288+28,4,x[7]);head=aliased(x[8],mode,0)
  if fault==1:head|=1
  elif fault==2:head=0x70001000
  elif fault==3:head=0xc0210080
  g.write(0x33b288+24,4,head)
  g.write(0x10001010,4,x[9]);g.write(0x10001000,4,x[10]);
  for i in range(3):
   addr=BASE+0x40*i;j=11+4*i
   pointer=aliased(x[j],mode,i+1)
   if i==0 and fault==4:pointer=0x00400000
   g.write(addr,4,pointer)
   for off,val in ((16,x[j+1]),(24,x[j+2]),(32,x[j+3])):g.write(addr+off,8,val)
  g.write(sx32(x[1]-64)&0xffffffff,8,x[3])
  g.write((sx32(x[1]-64)+16)&0xffffffff,8,x[4])
  g.write((sx32(x[1]-64)+32)&0xffffffff,8,x[5])
  g.write((sx32(x[1]-64)+48)&0xffffffff,8,x[2])
  a3=x[6];s2=sx32(g.read(0x10001010,4))
  first=sx32(g.read(0x33b288+24,4))
  if not first:raise ValueError('expected list prefix nonzero')
  if (first&0xffffffff)+32>0xffffffff:raise Fault(4)
  s0=(g.read(((first&0xffffffff)+32),8)+g.read((first&0xffffffff)+16,8)-g.read((first&0xffffffff)+24,8))&MASK
  start=sx32(g.read(first&0xffffffff,4))
  if not start:raise ValueError('fixture must enter linked-list continuation')
  entered=1
  if node:
   off={0:0,1:16,2:24,3:32}[field]
   patchaddr=(BASE+0x40*node)|(0x80000000 if mode==2 else 0xa0000000)
   g.write(patchaddr+off,4 if off==0 else 8,patchval)
  if fault==5:start=0x0000000080210080
  out,loads,byte_count=opcode_walk(s0,s2,a3,start,g,words)
  code=2 if out[-1]==2 else 1
  target=0 if code==2 else (0x113410 if out[1] else 0x113400)
  return (flags,mode,fault,1,code,entered,*out,loads,byte_count,target)
 except Fault as exc:
  return (flags,mode,fault,1,exc.code,entered,*zero,0,0,0)

def memory_cases():
 addr=[0x210040,0x210041,0x210044,0x210048,0x2100ff,0x210100,
   0x2002fc,0x200300,0x33b2a0,0x10001010,0x70000000,0xc0210040]
 for i,a in enumerate(addr):
  for mode in range(3):
   base=a
   if a<0x20000000 and mode:base=a|(0x80000000 if mode==1 else 0xa0000000)
   for width in (1,2,4,8):
    alias=a if a<0x20000000 else base
    if a<0x20000000 and mode==0:alias=a|0x80000000
    yield ('MEM',base,width,0x8899aabbccddeeff,alias,i*13+mode)

def memory_expected(c):
 _,address,width,value,alias,seed=c
 g=GuestMemory();g.write(BASE,4,0x44332211)
 before=g.read(BASE,4)
 code=0;readback=0
 try:
  g.write(address,width,value)
  readback=g.read(alias,width)
 except Fault as exc:code=exc.code
 return (code,readback,g.read(BASE,4),before,seed)

def compare_native(exe,words=None):
 if words is None:words=SYNTHETIC
 cc=list(cases());mm=list(memory_cases())
 payload=''.join(' '.join((c[0],*(hex(v) for v in c[1:])))+'\n' for c in cc+mm)
 proc=subprocess.run([str(exe)],input=payload,text=True,capture_output=True,timeout=240)
 if proc.returncode:raise ValueError('MappedGuestMemory C++ probe failed: '+proc.stderr[:320])
 rows=proc.stdout.splitlines()
 if len(rows)!=len(cc)+len(mm):raise ValueError(f'MappedGuestMemory row mismatch {len(rows)} != {len(cc)+len(mm)}')
 issues=[];states={};alias_checks=0;faults=0;entered=0
 for c,line in zip(cc,rows[:len(cc)]):
  t=line.split()
  if len(t)!=27 or t[0]!='MAPPED_GUEST_MEMORY':raise ValueError('bad MappedGuestMemory native line '+line[:120])
  got=tuple(int(s,16) for s in t[1:]);want=expected(c,words)
  if got!=want:issues.append({'case':c[0], 'mode':c[-2], 'fault':c[-1], 'expected':list(map(hex,want)),'actual':list(map(hex,got))})
  states[want[4]]=states.get(want[4],0)+1
  entered+=want[5]
  alias_checks+=int(c[-2]!=0 and want[4] in (1,2))
  faults+=int(want[4] in (3,4,5))
 for c,line in zip(mm,rows[len(cc):]):
  t=line.split()
  if len(t)!=6 or t[0]!='MAPPED_GUEST_MEMORY_MEM':raise ValueError('bad MappedGuestMemory MEM native output '+line[:120])
  got=tuple(int(s,16) for s in t[1:]);want=memory_expected(c)
  if got!=want:issues.append({'case':'MEM_'+str(c[1:3]),'expected':list(map(hex,want)),'actual':list(map(hex,got))})
 return {'component':'mapped_guest_memory','case_count':len(cc),'memory_access_cases':len(mm),'passed':not issues,
  'mismatches':issues[:8],'status_code_counts':{str(k):v for k,v in sorted(states.items())},
  'successfully_mapped_alias_traversals':alias_checks,'detected_virtual_address_faults':faults,
  'original_list_entered':entered,'source_identity': 'SHA-pinned retail list instruction window' if words is not SYNTHETIC else 'synthetic list opcode model',
  'guest_address_map':'bounded physical fixtures and 32-bit direct-map KSEG0/KSEG1, no TLB',
  'guest_registered':False,'real_hardware_accesses':0,'game_booted':False}
