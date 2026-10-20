"""bounded original handler under a VIRTUAL IRQ11 gate.

Oracle uses the independently decoded/pinned retail handler, original post-call
instructions and helper wrappers. Gate policy is NOT PS2 INTC behavior.
"""
from __future__ import annotations
import subprocess
from .helper_continuation import cases as continuation_cases, oracle as double_oracle, synthetic_post_words,validate_post_words
from .wrapper_chain import synthetic_instructions


def cases(handler_rows=None):
    for case, first, second, boundary in continuation_cases(handler_rows):
        for flags in range(16):
            yield case, first, second, boundary, flags


def expected(case, first, second, boundary, flags, words=None, post=None):
    queued = bool(flags & 1)
    dispatch = int(queued and bool(flags & 2) and bool(flags & 4))
    pending = int(queued and not (dispatch and bool(flags & 8)))
    if not dispatch:
        return (flags, dispatch, pending, 0, *(0 for _ in range(24)), 0)
    if words is None:words=synthetic_instructions()
    if post is None:post=synthetic_post_words()
    chain=double_oracle(case, first, second, boundary, words, post)
    helper_calls = 1 if chain[3]==2 else 2
    return (flags,dispatch,pending,1,*chain,helper_calls)


def compare_native(exe,handler_rows=None,words=None,post=None):
    if words is None:words=synthetic_instructions()
    if post is None:post=synthetic_post_words()
    validate_post_words(post)
    inputs=list(cases(handler_rows))
    payload=[]
    for case,first,second,boundary,flags in inputs:
        payload.append(' '.join((case[0],hex(flags),*(hex(v) for v in case[1:]),
                                 *(hex(v) for v in first),*(hex(v) for v in second)))+'\n')
    run=subprocess.run([str(exe)],input=''.join(payload),capture_output=True,text=True,timeout=240)
    if run.returncode:
        raise ValueError(f'IrqHandler native rc={run.returncode}: {run.stderr[:350]}')
    lines=run.stdout.splitlines()
    if len(lines)!=len(inputs):
        raise ValueError(f'IrqHandler output lines={len(lines)} expected={len(inputs)}')
    errs=[];dispatched=suppressed=ack=first_poll=second_poll=modeled_returns=0
    by_call={'0x00113400':0,'0x00113410':0}
    for ix,((case,first,second,boundary,flags),line) in enumerate(zip(inputs,lines)):
        tokens=line.split()
        if len(tokens)!=30 or tokens[0]!='IRQ_HANDLER':
            raise ValueError(f'IrqHandler output columns {len(tokens)} at case {ix}: {line[:150]}')
        try:got=tuple(int(v,16) for v in tokens[1:])
        except ValueError as exc:raise ValueError(f'IrqHandler invalid hexadecimal case {ix}') from exc
        want=expected(case,first,second,boundary,flags,words,post)
        if got!=want:
            errs.append({'case':case[0],'flags':flags,'index':ix,
                         'want':list(map(hex,want)), 'got':list(map(hex,got))})
        if want[1]:
            dispatched+=1
            by_call[f'0x{want[5]:08X}']+=1
            if want[2]==0:ack+=1
            if want[7]==2:first_poll+=1
            elif want[21]==2:second_poll+=1
            elif want[21]==1:modeled_returns+=1
        else:suppressed+=1
    return {'component':'irq_handler','case_count':len(inputs), 'passed':not errs and bool(inputs),
       'mismatches':errs[:8],'dispatched_original_handler_bounded_blocks':dispatched,
       'suppressed_without_handler_execution':suppressed,
       'explicit_acknowledgments':ack,'first_helper_poll_stops':first_poll,
       'second_helper_poll_stops':second_poll,'second_helper_modeled_returns':modeled_returns,
       'original_call_targets':by_call,'native_function_registry_additions':0,
       'source':'sha_pinned_retail_opcode_interpreter' if handler_rows is not None else 'synthetic_opcode_interpreter',
       'actual_original_guest_irq11_dispatch':False,'guest_handler_complete':False,
       'real_hardware_interrupt_timing_verified':False,'hardware_effects':'virtual_memory_and_cpu_only',
       'game_bootable':False}
