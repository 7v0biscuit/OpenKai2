#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Let the translated character select screen grow past 30 cells.

Each edit swaps a constant in one translated instruction for a call into
src/mods/select_screen.h. The calls return the original constants unless a
roster mod adds select-screen spots, so the unmodded screen is unchanged.
"""
import argparse
import re
from pathlib import Path

MARKER = '// OpenKai2: select-screen cells come from openkai2::mods.'
INCLUDE = '#include "mods/select_screen.h"\n'
CELLS = '(int32_t)openkai2::mods::select_cells())'
LAST = 'openkai2::mods::select_last())'

# (file, address, original text, replacement)
EDITS = []
for address, name in [(0x2aa0e0, 'sub_002A9FA0_0x2a9fa0'), (0x2aa16c, 'sub_002AA110_0x2aa110'),
                      (0x2aa214, 'sub_002AA110_0x2aa110'), (0x2aa264, 'sub_002AA110_0x2aa110'),
                      (0x2aa6b4, 'sub_002AA5B0_0x2aa5b0'), (0x2aaa8c, 'sub_002AAA20_0x2aaa20'),
                      (0x2aab2c, 'sub_002AAAB0_0x2aaab0'), (0x2aac38, 'sub_002AABD0_0x2aabd0'),
                      (0x2aad58, 'sub_002AAD00_0x2aad00'), (0x2aae38, 'sub_002AADD0_0x2aadd0'),
                      (0x2aaf58, 'sub_002AAF00_0x2aaf00')]:
    EDITS.append((name, address, '(int32_t)30)', CELLS))          # loops over the cells
for address, name in [(0x2ab0ec, 'sub_002AAFD0_0x2aafd0'), (0x2ab35c, 'sub_002AB310_0x2ab310'),
                      (0x2ac404, 'sub_002ABF10_0x2abf10'), (0x2ac918, 'sub_002ABF10_0x2abf10'),
                      (0x2ac99c, 'sub_002ABF10_0x2abf10'), (0x2acab0, 'sub_002ABF10_0x2abf10'),
                      (0x2acbec, 'sub_002ABF10_0x2abf10'), (0x2ad2a8, 'sub_002AD160_0x2ad160'),
                      (0x2ad36c, 'sub_002AD160_0x2ad160'), (0x2ad934, 'sub_002AD8E0_0x2ad8e0')]:
    EDITS.append((name, address, '29)', LAST))                    # the Random cell
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
    EDITS.append((name, address, f'{offset})', f'openkai2::mods::select_offset({offset}))'))  # fields after the cells
for address, name, register in [(0x2abf7c, 'sub_002ABF10_0x2abf10', 4), (0x2ae160, 'sub_002ADF90_0x2adf90', 5)]:
    EDITS.append((name, address, f'SET_GPR_S32(ctx, {register}, (int32_t)((uint32_t)8192 << 16));',
                  f'SET_GPR_U64(ctx, {register}, openkai2::mods::select_random_bit());'))  # Random's unlock bit
for address, name, register in [(0x2abfb8, 'sub_002ABF10_0x2abf10', 7), (0x2ae1a0, 'sub_002ADF90_0x2adf90', 8)]:
    EDITS.append((name, address, f'GPR_U64(ctx, {register}));',
                  f'GPR_U64(ctx, {register}) | openkai2::mods::select_extra_mask());'))  # added cells are open
EDITS.append(('sub_002A9E10_0x2a9e10', 0x2a9e64, 'ADD32(GPR_U32(ctx, 7), 36)',
              'ADD32(openkai2::mods::select_icon(GPR_U32(ctx, 7)), 36)'))            # the portrait sprite
for address, name, register in [(0x2af114, 'sub_002AF000_0x2af000', 18), (0x2af5a4, 'sub_002AF4E0_0x2af4e0', 16)]:
    EDITS.append((name, address, 'ADD32(GPR_U32(ctx, 0), 36)',
                  f'openkai2::mods::preview_id(GPR_U32(ctx, {register}))'))           # preview IDs above 36
for address in (0x2aec08, 0x2aec0c, 0x2aec14):
    EDITS.append(('sub_002AEBC0_0x2aebc0', address, 'GPR_U32(ctx, 6)',
                  'openkai2::mods::preview_row(GPR_U32(ctx, 6))'))                    # preview pose rows


def patch(text, edits):
    """Applies the edits for one file; text that is already patched is checked, not changed."""
    patched = MARKER in text
    for _, address, original, replacement in edits:
        comment = f'    // 0x{address:x}:'
        start = text.find(comment)
        if start < 0 or text.find(comment, start + 1) >= 0:
            raise ValueError(f'Instruction 0x{address:x} is missing or repeated')
        end = text.find('\n    // 0x', start + len(comment))
        end = len(text) if end < 0 else end
        block = text[start:end]
        wanted = replacement if patched else original
        if block.count(wanted) != 1:
            raise ValueError(f'Instruction 0x{address:x} does not hold {wanted!r} once')
        if not patched:
            text = text[:start] + block.replace(original, replacement) + text[end:]
    if not patched:
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
