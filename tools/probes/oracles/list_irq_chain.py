"""Independent original-handler entry/list/threshold/original helpers chain.

Reference composes instruction-word-interpreted MIPS linked list and original
post-JAL windows with validated original threshold JAL encoding. CPU/CP0/MMIO
are virtual. This tests an isolated native C++ integration, NOT game runtime.
"""
from __future__ import annotations
import subprocess
from .list_walk import nodes_for,BASE,opcode_oracle,SYNTHETIC as LIST_SYNTHETIC,checked_retail as checked_list
from .helper_continuation import oracle as double_helper_oracle,synthetic_post_words,validate_post_words
from .wrapper_chain import synthetic_instructions
from .handler_bridge import VIRTUAL_CPU_STATES
from .entry_oracle import s32
from .helper_boundary_oracle import verified_rows
from .return_threshold_oracle import verified_rows as threshold_retail
from .tail_wrappers import MASK

FIRST_STATES=(VIRTUAL_CPU_STATES[0],VIRTUAL_CPU_STATES[3],VIRTUAL_CPU_STATES[4])
SECOND_STATES=((0,0,0,0x8000beef,0x13579bdf),
               (0x10000,0x10000,0x10000,0x8000beef,0x2468ace0))
GATE_FLAGS=(0,7,15)
A0_VALUES=(0,0xffffffffffff8000)

def cases():
  for code in range(10):
   for a0 in A0_VALUES:
    for first in FIRST_STATES:
     for flags in GATE_FLAGS:
      for second in SECOND_STATES:
       base=list(nodes_for(code))
       # First retail prelude consumes node0; traversal starts at node1.
       head=(BASE+0x40,0x10,0x20,0x100)
       nodes=tuple(head)+tuple(base[4:])
       args=(flags,0x200080,0x80001234,0x12345678,0x54321,0x98765432,
             a0,0xffffffff,BASE,0x47,0x12345678,*nodes,*first,*second)
       assert len(args)==33
       yield (f'linked{code}_a{a0:x}_i{first[0]:x}_{first[1]:x}_j{second[0]:x}_g{flags}',*args)

def expected(case,list_words=None,chain_words=None,post_words=None,handler_words=None):
 label,flags,sp,ra,s0,s1,s2,a0,g28,g24,mmio,counter,*remaining=case
 nodes=remaining[:12];first=remaining[12:17];second=remaining[17:22]
 assert len(nodes)==12 and len(first)==len(second)==5
 dispatch=int(bool(flags&1) and bool(flags&2) and bool(flags&4))
 pending=int(bool(flags&1) and not(dispatch and bool(flags&8)))
 zero=(0,)*17
 chain_zero=(0,)*24
 if not dispatch:return (flags,0,pending,0,*zero,*chain_zero,0)
 if g28!=0xffffffff or g24!=BASE or nodes[0]!=BASE+0x40:
  raise ValueError('fixture failed original entry path preconditions')
 # Execute original prelude register/memory effects (113770...1137F0).
 saved_sp=s32(sp-64)
 first_node=nodes[0:4]
 after_pre=(first_node[3]+first_node[1]-first_node[2])&MASK
 s2_after=s32(mmio)
 a2_after=s32(nodes[0])
 walk_case=(label,after_pre,s2_after,a0,a2_after,*nodes)
 list_output=opcode_oracle(walk_case,list_words)
 if list_output[-1]==2:return (flags,1,pending,1,*list_output,*chain_zero,0)
 if list_output[0]!=0x113860 or list_output[-1]!=1:
  raise ValueError('expected bounded traversal to terminate at threshold decision')
 v0,v1,v4,v5,v6,v7,walk_s0,walk_s1,walk_s2=list_output[1:10]
 # Original threshold BEQ + unconditional LUI delay slot, then original JAL.
 # Assert the expected opcode properties when the owned retail report is used.
 if handler_words is not None:
  branch=handler_words[0x113860];lui=handler_words[0x113864]
  if (branch>>26)!=4 or (branch>>21)&31!=2 or (branch>>16)&31 or (lui>>26)!=15:
   raise ValueError('retail threshold branch or delay slot changed')
  if handler_words[0x1138b8]>>26!=3 or handler_words[0x113884]>>26!=3:
   raise ValueError('retail call sites not JAL')
 if v0==0:
  target=0x113400;site=0x1138b8;jal_ra=0x1138c0
  helper_a0=s2_after&0xfffffffffffff7ff
  helper_v0=0x10000000; helper_v1=v1
 else:
  target=0x113410;site=0x113884;jal_ra=0x11388c
  shift=((walk_s1 &0xffffffff)<<2)&63
  a4=(0x7333>>shift)&MASK
  helper_v1=s32(counter)
  helper_a0=s32((helper_v1 +s32(a4))&0xffffffff)
  helper_v0=0x10001000
 boundary={'external_call_site':hex(site),'external_call_target':hex(target),
   'ra':hex(jal_ra),'a0':hex(helper_a0),'s1':hex(walk_s1),
   'v0':hex(helper_v0),'v1':hex(helper_v1),'a2':hex(v6),
   'sp':hex(saved_sp),'s0':hex(walk_s0),'s2':hex(walk_s2)}
 original_case=(label,sp,ra,s0,s1,s2,a0,g28,g24,mmio,
                nodes[3],nodes[1],nodes[2],nodes[0],counter)
 if chain_words is None:chain_words=synthetic_instructions()
 if post_words is None:post_words=synthetic_post_words()
 chain=double_helper_oracle(original_case,first,second,boundary,chain_words,post_words)
 return (flags,1,pending,1,*list_output,*chain,1 if chain[3]==2 else 2)

