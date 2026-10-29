#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Use the EE's low 64-bit register for signed branch predicates."""
import argparse
from pathlib import Path
import re

ANNOTATION = re.compile(r'// 0x([0-9a-fA-F]+): 0x([0-9a-fA-F]+)\s+(bltzall|bgezall|bltzal|bgezal|bltzl|bgezl|blezl|bgtzl|bltz|bgez|blez|bgtz)\b')

def corrected(source):
    replacements = []
    annotations = list(ANNOTATION.finditer(source))
    for match in annotations:
        # Branches decoded inside another branch's delay slot do not have an
        # independent predicate. Their existing translator handling is retained.
        if '(Delay Slot)' in source[match.end():source.find('\n', match.end())]:
            continue
        address, word, mnemonic = int(match[1], 16), int(match[2], 16), match[3]
        opcode, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        expected = {6: 'blez', 7: 'bgtz', 22: 'blezl', 23: 'bgtzl'}.get(opcode)
        if opcode == 1:
            expected = {0:'bltz',1:'bgez',2:'bltzl',3:'bgezl',16:'bltzal',17:'bgezal',18:'bltzall',19:'bgezall'}.get(rt)
        if expected != mnemonic:
            raise ValueError(f'Unexpected signed branch opcode at {address:#x}')
        operator = '<' if mnemonic.startswith('bltz') else '>=' if mnemonic.startswith('bgez') else '<=' if mnemonic.startswith('blez') else '>'
        predicate = re.compile(r'const bool branch_taken_0x'+format(address,'x')+r' = \(GPR_S(32|64)\(ctx, '+str(rs)+r'\) '+re.escape(operator)+r' 0\);',re.I)
        found = predicate.search(source, match.end(), min(len(source), match.end()+1400))
        if not found:
            raise ValueError(f'Cannot identify the signed branch predicate at {address:#x}')
        if found[1] == '32':
            replacements.append((found.start(1),found.end(1),'64'))
    for start,end,value in reversed(replacements):
        source = source[:start]+value+source[end:]
    return source, len(replacements), len(annotations)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True)
    args=parser.parse_args()
    pending=[];count=total=0
    for path in sorted(args.directory.glob('*.cpp')):
        original=path.read_text();updated,n,seen=corrected(original);count+=n;total+=seen
        if updated!=original:pending.append((path,updated))
    if not total:raise SystemExit('No signed EE branch sites were found')
    for path,updated in pending:path.write_text(updated)
    print(f'Verified {total} signed EE branches; corrected {count} predicates in {len(pending)} translated files.')

if __name__=='__main__':main()
