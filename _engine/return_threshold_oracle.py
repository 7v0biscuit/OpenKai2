#!/usr/bin/env python3
"""Phase210 independently decode pinned original return/threshold instructions.

Original retail machine words exist only in the user's verified ELF memory.
No game data, opcode dumps, or retail assets are shipped with this release.
"""
from __future__ import annotations
import hashlib,json,struct,subprocess
from . import followup_oracle
from .entry_oracle import hex64,s32
MASK=(1<<64)-1
RETURN_PCS=tuple(range(0x1138e8,0x113900,4))
THRESHOLD_PCS=(0x113860,0x113864)
RETURN_SHA='9f7bb2ebf54ce80400fcd573f318d4a0d0d646e57d8ceaecd471f9a8c8d88a87'
THRESHOLD_SHA='765913b70753ac6d694708a9c3b0194fb8a097ac6a2a6cd40443066e100bc25b'
CASES=list(followup_oracle.CASES)+[
 ('threshold_equal',0x200080,0x1234,0,0,0,0,0xffffffff,0x210040,0,0x7333,0,0,0),
 ('threshold_over',0x2000c0,0x5678,0,0,0,0,0xffffffff,0x210040,0,0x7334,0,0,0),
 ('threshold_under',0x200100,0x90ab,0,0,0,0,0xffffffff,0x210040,0,0x7332,0,0,0),
 ('positive_return_full64',0x200140,0xfedcba9876543200,0x1234567887654321,0xabcdefaabbccddee,0x1111111111111111,0x55,0,0x210040,0x0,0x0,0x0,0x0,0),
]
FIELDS=followup_oracle.FIELDS+['ra','returned_from_handler','return_delay_executed','threshold_branch_seen','threshold_branch_taken','threshold_delay_executed']

def verified_rows(report,*,allow_stage_absent=False):
    rows=followup_oracle.verified_rows(report,allow_stage_absent=allow_stage_absent)
    for pcs,digest,label in ((RETURN_PCS,RETURN_SHA,'return'),(THRESHOLD_PCS,THRESHOLD_SHA,'threshold')):
        if any(pc not in rows for pc in pcs):raise ValueError(label+' original opcode missing')
        actual=hashlib.sha256(b''.join(struct.pack('<I',rows[pc]) for pc in pcs)).hexdigest()
        if actual!=digest:raise ValueError(label+' instruction sequence mismatch')
    # Verify semantic shape rather than depending solely on SHA-256.
    for pc,index,offset in zip(RETURN_PCS[:4],(31,18,17,16),(48,32,16,0)):
        w=rows[pc]
        if w>>26!=0x37 or (w>>21)&31!=29 or (w>>16)&31!=index or (w&0xffff)!=offset:
            raise ValueError('unexpected original saved-register load')
    jr=rows[0x1138f8]
    if jr>>26!=0 or jr&63!=8 or (jr>>21)&31!=31:raise ValueError('invalid JR return')
    slot=rows[0x1138fc]
    if slot>>26!=9 or (slot>>21)&31!=29 or (slot>>16)&31!=29 or slot&0xffff!=64:
        raise ValueError('invalid return delay slot')
    branch=rows[0x113860]
    if branch>>26!=4 or (branch>>21)&31!=2 or (branch>>16)&31!=0:
        raise ValueError('threshold BEQ must compare v0 with zero')
    disp=branch&65535
    if disp&0x8000:disp-=65536
    if 0x113864+(disp<<2)!=0x1138b0:raise ValueError('threshold BEQ destination mismatch')
    lui=rows[0x113864]
    if lui>>26!=15 or (lui>>16)&31!=2 or lui&65535!=0x1000:
        raise ValueError('threshold delay-slot LUI mismatch')
    return rows

