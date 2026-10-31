"""Independent opcode-driven interpreter of two retail tail-jump wrappers.
Reads actual original instruction words exclusively from a verified owned ELF;
synthetic examples are assembled programmatically for offline tests.
"""
import hashlib,struct,subprocess
from .retail_elf import MipsELF,EXPECTED_SHA256,AuditError
MASK=(1<<64)-1
ENTRIES=(0x00113400,0x00113410)
TARGET=0x00113380
EXPECTED_HASHES={
 0x00113400:'ccd3ac0defe5c732be9d1b1a5a9623e70800fb995109ba722e35e09bee0c8e7f',
 0x00113410:'91061431d9f8b9a76047b81508a9b1d9dcbbf09942f75a6e116a87ae5cd9eef1',
}
def retail_instructions(blob):
 if hashlib.sha256(blob).hexdigest()!=EXPECTED_SHA256:
  raise AuditError('Wrong retail ELF SHA-256')
 elf=MipsELF(blob)
 out={}
 for entry in ENTRIES:
  for i in range(4):
   pc=entry+4*i
   if not any(s['vaddr']<=pc and pc+4<=s['vaddr']+s['filesz'] and s['flags']&1 for s in elf.segments):
    raise AuditError('Wrapper instruction outside file-backed executable segment')
  words=tuple(elf.word(entry+4*i) for i in range(4))
  digest=hashlib.sha256(struct.pack('<4I',*words)).hexdigest()
  if digest!=EXPECTED_HASHES[entry]:
   raise AuditError('Verified ELF helper wrapper digest mismatch')
  out[entry]=words
 return out

def synthetically_assembled(entry):
 r=lambda rs,rt,rd,fn: (rs<<21)|(rt<<16)|(rd<<11)|fn
 i=lambda op,rs,rt,imm: (op<<26)|(rs<<21)|(rt<<16)|(imm&0xffff)
 jump=(2<<26)|((TARGET>>2)&0x03ffffff)
 return (r(4,0,5,45),i(15,0,4,0xb000),jump,i(13,4,4,0x1010 if entry==ENTRIES[0] else 0x1020))

def signextend32(x):return x | (0xffffffff00000000 if x&0x80000000 else 0)

def interpreter(entry,words,a0,a1,ra,v0):
 """Decode each opcode with generic semantics; stop at the tail-jump target.
 Unsupported or mutated code is rejected rather than silently emulated.
 """
 regs=[0]*32;regs[4]=a0&MASK;regs[5]=a1&MASK;regs[31]=ra&MASK;regs[2]=v0&MASK
 pc=entry
 pending=None
 trace=[]
 for idx,w in enumerate(words):
  here=entry+idx*4
  if pc!=here:raise ValueError('Unexpected interpreter PC')
  op=(w>>26)&63;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;fn=w&63;imm=w&0xffff
  if op==0 and fn==45:
   if w&0x7c0:raise ValueError('Unsupported daddu encoding')
   regs[rd]=(regs[rs]+regs[rt])&MASK
  elif op==15 and rs==0:regs[rt]=signextend32(imm<<16)
  elif op==2:
   if pending is not None or idx!=2:raise ValueError('Unexpected jump')
   pending=((here+4)&0xf0000000)|((w&0x03ffffff)<<2)
  elif op==13:regs[rt]=(regs[rs]|imm)&MASK
  else:raise ValueError('Unsupported opcode at PC '+hex(here))
  regs[0]=0
  trace.append(hex(here))
  pc=here+4
  if idx==3:
   if pending is None:raise ValueError('Missing tail jump')
   pc=pending
 if pc!=TARGET:raise ValueError('Unexpected tail jump destination')
 return (pc,regs[4],regs[5],regs[31],regs[2])

def inputs():
 candidates=(0,1,0xffffffff,0x100000000,0x7fffffffffffffff,0x8000000000000000,
             0xffffffffffffffff,0x123456789abcdef0,0xfffffff000000100,0xb0001020,
             0xabcdeffedcba9876,0xdeadbeef,0x123400000001)
 return [(e,x,((x<<7)^0xbeef1357)&MASK,((x>>3)^0xfedcba9876543210)&MASK,
          ((x>>1)^0x11773399)&MASK) for e in ENTRIES for x in candidates]

def compare_native(exe,words_by_entry):
 scenarios=inputs()
 stdin=''.join(f'{e:08X} {a:016X} {b:016X} {ra:016X} {v:016X}\n' for e,a,b,ra,v in scenarios)
 out=subprocess.run([str(exe)],input=stdin,text=True,capture_output=True,timeout=30)
 if out.returncode:raise ValueError('Native wrapper probe failed: '+out.stderr[:250])
 lines=out.stdout.strip().splitlines()
 if len(lines)!=len(scenarios):raise ValueError(f'Native output {len(lines)} != cases {len(scenarios)}')
 failures=[]
 for case,line in zip(scenarios,lines):
  e,a,b,ra,v=case
  want=interpreter(e,words_by_entry[e],a,b,ra,v)
  try:got=tuple(int(x,16) for x in line.split())
  except ValueError:raise ValueError('Invalid native hex output')
  if got!=want:failures.append({'entry':hex(e),'initial_a0':hex(a),'want':[hex(x) for x in want], 'got':[hex(x) for x in got]})
 return {'case_count':len(scenarios),'passed':not failures,'failures':failures[:12],
         'wrapper_entry_count':2,'downstream_target_executed':False,'retail_function_registered':False}

def shared_window(blob,count=64):
 """Read-only downstream window, not a proven function boundary."""
 if hashlib.sha256(blob).hexdigest()!=EXPECTED_SHA256:raise AuditError('Wrong retail ELF')
 from .helper_window import classification
 elf=MipsELF(blob)
 rows=[]
 for i in range(count):
  pc=TARGET+4*i
  if not any(s['vaddr']<=pc and pc+4<=s['vaddr']+s['filesz'] and s['flags']&1 for s in elf.segments):
   raise AuditError('Shared target window not in executable bytes')
  w=elf.word(pc)
  kind, dest,delay,likely=classification(pc,w)
  rows.append({'pc':f'0x{pc:08X}','word':f'0x{w:08X}', 'category':kind,
               'direct_target':f'0x{dest:08X}' if dest else None,
               'delay_slot':delay,'branch_likely':likely})
 digest=hashlib.sha256(b''.join(struct.pack('<I',int(row['word'],16)) for row in rows)).hexdigest()
 return {'entry':'0x00113380','window_end_exclusive':f'0x{TARGET+count*4:08X}',
         'sha256_of_window':digest,'instruction_count':count,'rows':rows,
         'possible_return_sites':[x['pc'] for x in rows if x['category']=='return'],
         'direct_jump_targets':sorted({x['direct_target'] for x in rows if x['category'] in ('jump','call') and x['direct_target']}),
         'function_boundaries_verified':False,'semantic_translation_completed':False,
         'original_handler_complete':False,'game_bootable':False}
