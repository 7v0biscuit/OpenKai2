"""Independent retail-opcode interpreter for the two external-call boundaries.
Only validated, owned original ELF words are consumed in memory at runtime.
No external helper function is invoked and no game assets are shipped.
"""
from __future__ import annotations
import hashlib,struct,json,subprocess
from . import return_threshold_oracle as prior
from .entry_oracle import hex64,s32
MASK=(1<<64)-1
CALL400=tuple(range(0x1138b0,0x1138c0,4))
CALL410=tuple(range(0x113868,0x11388c,4))
CALL400_SHA='4de535d9e8ba9f0f99f68231baddb4c977bb6dd7e07d904c12eb63fb6b3e70db'
CALL410_SHA='2d9e3ac60053aa5c79b30cf3b23ff7193d2c00b6c0e6748f4d11b14f885c0525'
CASES=[tuple(case)+(0,) for case in prior.CASES]+[
 ('counter_flags_1',0x200080,0x101,0,0,0,0,0xffffffff,0x210040,1,0,0,0,0,0x80000001),
 ('counter_flags_2',0x2000c0,0x102,0,0,0,0,0xffffffff,0x210040,2,0,0,0,0,0x7fffffff),
 ('counter_flags_3',0x200100,0x103,0,0,0,0,0xffffffff,0x210040,3,0,0,0,0,0xffffffff),
 ('masked_helper_400',0x200140,0x104,0,0,0,0,0xffffffff,0x210040,0x80000003,0,0,0x7333,0,0x12345678),
]
FIELDS=prior.FIELDS+['external_call_pending','external_call_executed','external_call_delay_executed','external_call_target','external_call_site','mmio_counter_read']

def verified_rows(report,*,allow_stage_absent=False):
    words=prior.verified_rows(report,allow_stage_absent=allow_stage_absent)
    for pcs,digest in ((CALL400,CALL400_SHA),(CALL410,CALL410_SHA)):
        if any(pc not in words for pc in pcs):raise ValueError('missing original helper call opcode')
        raw=b''.join(struct.pack('<I',words[p]) for p in pcs)
        if hashlib.sha256(raw).hexdigest()!=digest:raise ValueError('retail helper call boundary changed')
    for base,expect in ((0x1138b8,0x113400),(0x113884,0x113410)):
        w=words[base]
        if w>>26!=3 or (((base+4)&0xf0000000)|((w&0x3ffffff)<<2)) != expect:
            raise ValueError('retail JAL target unexpected')
    if (words[0x1138bc]&63)!=0x24 or (words[0x113888]&63)!=0x21:
        raise ValueError('JAL delay slot opcode changed')
    return words

