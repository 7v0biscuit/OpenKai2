"""Conservative entry-window audit of two unimplemented retail helpers.

This is opcode classification and candidate discovery, not translated helper execution.
Retail bytes come ONLY from user's SHA-256-matching original game input.
"""
from __future__ import annotations
import hashlib,struct,subprocess
from .retail_elf import MipsELF,EXPECTED_SHA256,AuditError

HELPERS=(0x00113400,0x00113410)
WINDOW_WORDS=24

def classification(pc,w):
    op=(w>>26)&63;rs=(w>>21)&31;rt=(w>>16)&31;fn=w&63
    br=(pc+4+(((w&0xffff)-0x10000 if w&0x8000 else w&0xffff)<<2))&0xffffffff
    jt=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
    if not w:return ('nop',0,False,False)
    if op==2:return ('jump',jt,True,False)
    if op==3:return ('call',jt,True,False)
    if op==0 and fn==8:return ('return' if rs==31 else 'indirect_jump',0,True,False)
    if op==0 and fn==9:return ('indirect_call',0,True,False)
    if op==1 and rt in (0,1,2,3,16,17,18,19):return ('branch_link' if rt>=16 else 'branch',br,True,rt in (2,3,18,19))
    if op in (4,5,6,7,20,21,22,23):return ('branch',br,True,op>=20)
    if op in (26,27,55) or 32<=op<=39:return ('load',0,False,False)
    if op in (42,43,63) or 40<=op<=47:return ('store',0,False,False)
    if op in (0,28) or 8<=op<=15:return ('scalar',0,False,False)
    return ('unclassified',0,False,False)

def compare_native(exe,rows):
    input_stream=''.join('%08X %08X\n'%(pc,w) for pc,w in rows)
    cp=subprocess.run([str(exe)],input=input_stream,text=True,capture_output=True,timeout=15)
    if cp.returncode:raise ValueError('native decode failed, code '+str(cp.returncode)+' stderr='+cp.stderr[:150])
    lines=cp.stdout.splitlines()
    if len(lines)!=len(rows):raise ValueError('native line count mismatch')
    differences=[]
    for (pc,w),line in zip(rows,lines):
        a=line.split()
        if len(a)!=5:raise ValueError('malformed decoder output')
        got=(a[1],int(a[2],16),bool(int(a[3])),bool(int(a[4])))
        want=classification(pc,w)
        if int(a[0],16)!=pc or got!=want:
            differences.append({'pc':'0x%08X'%pc,'want':str(want),'got':str(got)})
    return differences

def synthetic_cases():
    # Independent set of encoded examples, not borrowed from retail executable.
    return [(0x113400,0x00000000),(0x113400,0x03e00008),
        (0x113400,0x00800008),(0x113400,0x0080f809),
        (0x113400,0x0c044d04),(0x113400,0x08044d04),
        (0x113400,0x10a0ffff),(0x113400,0x14a00002),
        (0x113400,0x54a00002),(0x113400,0x50a00002),
        (0x113400,0x04110002),(0x113400,0x04020002),
        (0x113400,0x8c820000),(0x113400,0xdc820000),
        (0x113400,0xac820000),(0x113400,0xfc820000),
        (0x113400,0x27bdfff0),(0x113400,0x00431021),
        (0x113400,0x48000000),(0x113400,0x70000002)]

def inspect_verified_elf(blob,exe):
    digest=hashlib.sha256(blob).hexdigest()
    if digest!=EXPECTED_SHA256:raise AuditError('Retail ELF SHA-256 identity mismatch')
    elf=MipsELF(blob)
    reports=[]
    for start in HELPERS:
        rows=[]
        for n in range(WINDOW_WORDS):
            pc=start+n*4
            segment=next((s for s in elf.segments if s['vaddr']<=pc and pc+4<=s['vaddr']+s['filesz'] and (s['flags']&1)),None)
            if segment is None:raise AuditError('Helper address not file-backed executable memory')
            rows.append((pc,elf.word(pc)))
        discrepancies=compare_native(exe,rows)
        raw=b''.join(struct.pack('<I',w) for _,w in rows)
        records=[]
        for pc,w in rows:
            cat,target,delay,likely=classification(pc,w)
            records.append({'pc':f'0x{pc:08X}','word':f'0x{w:08X}',
                'category':cat,'target':f'0x{target:08X}' if target else None,
                'delay_slot':delay,'branch_likely':likely})
        reports.append({'entry_pc':f'0x{start:08X}', 'window_end_exclusive':f'0x{start+WINDOW_WORDS*4:08X}',
           'window_sha256':hashlib.sha256(raw).hexdigest(),
           'window_size_instructions':len(rows),
           'unclassified_opcode_count':sum(x['category']=='unclassified' for x in records),
           'candidate_returns':[x['pc'] for x in records if x['category']=='return'],
           'direct_external_targets':sorted({x['target'] for x in records if x['category'] in ('jump','call') and x['target']}),
           'branch_likely_sites':[x['pc'] for x in records if x['branch_likely']],
           'native_decoder_parity_pass':not discrepancies,'native_decoder_mismatches':discrepancies,
           'function_extent_verified':False,'semantic_translation_completed':False,
           'rows':records})
    return {'component':'helper_window','status':'PINNED_RETAIL_HELPER_ENTRY_WINDOWS_CAPTURED_NOT_TRANSLATED',
       'retail_elf_sha256':EXPECTED_SHA256,'original_handler_complete':False,
       'helper_functions_recompiled':0,'helper_functions_registered':0,'game_booted':False,
       'evidence_type':'exact_sha256_original_mips_executable',
       'instruction_word_excerpt_in_results':True,
       'windows_are_not_proven_function_boundaries':True,
       'native_decoder_parity_pass':all(x['native_decoder_parity_pass'] for x in reports),
       'helpers':reports}
