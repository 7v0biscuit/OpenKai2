#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import argparse
from pathlib import Path
import re

SITES = (0x14a004, 0x14adb8, 0x14b96c)
LOAD = 'runtime->Load64(rdram, ctx, 0x12001000u)'
DIRECT = 'runtime->memory().gs().csr.load(std::memory_order_acquire)'


def site_pattern(address):
    return re.compile(r'    // 0x%x: 0xdc2[23]1000 [^\n]*\n    ctx->pc = 0x%xu;\n    SET_GPR_U64\(ctx, ([23]), ' % (address, address)
                      + re.escape(LOAD) + r'\);\n'
                      + r'    // 0x%x: 0x[0-9a-f]+ +dsrl +\$v[01], \$v[01], 13\n(?:    [^/\n][^\n]*\n){1,2}' % (address + 4)
                      + r'    // 0x%x: 0x30[0-9a-f]{2}0001 +andi +\$v[01], \$v[01], 0x1\n' % (address + 8))


def patch(text):
    changed = 0
    for address in SITES:
        pattern = site_pattern(address)

        def fix(match):
            nonlocal changed
            changed += 1
            return match.group(0).replace(LOAD, DIRECT, 1)

        text = pattern.sub(fix, text)
    return text, changed


def main(argv=None):
    parser = argparse.ArgumentParser(description='Read the CSR FIELD bit without waiting for queued GS work.')
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args(argv)
    total = 0
    for path in sorted(args.directory.glob('*.cpp')):
        text = path.read_text()
        if LOAD not in text and DIRECT not in text:
            continue
        if not any('0x%xu;' % address in text for address in SITES):
            continue
        patched, changed = patch(text)
        if changed:
            path.write_text(patched)
        total += changed + sum(text.count('ctx->pc = 0x%xu;\n    SET_GPR_U64(ctx, %d, %s' % (a, r, DIRECT))
                               for a in SITES for r in (2, 3))
    if total != len(SITES):
        raise SystemExit(f'Expected {len(SITES)} CSR FIELD reads, found {total}.')
    print(f'Read the CSR FIELD bit directly at {total} sites.')


if __name__ == '__main__':
    main()