def expected(case,rows=None):
    old=prior.expected(case[:-1],rows)
    out=dict(old)
    out.update(external_call_pending=False,external_call_executed=False,
      external_call_delay_executed=False,external_call_target=hex64(0),
      external_call_site=hex64(0),mmio_counter_read=False)
    pc=int(old['next_pc'],16)
    if pc not in (0x1138b0,0x113868):return out
    regs=[0]*32
    for index,name in ((2,'v0'),(3,'v1'),(4,'a0'),(6,'a2'),(7,'a3'),(16,'s0'),(17,'s1'),(18,'s2'),(29,'sp'),(31,'ra')):
        regs[index]=int(old[name],16)
    pcs=CALL400 if pc==0x1138b0 else CALL410
    if rows is None:
        if pc==0x1138b0:
            regs[4]=((0xffffffffffff0000|0xf7ff)&regs[18]);regs[31]=0x1138c0
            target=0x113400;site=0x1138b8
        else:
            shift=((regs[17]&0xffffffff)<<2)&63
            regs[5]=s32((regs[17]&0xffffffff)<<2)
            regs[2]|=0x1000
            regs[4]=(0x7333>>shift)&MASK
            regs[3]=s32(case[-1]);regs[4]=s32(regs[4]);regs[31]=0x11388c
            regs[4]=s32(regs[3]+regs[4]);target=0x113410;site=0x113884
    else:
        target=0;site=0
        for instr_pc in pcs:
            w=rows[instr_pc];op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31
            sa=(w>>6)&31;fn=w&63;imm=w&65535
            if op==15:regs[rt]=s32(imm<<16)
            elif op==13:regs[rt]=(regs[rs]|imm)&MASK
            elif op==9:regs[rt]=s32(regs[rs]+(imm-65536 if imm&32768 else imm))
            elif op==35:
                addr=(regs[rs]+(imm-65536 if imm&32768 else imm))&0xffffffff
                if addr!=0x10001000:raise ValueError('outside modeled retail MMIO counter')
                regs[rt]=s32(case[-1])
            elif op==3:
                target=((instr_pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
                regs[31]=instr_pc+8;site=instr_pc
            elif op==0 and fn==0:regs[rd]=s32((regs[rt]&0xffffffff)<<sa)
            elif op==0 and fn==0x16:regs[rd]=regs[rt]>>(regs[rs]&63)
            elif op==0 and fn==0x3c:regs[rd]=(regs[rt]<<(sa+32))&MASK
            elif op==0 and fn==0x3f:
                val=regs[rt]; signed=val-(1<<64) if val>>63 else val
                regs[rd]=(signed>>(sa+32))&MASK
            elif op==0 and fn==0x21:regs[rd]=s32(regs[rs]+regs[rt])
            elif op==0 and fn==0x24:regs[rd]=regs[rs]&regs[rt]
            else:raise ValueError(f'unexpected original helper opcode at {instr_pc:#x}')
            regs[0]=0
        if not site or not target:raise ValueError('helper JAL was not executed')
    out.update(a0=hex64(regs[4]),v0=hex64(regs[2]),v1=hex64(regs[3]),ra=hex64(regs[31]),
     next_pc=hex64(target),external_call_pending=True,external_call_executed=False,
     external_call_delay_executed=True,external_call_target=hex64(target),external_call_site=hex64(site),
     mmio_counter_read=(pc==0x113868))
    return out

def run_suite(exe,report,output,*,offline=False,allow_stage_absent=False):
    result={'component':'helper_boundary','status':'STARTED','scope':'PINNED_HANDLER_JAL_DELAY_SLOT_AND_EXTERNAL_CALL_BOUNDARIES',
      'evidence_type':'synthetic_math_model' if offline else 'sha_pinned_original_opcode_interpreter',
      'original_handler_complete':False,'original_handler_registered':False,
      'external_helper_executed':False,'game_booted':False,'cases':[]}
    try:
        words=None if offline else verified_rows(report,allow_stage_absent=allow_stage_absent)
        for case in CASES:
            cp=subprocess.run([str(exe),case[0],*[hex(v) for v in case[1:]]],capture_output=True,text=True,timeout=12)
            if cp.returncode or 'HELPER_BOUNDARY_BRANCH ' not in cp.stdout:
                raise ValueError(f'{case[0]}: native rc={cp.returncode}, {cp.stderr[:180]}')
            got=json.loads(cp.stdout.split('HELPER_BOUNDARY_BRANCH ',1)[1].splitlines()[0]);want=expected(case,words)
            diff={k:{'want':want[k],'got':got.get(k)} for k in FIELDS if want[k]!=got.get(k)}
            result['cases'].append({'case':case[0],'pass':not diff,'next_pc':got.get('next_pc'),'mismatches':diff})
        result['call_400_cases']=sum(c['pass'] and c['next_pc']==hex64(0x113400) for c in result['cases'])
        result['call_410_cases']=sum(c['pass'] and c['next_pc']==hex64(0x113410) for c in result['cases'])
        result['all_cases_pass']=all(c['pass'] for c in result['cases']) and result['call_400_cases']>0 and result['call_410_cases']>0
        result['status']='HELPER_BOUNDARY_BOTH_HELPER_CALL_BOUNDARIES_PASS_NOT_GAME_BOOT' if result['all_cases_pass'] else 'HELPER_BOUNDARY_DIFFERENTIAL_MISMATCH'
    except Exception as e:result.update(all_cases_pass=False,status='HELPER_BOUNDARY_ORACLE_OR_NATIVE_REJECTED',error=str(e)[:500])
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result.get('all_cases_pass') else 2
