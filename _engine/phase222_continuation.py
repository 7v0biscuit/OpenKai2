"""Opcode-driven validation of BOTH post-JAL handler paths through a second
retail tail wrapper and VIRTUAL shared helper. Does NOT execute guest IRQ.
"""
from __future__ import annotations
import hashlib,struct,subprocess
from .helper_boundary_oracle import expected as handler_expected
from .phase221_bridge import bridge_cases,VIRTUAL_CPU_STATES
from .phase213_semantics import interpreter as wrapper_oracle
from .phase216_semantics import oracle as shared_oracle
from .phase217_semantics import synthetic_instructions
from .entry_oracle import s32
MASK=(1<<64)-1
WINDOWS={
 0x11388c:(0x1138b0,'dd02ccd3d52a731478997e034c9e1b01484f543ca346c270d36bd9df936ccd95'),
 0x1138c0:(0x1138e8,'2fdce753cb0349b33f4f59c5889ba97fb9e86b23e60e7735cab6f750a3781d21'),
}
def i(op,rs,rt,imm):return (op<<26)|(rs<<21)|(rt<<16)|(imm&65535)
def r(rs,rt,rd,sh,fn):return (rs<<21)|(rt<<16)|(rd<<11)|(sh<<6)|fn
def jump(pc):return (2<<26)|((pc>>2)&0x3ffffff)
def synthetic_post_words():
 return dict(zip(range(0x11388c,0x1138b0,4),(
  i(15,0,4,0xffff),i(55,29,31,48),i(13,4,4,0xf7ff),
  i(55,29,17,16),r(18,4,4,0,36),i(55,29,16,0),
  i(55,29,18,32),jump(0x113400),i(9,29,29,64)))) | dict(zip(range(0x1138c0,0x1138e8,4),(
  r(0,17,4,2,0),i(55,29,31,48),r(4,16,4,0,22),
  i(55,29,18,32),i(55,29,17,16),r(0,4,4,0,60),
  r(0,4,4,0,63),i(55,29,16,0),jump(0x113410),i(9,29,29,64))))
def validate_post_words(rows):
 result={}
 for first,(stop,digest) in WINDOWS.items():
  pcs=list(range(first,stop,4))
  if any(pc not in rows for pc in pcs):raise ValueError('missing post-helper window')
  raw=b''.join(struct.pack('<I',rows[pc]) for pc in pcs)
  if hashlib.sha256(raw).hexdigest()!=digest:raise ValueError('post-helper retail digest mismatch at '+hex(first))
  for pc in pcs:
   if rows[pc]!=synthetic_post_words()[pc]:raise ValueError('unexpected retail instruction '+hex(pc))
  result[first]=digest
 return result

