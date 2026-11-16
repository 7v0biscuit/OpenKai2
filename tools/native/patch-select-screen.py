#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Let the translated character select screen grow past 30 cells, and let
character IDs 53 to 62 have records of their own.

Each edit swaps a constant in one translated instruction for a call into
src/mods/select_screen.h. The calls return the original constants unless a
roster mod adds select-screen spots or characters, so the unmodded game is
unchanged.
"""
import argparse
import re
from pathlib import Path

MARKER = '// OpenKai2: select-screen cells come from openkai2::mods.'
INCLUDE = '#include "mods/select_screen.h"\n'
CELLS = '(int32_t)openkai2::mods::select_cells())'
LAST = 'openkai2::mods::select_last())'

EDITS = []
for address, name in [(0x2aa0e0, 'sub_002A9FA0_0x2a9fa0'), (0x2aa16c, 'sub_002AA110_0x2aa110'),
                      (0x2aa214, 'sub_002AA110_0x2aa110'), (0x2aa264, 'sub_002AA110_0x2aa110'),
                      (0x2aa6b4, 'sub_002AA5B0_0x2aa5b0'), (0x2aaa8c, 'sub_002AAA20_0x2aaa20'),
                      (0x2aab2c, 'sub_002AAAB0_0x2aaab0'), (0x2aac38, 'sub_002AABD0_0x2aabd0'),
                      (0x2aad58, 'sub_002AAD00_0x2aad00'), (0x2aae38, 'sub_002AADD0_0x2aadd0'),
                      (0x2aaf58, 'sub_002AAF00_0x2aaf00')]:
    EDITS.append((name, address, '(int32_t)30)', CELLS))
for address, name in [(0x2ab0ec, 'sub_002AAFD0_0x2aafd0'), (0x2ab35c, 'sub_002AB310_0x2ab310'),
                      (0x2ac404, 'sub_002ABF10_0x2abf10'), (0x2ac918, 'sub_002ABF10_0x2abf10'),
                      (0x2ac99c, 'sub_002ABF10_0x2abf10'), (0x2acab0, 'sub_002ABF10_0x2abf10'),
                      (0x2acbec, 'sub_002ABF10_0x2abf10'), (0x2ad2a8, 'sub_002AD160_0x2ad160'),
                      (0x2ad36c, 'sub_002AD160_0x2ad160'), (0x2ad934, 'sub_002AD8E0_0x2ad8e0')]:
    EDITS.append((name, address, '29)', LAST))
EDITS.append(('sub_002ABF10_0x2abf10', 0x2acb38, '(int32_t)29)', '(int32_t)' + LAST))
EDITS.append(('sub_002ABF10_0x2abf10', 0x2aca7c, '1600)', 'openkai2::mods::select_offset(1600))'))
for address, name, offset in [(0x2a9b60, 'sub_002A9B40_0x2a9b40', 1652), (0x2ab28c, 'sub_002AB1F0_0x2ab1f0', 1652),
                              (0x2ab2a4, 'sub_002AB1F0_0x2ab1f0', 1652), (0x2ab2b0, 'sub_002AB1F0_0x2ab1f0', 1652),
                              (0x2ab2bc, 'sub_002AB1F0_0x2ab1f0', 1652), (0x2ab46c, 'sub_002AB410_0x2ab410', 1652),
                              (0x2ab864, 'sub_002AB540_0x2ab540', 1652), (0x2ab91c, 'sub_002AB540_0x2ab540', 1652),
                              (0x2ab994, 'sub_002AB540_0x2ab540', 1652), (0x2ac5c8, 'sub_002ABF10_0x2abf10', 1652),
                              (0x2ad420, 'sub_002AD3D0_0x2ad3d0', 1652), (0x2ad42c, 'sub_002AD3D0_0x2ad3d0', 1652),
                              (0x2adb60, 'sub_002ADAE0_0x2adae0', 1652), (0x2adf50, 'sub_002ADEE0_0x2adee0', 1648),
                              (0x2ae09c, 'sub_002ADF90_0x2adf90', 1648), (0x2ae0a4, 'sub_002ADF90_0x2adf90', 1652),
                              (0x2adffc, 'sub_002ADF90_0x2adf90', 1664), (0x2ae020, 'sub_002ADF90_0x2adf90', 1664)]:
    EDITS.append((name, address, f'{offset})', f'openkai2::mods::select_offset({offset}))'))
for address, name, register in [(0x2abf7c, 'sub_002ABF10_0x2abf10', 4), (0x2ae160, 'sub_002ADF90_0x2adf90', 5)]:
    EDITS.append((name, address, f'SET_GPR_S32(ctx, {register}, (int32_t)((uint32_t)8192 << 16));',
                  f'SET_GPR_U64(ctx, {register}, openkai2::mods::select_random_bit());'))
for address, name, register in [(0x2abfb8, 'sub_002ABF10_0x2abf10', 7), (0x2ae1a0, 'sub_002ADF90_0x2adf90', 8)]:
    EDITS.append((name, address, f'GPR_U64(ctx, {register}));',
                  f'GPR_U64(ctx, {register}) | openkai2::mods::select_extra_mask());'))
EDITS.append(('sub_002A9E10_0x2a9e10', 0x2a9e64, 'ADD32(GPR_U32(ctx, 7), 36)',
              'ADD32(openkai2::mods::select_icon(GPR_U32(ctx, 7)), 36)'))
DELAY_SLOTS = {0x2abd64, 0x2abeb4, 0x2ac53c}
for name, address, register in [('sub_002AA6E0_0x2aa6e0', 0x2aa880, 3), ('sub_002ABC70_0x2abc70', 0x2abd64, 6),
                                ('sub_002ABDC0_0x2abdc0', 0x2abeb4, 6), ('sub_002ABF10_0x2abf10', 0x2ac53c, 2)]:
    EDITS.append((name, address, f'ADD32(GPR_U32(ctx, {register}), 68)',
                  f'openkai2::mods::select_label(GPR_U32(ctx, {register}))'))
for address, name, register in [(0x2af114, 'sub_002AF000_0x2af000', 18), (0x2af5a4, 'sub_002AF4E0_0x2af4e0', 16)]:
    EDITS.append((name, address, 'ADD32(GPR_U32(ctx, 0), 36)',
                  f'openkai2::mods::preview_id(GPR_U32(ctx, {register}))'))
for address in (0x2aec08, 0x2aec0c, 0x2aec14):
    EDITS.append(('sub_002AEBC0_0x2aebc0', address, 'GPR_U32(ctx, 6)',
                  'openkai2::mods::preview_row(GPR_U32(ctx, 6))'))
for name, register, table, scale, adds in [('sub_002AF000_0x2af000', 18, 0x2af144, 0x2af14c, (0x2af154, 0x2af164)),
                                           ('sub_002AF4E0_0x2af4e0', 16, 0x2af5d8, 0x2af5e8, (0x2af5ec, 0x2af5fc))]:
    row = f'openkai2::mods::preview_row(GPR_U32(ctx, {register}))'
    EDITS.append((name, table, f'ADD32(GPR_U32(ctx, {register}), GPR_U32(ctx, 2))',
                  f'ADD32({row}, GPR_U32(ctx, 2))'))
    EDITS.append((name, scale, f'SLL32(GPR_U32(ctx, {register}), 2)', f'SLL32({row}, 2)'))
    for address in adds:
        EDITS.append((name, address, f'ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, {register}))',
                      f'ADD32(GPR_U32(ctx, 2), {row})'))

for name, register, scale, add, shift in [('sub_0020B340_0x20b340', 6, 0x20b368, 0x20b36c, 1),
                                          ('sub_0020BA60_0x20ba60', 9, 0x20bb98, 0x20bb9c, 1),
                                          ('sub_0020C3B0_0x20c3b0', 5, 0x20c438, 0x20c43c, 1),
                                          ('sub_00200AC0_0x200ac0', 5, 0x200b2c, 0x200b30, 2)]:
    row = f'openkai2::mods::id_table_row(GPR_U32(ctx, {register}))'
    EDITS.append((name, scale, f'SLL32(GPR_U32(ctx, {register}), {shift})', f'SLL32({row}, {shift})'))
    EDITS.append((name, add, f'GPR_U32(ctx, {register})))', f'{row}))'))

for address, count in [(0x24a14c, 32), (0x24a184, 36), (0x24a1c0, 36)]:
    EDITS.append(('sub_0024A0C0_0x24a0c0', address, 'SLL32(GPR_U32(ctx, 3), 2)',
                  f'SLL32(openkai2::mods::voice_row(GPR_U32(ctx, 3), {count}), 2)'))

ROSTER_RECORDS = [
    ('sub_001D6F20_0x1d6f20', [0x1d6fd4]), ('sub_001E7570_0x1e7570', [0x1e767c]),
    ('sub_001EA290_0x1ea290', [0x1ea3f0]), ('sub_0022F410_0x22f410', [0x22f554]),
    ('sub_002401D0_0x2401d0', [0x24050c]),
    ('sub_00246780_0x246780', [0x246a60 + 0x14 * i for i in range(8)]),
    ('sub_0024EE30_0x24ee30', [0x24f310, 0x24f708]),
    ('sub_00260360_0x260360', [0x260580 + 0x14 * i for i in range(8)]),
    ('sub_00264090_0x264090', [0x264270 + 0x14 * i for i in range(8)]),
    ('sub_00264450_0x264450', [0x2646b0 + 0x14 * i for i in range(8)]),
    ('sub_002AF000_0x2af000', [0x2af1d8]), ('sub_002AF4E0_0x2af4e0', [0x2af6d4]),
    ('sub_002B5320_0x2b5320', [0x2b5488]),
]
for name, addresses in ROSTER_RECORDS:
    for address in addresses:
        EDITS.append((name, address, '(int32_t)result); }', 'openkai2::mods::roster_offset((int32_t)result)); }'))
for name, address, register in [('sub_0020BD20_0x20bd20', 0x20bd2c, 5), ('sub_0020C3B0_0x20c3b0', 0x20c45c, 4),
                                ('sub_0020C3B0_0x20c3b0', 0x20c4d4, 4)]:
    EDITS.append((name, address, f'SLL32(GPR_U32(ctx, {register}), 2)',
                  f'SLL32(openkai2::mods::roster_row(GPR_U32(ctx, {register}), 53), 2)'))
for address in [0x2b1a38, 0x2b1a9c, 0x2b1b98, 0x2b1c00, 0x2b1cf8, 0x2b1d5c, 0x2b1e60, 0x2b1ec4, 0x2b1fc0, 0x2b2024,
                0x2b2130, 0x2b2210]:
    EDITS.append(('sub_002B19B0_0x2b19b0', address, 'ADD32(GPR_U32(ctx, 0), 29)',
                  'ADD32(GPR_U32(ctx, 0), openkai2::mods::select_last())'))
EDITS.append(('sub_002B45B0_0x2b45b0', 0x2b4708, 'ADD32(GPR_U32(ctx, 0), 29)',
              'ADD32(GPR_U32(ctx, 0), openkai2::mods::select_last())'))
PREVIOUS = {address: f'SLL32(openkai2::mods::id_table_row(GPR_U32(ctx, {register})), 2)'
            for address, register in [(0x20bd2c, 5), (0x20c45c, 4), (0x20c4d4, 4)]}
EDITS.append(('sub_001C7940_0x1c7940', 0x1c7988, 'SLL32(GPR_U32(ctx, 18), 2)',
              'SLL32(openkai2::mods::portrait_row(GPR_U32(ctx, 18), 57), 2)'))
PREVIOUS[0x1c7988] = 'SLL32(openkai2::mods::roster_row(GPR_U32(ctx, 18), 57), 2)'
for address, register, field in [(0x2119ac, 10, '4480'), (0x211a08, 8, '4294942412')]:
    load = f'READ16(ADD32(GPR_U32(ctx, {4 if register == 10 else 1}), {field}))'
    EDITS.append(('sub_00211970_0x211970', address, f'(int16_t){load}',
                  f'(int32_t)openkai2::mods::roster_row((uint32_t)(int32_t)(int16_t){load}, 53)'))
EDITS.append(('sub_00267DC0_0x267dc0', 0x267e9c, 'ADD32(GPR_U32(ctx, 19), 1)',
              'ADD32(openkai2::mods::portrait_row(GPR_U32(ctx, 19), 43), 1)'))
PREVIOUS[0x267e9c] = 'ADD32(openkai2::mods::roster_row(GPR_U32(ctx, 19), 43), 1)'


def patch(text, edits):
    """Applies the edits for one file. Each edit already made is checked, not
    changed, so files patched by an earlier version gain only the new edits."""
    for _, address, wanted, replacement in edits:
        comment = f'    // 0x{address:x}:'
        starts = []
        at = text.find(comment)
        while at >= 0:
            starts.append(at)
            at = text.find(comment, at + 1)
        if not starts or (len(starts) > 1 and address not in DELAY_SLOTS):
            raise ValueError(f'Instruction 0x{address:x} is missing or repeated')
        for start in reversed(starts):
            end = text.find('// 0x', start + len(comment))
            end = len(text) if end < 0 else text.rfind('\n', start, end)
            block = text[start:end]
            if block.count(replacement) == 1:
                continue
            original = wanted
            if address in PREVIOUS and block.count(PREVIOUS[address]) == 1:
                original = PREVIOUS[address]
            if block.count(original) != 1:
                raise ValueError(f'Instruction 0x{address:x} does not hold {original!r} once')
            text = text[:start] + block.replace(original, replacement) + text[end:]
    if MARKER not in text:
        text = INCLUDE + MARKER + '\n' + text
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args(argv)
    files = {}
    for edit in EDITS:
        files.setdefault(edit[0], []).append(edit)
    for name, edits in files.items():
        path = args.directory / f'{name}.cpp'
        original = path.read_text()
        corrected = patch(original, edits)
        if corrected != original:
            path.write_text(corrected)
    print(f'Verified {len(EDITS)} select-screen cell edits in {len(files)} functions.')


if __name__ == '__main__':
    main()
