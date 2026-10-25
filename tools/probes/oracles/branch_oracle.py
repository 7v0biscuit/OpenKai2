#!/usr/bin/env python3
"""Independent source-word oracle for a bounded USA EE handler successor.

Never executes owned game code. Retail verification requires a SHA-verified
RetailAudit report, not guessed instruction contents or synthetic fixtures.
"""
from __future__ import annotations
import hashlib,json,struct,subprocess
from pathlib import Path
from .entry_oracle import (BASE,Mem,hex64,s32,extract_verified_words,
                                         oracle_words,standalone_oracle)
PC_START=0x11379c
PC_LAST=0x1137ac
FIVE_WORD_SHA='fbdd4cdf2c2c45c3b2b8d54321d4b1415d09fd89c16be74d3b6caca61118f5e1'
# case name, SP, RA, S0, S1, S2, A0, state-sign, handler-list ptr,
# synthetic mmio value, memory node+32 value
CASES=[
 ('negative_zero',0x200080,0x1111111122222222,0x123,0x456,0x789,0xabc,0xffffffff,0,0x81234567,0x1122334455667788),
 ('negative_ptr',0x200100,0xabcdef,0xdef,0xaaa,0xabc123,0x22,0xffffffff,0x210040,0x80000001,0x0123456789abcdef),
 ('negative_ptr_allones',0x200180,1,2,3,4,5,0x80000000,0x210040,0xffffffff,0xffffffffffffffff),
 ('negative_zero_mmio',0x2000c0,0xdeadbeef,0x333,0x444,0x555,0x666,0xfffffffe,0,0x7fffffff,0xabcdeffedcba9876),
 ('negative_ptr_zero',0x200140,0x444,0x555,0x666,0x777,0x888,0xffffffff,0x210040,0,0),
 ('positive_skip',0x200080,0x123,0x456,0x789,0xabc,0xdef,0,0x210040,0xdeadbeef,0x12345678),
 ('positive_skip2',0x200100,0xfedcba,0x111,0x222,0x333,0x444,0x7fffffff,0,0xabcdef,0),
 ('negative_signextend',0xffffffff00200140,0x8888,0x7777,0x6666,0x5555,0x4444,0x80000001,0x210040,0x80000000,0x8000000000000000),
]
FIELDS=['case','status','next_pc','sp','v0','v1','a2','a3','s2','delay_executed',
        'branch_likely_taken','mmio_read','saved_ra','saved_s2','saved_s1',
        'saved_s0','stack_checksum']

def verified_rows(report):
    rows=extract_verified_words(report)
    words=[rows.get(x) for x in range(PC_START,PC_LAST+1,4)]
    if any(v is None for v in words):raise ValueError('missing verified 5-word successor')
    digest=hashlib.sha256(b''.join(struct.pack('<I',x) for x in words)).hexdigest()
    if digest!=FIVE_WORD_SHA:raise ValueError('successor word SHA mismatch')
    # Decode structural operation categories, without hardcoding full raw words.
    expected=[0x0f,0x0d,0x23,0x15,0x37]
    for i,(w,op) in enumerate(zip(words,expected)):
        if w>>26!=op:raise ValueError(f'unexpected opcode at successor slot {i}')
    if (words[3]>>21)&31!=6 or (words[3]>>16)&31!=0:
        raise ValueError('unexpected bnel operand registers')
    return rows