def compare_native(exe,list_words=None,chain_words=None,post_words=None,handler_words=None):
 if list_words is None:list_words=LIST_SYNTHETIC
 if chain_words is None:chain_words=synthetic_instructions()
 if post_words is None:post_words=synthetic_post_words()
 validate_post_words(post_words)
 cc=list(cases())
 data=''.join(' '.join((c[0],*(hex(x) for x in c[1:])))+'\n' for c in cc)
 proc=subprocess.run([str(exe)],input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=180)
 if proc.returncode:raise ValueError(f'ListIrqChain native process rc={proc.returncode}: {proc.stderr[:400]}')
 lines=proc.stdout.splitlines()
 if len(lines)!=len(cc):raise ValueError(f'ListIrqChain received {len(lines)} records expected {len(cc)}')
 mismatches=[];suppressed=walks=loops=path400=path410=first_polls=returns=second_polls=0
 for c,line in zip(cc,lines):
  v=line.split()
  if len(v)!=47 or v[0]!='LIST_IRQ_CHAIN':raise ValueError(f'ListIrqChain bad output format: {line[:150]} count={len(v)}')
  got=tuple(int(x,16) for x in v[1:]);want=expected(c,list_words,chain_words,post_words,handler_words)
  if got!=want:mismatches.append({'case':c[0],'want':list(map(hex,want)), 'got':list(map(hex,got))})
  suppressed+=int(want[1]==0)
  walks+=int(want[3]==1)
  loops+=int(want[3]==1 and want[20]==2) # list stop_code at index 20
  if want[22]==0x113400:path400+=1
  if want[22]==0x113410:path410+=1
  if want[24]==2:first_polls+=1
  if want[38]==2:second_polls+=1
  if want[38]==1:returns+=1
 return {'component':'list_irq_chain','case_count':len(cc),'passed':not mismatches,
  'mismatches':mismatches[:6],'virtual_dispatch_suppressed':suppressed,
  'original_list_walk_executed':walks,'bounded_list_stops':loops,
  'first_helper_call_400':path400,'first_helper_call_410':path410,
  'first_helper_poll_stops':first_polls,'second_helper_poll_stops':second_polls,
  'second_helper_modeled_returns':returns,
  'evidence':'SHA-pinned retail opcode interpreter and bounded compositional model' if handler_words is not None else 'synthetic instruction-level oracle',
  'guest_handler_registered':False,'game_bootable':False,
  'actual_PS2_interrupt_timing_verified':False,'hardware_effects':'virtual CPU and memory only'}
