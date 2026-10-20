#!/usr/bin/env python3
"""Conservative entry-rooted EE MIPS control-flow/recompilation readiness survey.

Inputs: a *fresh* Phase203 SHA-pinned original-game ELF report, never executable bytes.
The graph models the branch-likely annul and instruction delay slots. It is not an
interpreter, formal proof of a function boundary, or permission to bind game code.
"""
from __future__ import annotations
import argparse,json,collections
from pathlib import Path
from .retail_elf import decode_mips
PIN='431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'
ENTRY=0x00113770
LIKELY_OPS={0x14,0x15,0x16,0x17}
REGIMM_LIKELY_RT={0x02,0x03,0x12,0x13}
MAX_VISITED=192

def hx(pc):return f'0x{pc:08X}'
def likely(word:int)->bool:
    op=(word>>26)&63
    return op in LIKELY_OPS or (op==1 and (word>>16)&31 in REGIMM_LIKELY_RT)

def advisory_decode(word:int)->str|None:
    """Only confidently recognized EE scalar opcodes; advisory, not executable semantics."""
    opcode=(word>>26)&63; funct=word&63
    if opcode==0:
        supported={0x00:'sll',0x21:'addu',0x23:'subu',0x24:'and',0x25:'or',0x26:'xor',0x27:'nor',
                   0x2a:'slt',0x2b:'sltu',0x2c:'dadd',0x2d:'daddu',0x2e:'dsub',0x2f:'dsubu',
                   0x14:'dsllv',0x16:'dsrlv',0x17:'dsrav',0x38:'dsll',0x3a:'dsrl',
                   0x3b:'dsra',0x3c:'dsll32',0x3e:'dsrl32',0x3f:'dsra32'}
        return supported.get(funct)
    if opcode==0x19:return 'daddiu'
    return None