def expected(case,rows=None):
    original=followup_oracle.expected(case,rows)
    out=dict(original)
    out.update(ra=hex64(case[2]),returned_from_handler=False,return_delay_executed=False,
               threshold_branch_seen=False,threshold_branch_taken=False,threshold_delay_executed=False)
    old_pc=int(original['next_pc'],16)
    if old_pc==0x1138e8:
        # Retain original saved stack snapshots; the epilogue reads but does not overwrite them.
        sp=int(original['sp'],16)
        regs={29:sp,31:case[2],16:case[3],17:case[4],18:case[5]}
        stack={sp+48:case[2],sp+32:case[5],sp+16:case[4],sp:case[3]}
        if rows is None:
            for reg,off in ((31,48),(18,32),(17,16),(16,0)):regs[reg]=stack[sp+off]
            target=regs[31]
            regs[29]=s32(sp+64)
        else:
            for pc in RETURN_PCS:
                w=rows[pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;imm=w&65535
                if imm&0x8000:imm-=65536
                if op==0x37:
                    addr=(regs[rs]+imm)&MASK
                    if addr not in stack:raise ValueError('unmapped epilogue load')
                    regs[rt]=stack[addr]
                elif op==0 and (w&63)==8:target=regs[rs]
                elif op==9:regs[rt]=s32(regs[rs]+imm)
                else:raise ValueError('unexpected original epilogue opcode')
        out.update(next_pc=hex64(target),sp=hex64(regs[29]),ra=hex64(regs[31]),
                   s0=hex64(regs[16]),s1=hex64(regs[17]),s2=hex64(regs[18]),
                   returned_from_handler=True,return_delay_executed=True)
    if old_pc==0x113860:
        regs={2:int(original['v0'],16),0:0}
        if rows is None:
            take=regs[2]==0
            regs[2]=s32(0x10000000)
            next_pc=0x1138b0 if take else 0x113868
        else:
            word=rows[0x113860]
            rs=(word>>21)&31;rt=(word>>16)&31;disp=word&65535
            if disp&0x8000:disp-=65536
            take=regs[rs]==regs[rt]
            slot=rows[0x113864]
            regs[(slot>>16)&31]=s32((slot&0xffff)<<16)
            next_pc=0x113864+(disp<<2) if take else 0x113868
        out.update(v0=hex64(regs[2]),next_pc=hex64(next_pc),threshold_branch_seen=True,
                   threshold_branch_taken=take,threshold_delay_executed=True)
    return out

def run_suite(exe,report,output,*,offline=False,allow_stage_absent=False):
    result={'phase':210,'status':'STARTED',
            'scope':'EARLY_HANDLER_RETURN_EPILOGUE_AND_THRESHOLD_BEQ_DELAY_SLOT',
            'evidence_type':'synthetic_model' if offline else 'sha_pinned_retail_opcode_interpreter',
            'original_handler_complete':False,'function_registered':False,'game_booted':False,'cases':[]}
    try:
        rows=None if offline else verified_rows(report,allow_stage_absent=allow_stage_absent)
        for case in CASES:
            c=subprocess.run([str(exe),case[0],*[hex(x) for x in case[1:]]],capture_output=True,text=True,timeout=10)
            if c.returncode or 'PHASE210_BRANCH ' not in c.stdout:
                raise ValueError(f'{case[0]} native rc={c.returncode} stderr={c.stderr[:150]}')
            got=json.loads(c.stdout.split('PHASE210_BRANCH ',1)[1].splitlines()[0])
            want=expected(case,rows)
            diff={k:{'expected':want[k],'actual':got.get(k)} for k in FIELDS if want[k]!=got.get(k)}
            result['cases'].append({'case':case[0],'pass':not diff,'next_pc':got.get('next_pc'),'mismatches':diff})
        result['all_cases_pass']=all(r['pass'] for r in result['cases'])
        result['return_cases']=sum(r['pass'] and expected(c,rows)['returned_from_handler'] for r,c in zip(result['cases'],CASES))
        result['threshold_taken_cases']=sum(r['pass'] and expected(c,rows)['threshold_branch_taken'] for r,c in zip(result['cases'],CASES))
        result['threshold_fallthrough_cases']=sum(r['pass'] and expected(c,rows)['threshold_branch_seen'] and not expected(c,rows)['threshold_branch_taken'] for r,c in zip(result['cases'],CASES))
        if min(result['return_cases'],result['threshold_taken_cases'],result['threshold_fallthrough_cases'])<1:
            raise ValueError('insufficient branch and return coverage')
        result['status']=('PHASE210_SYNTHETIC_PASS_NOT_RETAIL' if offline else 'PHASE210_RETAIL_WORDS_DIFFERENTIAL_PASS_NOT_FULL_HANDLER') if result['all_cases_pass'] else 'PHASE210_DIFFERENTIAL_MISMATCH'
    except Exception as exc:
        result.update(status='PHASE210_ORACLE_OR_NATIVE_REJECTED',error=str(exc)[:500],all_cases_pass=False)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    return 0 if result.get('all_cases_pass') else 2