def expected(case,rows=None):
    prefix=case[:9]
    got=(oracle_words(rows,prefix) if rows is not None else standalone_oracle(prefix))
    assert got['status']=='TRANSLATED_PREFIX_RETURNED'
    out=dict(got)
    out['status']='TRANSLATED_BOUNDED_BLOCK_RETURNED'
    out['s2']=hex64(case[5]);out['delay_executed']=False
    out['branch_likely_taken']=False;out['mmio_read']=False
    if int(got['next_pc'],16)==0x1138e8:
        return out
    if rows is None:
        out['v0']=hex64(0x10001010)
        out['s2']=hex64(s32(case[9]))
        out['mmio_read']=True
        if int(got['a2'],16)!=0:
            out['v0']=hex64(case[10]);out['delay_executed']=True
            out['branch_likely_taken']=True;out['next_pc']=hex64(0x1137dc)
        else:out['next_pc']=hex64(0x1137b0)
        return out
    # Interpret the verified successor words. This is not a copy of the native
    # C++ branch statement; target and register operands come from original MIPS.
    registers=[0]*32
    registers[2]=int(got['v0'],16)
    registers[6]=int(got['a2'],16)
    registers[18]=case[5]
    mem=Mem()
    # The independent model uses only these bounded read locations.
    extra={}
    for base,val,width in ((0x10001010,case[9],4),(0x210040+32,case[10],8)):
        for j in range(width):extra[base+j]=(val>>(8*j))&0xff
    def memread(addr,width):
        if addr%width:raise ValueError('unaligned EE load')
        if any(addr+j not in extra for j in range(width)):
            raise ValueError(f'unmapped independent EE read {addr:#x}')
        return sum(extra[addr+j]<<(8*j) for j in range(width))
    pc=PC_START
    for _ in range(4):
        w=rows[pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;imm=w&0xffff
        imm_signed=imm-65536 if imm>=32768 else imm
        if op==0x0f:
            if rt:registers[rt]=s32(imm<<16)
        elif op==0x0d:
            if rt:registers[rt]=(registers[rs] | imm)&((1<<64)-1)
        elif op==0x23:
            if rt:registers[rt]=s32(memread((registers[rs]+imm_signed)&0xffffffff,4))
            out['mmio_read']=True
        elif op==0x15:
            target=pc+4+(imm_signed<<2)
            if registers[rs]!=registers[rt]:
                delay=rows[pc+4]
                assert delay>>26==0x37
                d_rs=(delay>>21)&31;d_rt=(delay>>16)&31
                d_i=delay&0xffff
                if d_i&0x8000:d_i-=0x10000
                if d_rt:registers[d_rt]=memread((registers[d_rs]+d_i)&0xffffffff,8)
                out['delay_executed']=True;out['branch_likely_taken']=True
                out['next_pc']=hex64(target)
            else:
                out['next_pc']=hex64(pc+8)
        else:raise ValueError(f'unsupported successor opcode {op}')
        pc+=4
    out['v0']=hex64(registers[2]);out['s2']=hex64(registers[18])
    return out

def run_suite(exe,report,output, *, offline=False):
    result={'component':'branch','status':'STARTED','source_kind': 'synthetic_semantic_model' if offline else 'verified_retail_opcode_word_oracle',
            'scope':'ENTRY_PREFIX_PLUS_FIVE_NEGATIVE_PATH_INSTRUCTIONS','original_handler_registered':False,
            'full_handler_compiled':False,'game_booted':False,'test_cases':[]}
    try:
        rows=None if offline else verified_rows(report)
        for case in CASES:
            p=subprocess.run([str(exe),case[0],*[hex(x) for x in case[1:]]],
                              capture_output=True,text=True,timeout=10)
            if p.returncode or 'BRANCH_BRANCH ' not in p.stdout:
                raise RuntimeError(f'{case[0]}: rc {p.returncode}, {p.stderr[:200]}')
            observed=json.loads(p.stdout.split('BRANCH_BRANCH ',1)[1].splitlines()[0])
            wanted=expected(case,rows)
            mismatches={k:{'want':wanted[k],'got':observed.get(k)} for k in FIELDS if wanted[k]!=observed.get(k)}
            result['test_cases'].append({'case':case[0],'pass':not mismatches,
                                         'expected_pc':wanted['next_pc'],
                                         'observed_pc':observed.get('next_pc'),'mismatches':mismatches})
        result['all_cases_pass']=all(x['pass'] for x in result['test_cases'])
        result['status']=('BRANCH_SYNTHETIC_BRANCH_PASS_NOT_RETAIL' if offline else
                          'BRANCH_PINNED_RETAIL_BRANCH_DIFFERENTIAL_PASS_NOT_HANDLER') if result['all_cases_pass'] else 'BRANCH_DIFFERENTIAL_FAILED'
    except Exception as exc:
        result.update(status='BRANCH_ORACLE_OR_NATIVE_REJECTED',error=str(exc)[:600],all_cases_pass=False)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    return 0 if result['all_cases_pass'] else 2