def survey(report:dict)->dict:
    """Fails closed when the report is not from the exact pinned game revision."""
    out={'phase':204,'status':'STARTED','source_phase203_status':report.get('status'),
         'retail_elf_sha256':report.get('retail_elf_sha256'),
         'entry_pc':hx(ENTRY),'original_handler_compiled':False,
         'function_boundary_verified':False,'registry_mutation':False,'game_booted':False,
         'graph_is_conservative_advisory':True,'raw_instruction_words_in_output':False}
    if (report.get('status')!='PINNED_RETAIL_HANDLER_BYTES_AVAILABLE_NOT_RECOMPILED' or
        report.get('retail_elf_sha256')!=PIN or report.get('expected_retail_elf_sha256')!=PIN or
        report.get('target_pc')!=hx(ENTRY) or report.get('stage_registry_audit')!='PASS' or
        report.get('handler_registered') is not False or not isinstance(report.get('rows'),list)):
        return dict(out,status='UNVERIFIED_SOURCE_REPORT_REJECTED')
    rows={}
    try:
        for row in report['rows']:
            pc=int(row['pc'],16);word=int(row['word'],16)
            if pc&3 or pc in rows or not 0<=word<=0xffffffff:raise ValueError('bad or duplicate PC/word')
            
            dec=decode_mips(pc,word)
            if any(row.get(k)!=dec.get(k) for k in ('kind','has_delay_slot','target')):
                raise ValueError('reported branch/control metadata differs from word')
            rows[pc]=row
    except (KeyError,TypeError,ValueError):return dict(out,status='INVALID_INSTRUCTION_ROWS')
    if ENTRY not in rows or len(rows)>192:return dict(out,status='MISSING_ENTRY_OR_OVERSIZED_WINDOW')
    seen=set();work=[ENTRY];exits=[];edges=[];calls=set();delay=set();annul=[];ambiguities=[]
    def enqueue(origin,destination,kind):
        edges.append({'from':hx(origin),'to':hx(destination) if destination is not None else None,'kind':kind})
        if destination is not None:
            if destination<ENTRY or destination not in rows:
                exits.append({'site':hx(origin),'target':hx(destination),'kind':'outside_handler_window'})
            else:work.append(destination)
    while work:
        if len(seen)>MAX_VISITED:return dict(out,status='GRAPH_SIZE_LIMIT_EXCEEDED')
        pc=work.pop()
        if pc in seen:continue
        if pc<ENTRY or pc not in rows:continue
        seen.add(pc);r=rows[pc];kind=r.get('kind','')
        if not r.get('has_delay_slot',False):
            if kind:ambiguities.append({'pc':hx(pc),'problem':'control_without_delay_slot'})
            enqueue(pc,pc+4,'fallthrough');continue
        slot=pc+4
        if slot not in rows:
            ambiguities.append({'pc':hx(pc),'problem':'missing_delay_slot'});continue
        seen.add(slot);delay.add(slot)
        if rows[slot].get('has_delay_slot'):
            ambiguities.append({'pc':hx(pc),'problem':'control_instruction_in_delay_slot'})
        target=int(r['target'],16) if r.get('target') else None
        if kind=='conditional_branch':
            is_likely=likely(int(r['word'],16))
            if is_likely:
                annul.append(hx(pc));enqueue(pc,pc+8,'not_taken_annuls_delay_slot')
                enqueue(pc,target,'taken_after_executing_delay_slot')
            else:
                enqueue(pc,pc+8,'not_taken_after_delay_slot')
                enqueue(pc,target,'taken_after_delay_slot')
        elif kind=='direct_call':
            if target is None: ambiguities.append({'pc':hx(pc),'problem':'direct_call_missing_target'})
            else:calls.add(target)
            enqueue(pc,pc+8,'return_from_external_call_after_delay_slot')
        elif kind=='direct_jump':enqueue(pc,target,'unconditional_jump_after_delay_slot')
        elif kind=='indirect_jump':
            typ='return_via_ra' if int(r['word'],16)==0x03e00008 else 'indirect_dynamic_jump'
            exits.append({'site':hx(pc),'delay_slot_pc':hx(slot),'kind':typ})
        else:
            ambiguities.append({'pc':hx(pc),'problem':'unknown_control_type:'+str(kind)})
    for e in edges:
        if e['to'] and int(e['to'],16) in delay:
            ambiguities.append({'pc':e['from'],'problem':'explicit_control_target_is_another_delay_slot'})
    # The source window includes a previous function and potentially a next function.
    # Only entry-reachable PCs may be called this candidate handler's body.
    returns=sorted(int(x['site'],16) for x in exits if x['kind']=='return_via_ra')
    last_return=max(returns) if returns else None
    boundary_candidate=last_return+8 if last_return is not None else None
    beyond_boundary=sorted(p for p in seen if boundary_candidate is not None and p>=boundary_candidate)
    indep_context=sorted(p for p in rows if p<ENTRY)
    candidate_contiguous=bool(returns and not beyond_boundary)
    partial=[p for p in sorted(seen) if rows[p].get('decoder_coverage')=='partial']
    decodable=[{'pc':hx(p),'mnemonic':advisory_decode(int(rows[p]['word'],16))}
               for p in partial if advisory_decode(int(rows[p]['word'],16))]
    unrecognized=[hx(p) for p in partial if advisory_decode(int(rows[p]['word'],16)) is None]
    external_exits=sorted({e['target'] for e in exits if e['kind']=='outside_handler_window'})
    # Distinguish calls from control transfers: direct calls are dependencies, not exits.
    out.update(status='PINNED_ENTRY_CFG_SURVEY_COMPLETE_NOT_NATIVE_TRANSLATION',
        inspected_instruction_count=len(rows),reachable_word_count=len(seen),
        unreachable_window_word_count=len(rows)-len(seen),
        preceding_context_word_count=len(indep_context),
        reachable_window_first_pc=hx(min(seen)) if seen else None,
        reachable_window_last_pc=hx(max(seen)) if seen else None,
        candidate_ending_after_delay_slot=hx(boundary_candidate) if candidate_contiguous else None,
        candidate_extent_status='UNVERIFIED_FIRST_RETURN_BOUNDARY' if candidate_contiguous else 'AMBIGUOUS_REQUIRES_EXPANSION',
        return_sites=[hx(i) for i in returns],instruction_after_return_reachable=[hx(x) for x in beyond_boundary],
        possible_external_call_dependencies=[hx(x) for x in sorted(calls)],
        external_control_transfer_destinations=external_exits,
        branch_likely_sites=sorted(annul),reachable_delay_slot_count=len(delay),
        reachability_edges=edges,control_exits=exits,
        reachable_partial_decoder_count=len(partial),
        supplementary_scalar_decode_count=len(decodable),supplementary_scalar_decodes=decodable,
        remaining_unrecognized_instructions=unrecognized,
        semantic_translation_blockers={'remaining_unrecognized':len(unrecognized),
            'external_calls':len(calls),'external_control_transfers':len(external_exits),
            'branch_in_delay_slot_or_control_ambiguity':len(ambiguities)},
        ambiguity_details=ambiguities,
        game_function_ready_for_binding=False,
        recommended_next_step='Verify the candidate end and external jump targets, then translate/compare bounded semantic blocks using instruction-level oracle tests; do not bind the original handler yet.')
    return out

def run(source:Path,destination:Path):
    try:report=json.loads(source.read_text(encoding='utf-8-sig'))
    except (OSError,ValueError):report={}
    result=survey(report);destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    return 0 if result['status']=='PINNED_ENTRY_CFG_SURVEY_COMPLETE_NOT_NATIVE_TRANSLATION' else 2
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--phase203-report',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();rc=run(Path(a.phase203_report),Path(a.output));print('PHASE204_ENTRY_CFG',rc);raise SystemExit(rc)