def interpret_post(rows,entry,regs,stack):
 """Generic MIPS scalar interpreter of bounded straight-line windows; J delay included."""
 regs=list(regs);base=regs[29]&0xffffffff;pc=entry;target=None
 low32=lambda v:v&0xffffffff
 for step in range((WINDOWS[entry][0]-entry)//4):
  w=rows[pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31
  sh=(w>>6)&31;fn=w&63;imm=w&65535
  if op==15:regs[rt]=s32(imm<<16)
  elif op==55:
   addr=(low32(regs[rs])+(imm-65536 if imm&0x8000 else imm))&0xffffffff
   if addr not in stack:raise ValueError('unmapped guest stack load '+hex(addr))
   regs[rt]=stack[addr]
  elif op==13:regs[rt]=(regs[rs]|imm)&MASK
  elif op==9:regs[rt]=s32(regs[rs]+(imm-65536 if imm&0x8000 else imm))
  elif op==0 and fn==36:regs[rd]=regs[rs]&regs[rt]
  elif op==0 and fn==0:regs[rd]=s32((regs[rt]&0xffffffff)<<sh)
  elif op==0 and fn==22:regs[rd]=regs[rt]>>(regs[rs]&63)
  elif op==0 and fn==60:regs[rd]=(regs[rt]<<(sh+32))&MASK
  elif op==0 and fn==63:
   signed=regs[rt]-(1<<64) if regs[rt]&(1<<63) else regs[rt]
   regs[rd]=(signed>>(sh+32))&MASK
  elif op==2:
   if pc!=WINDOWS[entry][0]-8:raise ValueError('unexpected nonterminal J')
   target=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
  else:raise ValueError('unsupported guest post-call opcode '+hex(w)+' at '+hex(pc))
  regs[0]=0;pc+=4
 if target not in (0x113400,0x113410):raise ValueError('unknown tail helper')
 return target,regs

SECOND_SNAPSHOTS=((0,0,0,0x8000beef,0xf0f0f0f0),
 (0x10004,4,4,0x8000beef,0xabababab),
 (0x10004,0x10004,0x10004,0x8000beef,0xdeadbeef))

def cases(handler_rows=None):
 for case,first,boundary in bridge_cases(handler_rows):
  for second in SECOND_SNAPSHOTS:yield case,first,second,boundary

def oracle(case,first,second,boundary,words,post):
 wrapper_words,shared_words=words
 site=int(boundary['external_call_site'],16);target=int(boundary['external_call_target'],16)
 jal_ra=int(boundary['ra'],16);a0=int(boundary['a0'],16)
 a1=s32((int(boundary['s1'],16)&0xffffffff)<<2) if target==0x113410 else 0
 w=wrapper_oracle(target,wrapper_words[target],a0,a1,jal_ra,int(boundary['v0'],16))
 if w[0]!=0x113380 or w[3]!=jal_ra:raise ValueError('initial wrapper ABI mismatch')
 init,e1,e2,epc,old=first
 sh=shared_oracle(shared_words,(init,e1,e2,w[4],int(boundary['v1'],16),int(boundary['a2'],16),w[1],w[2],w[3],int(boundary['sp'],16),epc,old))
 head=(site,target,sh[0],sh[17],sh[1],sh[18],sh[19],sh[20])
 if sh[17]!=1:return head+(0,)+(0,)*15
 if sh[0]!=jal_ra:raise ValueError('virtual first ERET not returning to JAL+8')
 regs=[0]*32
 for r,v in {2:sh[4],3:sh[5],6:sh[6],4:sh[7],5:sh[8],
  31:sh[9],29:int(boundary['sp'],16),16:int(boundary['s0'],16),
  17:int(boundary['s1'],16),18:int(boundary['s2'],16)}.items():regs[r]=v
 sp=regs[29]&0xffffffff
 saved={sp:case[3]&MASK,sp+16:case[4]&MASK,
  sp+32:case[5]&MASK,sp+48:case[2]&MASK}
 tail,regs=interpret_post(post,sh[0],regs,saved)
 if regs[31]!=case[2] or regs[29]!=s32(case[1]):raise ValueError('handler callee-saved ABI broken')
 w2=wrapper_oracle(tail,wrapper_words[tail],regs[4],sh[8],regs[31],regs[2])
 if w2[0]!=0x113380 or w2[3]!=case[2]:raise ValueError('second tail wrapper ABI broken')
 init,e1,e2,epc,old=second
 sh2=shared_oracle(shared_words,(init,e1,e2,w2[4],sh[5],sh[6],w2[1],w2[2],w2[3],regs[29],epc,old))
 return head+(1,tail,regs[31],regs[29],regs[16],regs[17],regs[18],regs[4],
              sh2[0],sh2[17],sh2[1],sh2[18],sh2[19],sh2[2],sh2[20],sh2[16])

def compare_native(exe,handler_rows=None,words=None,post=None):
 if words is None:words=synthetic_instructions()
 if post is None:post=synthetic_post_words()
 validate_post_words(post)
 errs=[];count=0;first_poll=second_poll=returns=0;tails={hex(0x113400):0,hex(0x113410):0}
 for case,first,second,boundary in cases(handler_rows):
  count+=1
  args=[str(exe),case[0],*(hex(x) for x in case[1:]),
        *(hex(x) for x in first),*(hex(x) for x in second)]
  p=subprocess.run(args,text=True,capture_output=True,timeout=12)
  if p.returncode or 'PHASE222_CHAIN' not in p.stdout:raise ValueError(f'native phase222 rc={p.returncode} {p.stderr[:250]}')
  got=tuple(int(x,16) for x in p.stdout.split('PHASE222_CHAIN',1)[1].splitlines()[0].split())
  want=oracle(case,first,second,boundary,words,post)
  if len(got)!=24 or len(want)!=24:raise ValueError(f'phase222 field count actual={len(got)},expected={len(want)}')
  if got!=want:errs.append({'case':case[0],'first':first[:3],'second':second[:3],'actual':list(map(hex,got)),'expected':list(map(hex,want))})
  if want[3]==2:first_poll+=1
  else:
   tails[hex(want[9])]+=1
   if want[17]==2:second_poll+=1
   elif want[17]==1:returns+=1
 return {'phase':222,'case_count':count,'passed':not errs and count>0,
  'mismatches':errs[:8],'first_helper_poll_stop':first_poll,
  'second_helper_poll_stop':second_poll,'second_helper_modeled_return':returns,
  'tail_continuation_targets':tails,'post_helper_instruction_count':19,
  'virtual_mmio_only':True,'observed_guest_irq11_dispatch':False,
  'real_ps2_eret_semantics_verified':False,'game_booted':False,
  'reference':'retail opcode interpreter' if handler_rows is not None else 'synthetic MIPS encoding oracle'}
