"""Differential oracle for LiveGuestMemory live guest memory original-handler chain.

The reference runs the original list-loop MIPS words through the independent
ListWalk interpreter. Patch fixture mutations apply to guest nodes AFTER the
original handler prefix. This isn't a PS2 memory subsystem or guest boot.
"""
from __future__ import annotations
import subprocess
from .list_irq_chain import cases as original_cases,expected as original_expected
from .list_walk import SYNTHETIC as LIST_SYNTHETIC
from .wrapper_chain import synthetic_instructions
from .helper_continuation import synthetic_post_words

PATCHES=((0,0,0), (1,3,0x9000), (1,0,0), (2,1,0x7fff000000001337))

def cases():
 for c in original_cases():
  for k,(node,field,value) in enumerate(PATCHES):
   yield (f'{c[0]}_mem{k}',*c[1:],node,field,value)

def mutated_original(c):
 original=list(c[:-3]);node,field,value=c[-3:]
 if node:
  # original original-case node words begin at original tuple index 12;
  # node1 is fields 16..19, node2 is fields 20..23.
  original[12+node*4+field]=value & (0xffffffff if field==0 else 0xffffffffffffffff)
 return tuple(original)

def expected(c,list_words=None,chain_words=None,post_words=None,handler_words=None):
 mutated=mutated_original(c)
 o=original_expected(mutated,list_words,chain_words,post_words,handler_words)
 assert len(o)==46
 if not o[3]:return (*o,0,0)
 loaded=o[19]
 backedges=o[18]
 first_exit=o[14]
 links=0 if first_exit else backedges+(0 if o[20]==2 else 1)
 reads=loaded*3+links
 bytes_read=loaded*24+links*4
 return (*o,reads,bytes_read)

def compare_native(exe,list_words=None,chain_words=None,post_words=None,handler_words=None):
 list_words=LIST_SYNTHETIC if list_words is None else list_words
 chain_words=synthetic_instructions() if chain_words is None else chain_words
 post_words=synthetic_post_words() if post_words is None else post_words
 cc=list(cases())
 payload=''.join(' '.join((c[0],*(hex(x) for x in c[1:])))+'\n' for c in cc)
 proc=subprocess.run([str(exe)],input=payload,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=240)
 if proc.returncode:raise ValueError(f'LiveGuestMemory native rc={proc.returncode}: {proc.stderr[:350]}')
 rows=proc.stdout.splitlines()
 if len(rows)!=len(cc):raise ValueError(f'LiveGuestMemory {len(rows)} rows vs {len(cc)} cases')
 issues=[];changed=0;loads=0;bytes_read=0;admitted=0
 previous={}
 for c,line in zip(cc,rows):
  v=line.split()
  if len(v)!=49 or v[0]!='LIVE_GUEST_MEMORY':raise ValueError('LiveGuestMemory malformed native output '+line[:150])
  got=tuple(int(z,16) for z in v[1:])
  want=expected(c,list_words,chain_words,post_words,handler_words)
  if got!=want:
   issues.append({'case':c[0],'expected':list(map(hex,want)), 'actual':list(map(hex,got))})
  admitted+=int(want[1]==1)
  loads+=want[-2];bytes_read+=want[-1]
  k=c[0].rsplit('_mem',1)[0]
  if c[-3]==0:previous[k]=want
  elif want!=previous[k]:changed+=1
 return {'component':'live_guest_memory','case_count':len(cc),'passed':not issues,'mismatches':issues[:8],
  'original_handler_admitted':admitted,'observable_changed_cases':changed,
  'live_guest_memory_loads':loads,'live_guest_memory_bytes_read':bytes_read,
  'guest_memory_backend':'byte-addressable CPU.memory, no detached node source',
  'source_identity':'SHA-pinned retail opcode model' if handler_words is not None else 'synthetic opcode model',
  'guest_registered':False,'ps2_interrupt_timing_verified':False,'game_booted':False}
