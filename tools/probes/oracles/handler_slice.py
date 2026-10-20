#!/usr/bin/env python3
"""Read-only, exact-identity-verified Budokai 2 USA MIPS handler slice.

Never writes back to an ISO/ELF, never bundles ELF contents, never registers a function.
Linear opcode review is an investigative snapshot, NOT a proven function extent or CFG.
"""
from __future__ import annotations
import argparse,hashlib,json,struct
from pathlib import Path
from .retail_elf import AuditError,MipsELF,EXPECTED_SHA256,decode_mips,load_retail_elf,search_elf
from .frontier import frontier
HANDLER_PC=0x00113770
BEFORE_WORDS=24
AFTER_WORDS=120
MAX_INPUT_BYTES=256*1024*1024

def handler_snapshot(blob:bytes, *, expected_sha:str=EXPECTED_SHA256, handler_pc:int=HANDLER_PC,
                     before:int=BEFORE_WORDS, after:int=AFTER_WORDS)->dict:
    """Pure verifier/parser. Caller must supply input from its own disk."""
    identity=hashlib.sha256(blob).hexdigest()
    r={'component':'retail_audit','kind':'verified_retail_elf_instruction_slice',
       'status':'STARTED','retail_elf_sha256':identity,
       'expected_retail_elf_sha256':expected_sha,
       'target_pc':f'0x{handler_pc:08X}',
       'original_function_recompiled':False,'function_start_verified':False,
       'function_end_verified':False,'runtime_guest_handler_executed':False,
       'game_booted':False,'game_playable':False,
       'is_linear_decode_not_cfg':True,'data_only_no_registry_mutation':True}
    if len(blob)>MAX_INPUT_BYTES:raise AuditError('Retail ELF exceeds bounded parsing limit')
    if identity != expected_sha:
        r.update(status='RETAIL_ELF_IDENTITY_MISMATCH',warning='Refusing to decode an unverified executable')
        return r
    elf=MipsELF(blob)
    if handler_pc%4 or not (0<=before<=64) or not (1<=after<=192):
        raise AuditError('Unaligned target or disassembly window outside bounded limits')
    segment=next((x for x in elf.segments if x['vaddr']<=handler_pc<x['vaddr']+x['filesz']),None)
    if not segment:raise AuditError('Handler PC not backed by file bytes in PT_LOAD segment')
    if not segment['flags'] & 1:
        raise AuditError('Handler PC is not in an executable PT_LOAD segment')
    rows=[]
    for addr in range(handler_pc-before*4,handler_pc+after*4,4):
        try:
            w=elf.word(addr)
        except AuditError:
            if addr==handler_pc:raise
            continue
        x=decode_mips(addr,w)
        x['relative_word_index']=(addr-handler_pc)//4
        x['is_target_entry']=addr==handler_pc
        x['is_prior_context']=addr<handler_pc
        # The upstream minimal decoder calls unrecognized instructions .word; don't
        # confuse them with nonexistent/invalid retail instructions.
        x['decoder_coverage']='partial' if x['mnemonic'].startswith('.word') else 'recognized'
        rows.append(x)
    branch=[x for x in rows if x['kind']]
    targets=[{'site':x['pc'],'mnemonic':x['mnemonic'],'kind':x['kind'],'target':x['target'],
              'delay_slot_pc':f"0x{int(x['pc'],16)+4:08X}" if x['has_delay_slot'] else None,
              'target_in_slice':(bool(x['target']) and any(y['pc']==x['target'] for y in rows))}
             for x in branch]
    r.update(status='PINNED_RETAIL_HANDLER_BYTES_AVAILABLE_NOT_RECOMPILED',
       elf_entry_pc=f'0x{elf.entry:08X}',segment_index=segment['index'],
       segment_vaddr=f"0x{segment['vaddr']:08X}",
       segment_filesz=segment['filesz'],segment_flags=segment['flags'],
       handler_file_offset=elf.file_offset(handler_pc),
       rows=rows,word_count=len(rows),recognized_words=sum(x['decoder_coverage']=='recognized' for x in rows),
       unknown_opcode_words=sum(x['decoder_coverage']=='partial' for x in rows),
       control_flow=targets,direct_call_candidates=sorted(set(x['target'] for x in rows if x['kind']=='direct_call' and x['target'])),
       branch_delay_slot_count=sum(x['has_delay_slot'] for x in rows),
       entry_context={'first_word':next((x['word'] for x in rows if x['is_target_entry']),None)},
       followup='Verify precise function boundaries and instruction behavior with independent disassembly and interpreter tests before compiling or binding.')
    return r

def summarize_source(stage:Path, data:dict):
    reg=frontier(stage)
    data['stage_registry_audit']=reg.get('stage_audit_result','MISSING')
    data['stage_registry_sha256']=reg.get('stage_sha256')
    data['stage_registered_original_entries']=reg.get('original_registry_binding_count')
    data['handler_registered']=f'0x{HANDLER_PC:08X}' in reg.get('registered_addresses',[])
    return data

def run(input_path:Path|None, stage:Path, output:Path, expected_sha=EXPECTED_SHA256):
    """Structured missing-input reporting; never silently substitute another game build."""
    if input_path is None or not input_path.is_file():
        report={'component':'retail_audit','status':'OWNED_RETAIL_ISO_OR_ELF_REQUIRED',
                'expected_retail_elf_sha256':expected_sha,
                'original_function_recompiled':False,'game_booted':False,
                'handler_pc':f'0x{HANDLER_PC:08X}',
                'action':'Select the owned Budokai 2 USA ISO or extracted SLUS_207.79 on Windows.'}
        code=3
    else:
        try:
            blob,kind=load_retail_elf(input_path)
            report=handler_snapshot(blob,expected_sha=expected_sha)
            report['input_kind']=kind
            report['retail_elf_bytes_in_report']=False
            code=0 if report['status']=='PINNED_RETAIL_HANDLER_BYTES_AVAILABLE_NOT_RECOMPILED' else 2
        except (OSError,ValueError,AuditError,struct.error) as e:
            report={'component':'retail_audit','status':'RETAIL_INPUT_REJECTED',
                    'error':str(e)[:320],'original_function_recompiled':False,
                    'game_booted':False};code=2
    summarize_source(stage,report)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    return code

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--iso',default='')
    p.add_argument('--stage',default=str(Path(__file__).resolve().parent/'stage/register_functions_registry_snapshot.cpp'))
    p.add_argument('--output',required=True)
    p.add_argument('--no-discovery',action='store_true')
    a=p.parse_args()
    path=Path(a.iso) if a.iso else (None if a.no_discovery else search_elf('',[Path(__file__).resolve().parent,Path(__file__).resolve().parent.parent]))
    rc=run(path,Path(a.stage),Path(a.output))
    print('RETAIL_AUDIT_RETAIL_ELF_HANDLER',json.loads(Path(a.output).read_text())['status'])
    return rc
if __name__=='__main__':raise SystemExit(main())
