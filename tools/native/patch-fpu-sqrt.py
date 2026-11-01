#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import argparse
from pathlib import Path
import re

INSTRUCTION = re.compile(r'    // 0x([0-9a-f]+): 0x([0-9a-f]+)[^\n]*\n(    ctx->pc = 0x[0-9a-f]+u;\n)?'
                         r'    ctx->f\[(\d+)\] = FPU_SQRT_S\(ctx->f\[(\d+)\]\);\n')


def operand(word):
    if word >> 26 != 0x11 or (word >> 21) & 31 != 0x10 or word & 63 != 4:
        return None
    return (word >> 6) & 31, (word >> 16) & 31


def patch(text):
    changed = 0

    def fix(match):
        nonlocal changed
        decoded = operand(int(match.group(2), 16))
        if decoded is None:
            return match.group(0)
        fd, ft = decoded
        if int(match.group(4)) != fd:
            raise ValueError(f'SQRT.S at 0x{match.group(1)} writes f{match.group(4)}, expected f{fd}')
        if int(match.group(5)) == ft:
            return match.group(0)
        changed += 1
        return match.group(0).replace(f'FPU_SQRT_S(ctx->f[{match.group(5)}])', f'FPU_SQRT_S(ctx->f[{ft}])')

    return INSTRUCTION.sub(fix, text), changed


def main(argv=None):
    parser = argparse.ArgumentParser(description='Read the R5900 SQRT.S operand from ft.')
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args(argv)
    total = files = 0
    for path in sorted(args.directory.glob('*.cpp')):
        text = path.read_text()
        if 'FPU_SQRT_S(' not in text:
            continue
        corrected, changed = patch(text)
        if changed:
            path.write_text(corrected)
            total += changed
            files += 1
    print(f'Corrected {total} SQRT.S operands in {files} files.')


if __name__ == '__main__':
    main()
