#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Let the translated game hold more than 520 capsules.

Each edit swaps a constant in one translated instruction for a call into
src/mods/capsules.h. The calls return the original constants unless a mod
supplies a capsule table with more than 520 capsules, so the unmodded game is
unchanged.
"""
import argparse
import re
from pathlib import Path

MARKER = '// OpenKai2: capsule counts come from openkai2::mods.'
INCLUDE = '#include "mods/capsules.h"\n'

TABLE = [
    ('sub_001DC460_0x1dc460', 0x1dc69c, 19104, 0x694AA0),
    ('sub_001EA8F0_0x1ea8f0', 0x1ea948, 19104, 0x694AA0),
    ('sub_001EAA50_0x1eaa50', 0x1eaad8, 19104, 0x694AA0),
    ('sub_001ED8B0_0x1ed8b0', 0x1ed974, 19104, 0x694AA0),
    ('sub_001EE140_0x1ee140', 0x1ee1a8, 19104, 0x694AA0),
    ('sub_001EE680_0x1ee680', 0x1ee6e4, 19104, 0x694AA0),
    ('sub_001EE900_0x1ee900', 0x1ee914, 19104, 0x694AA0),
    ('sub_001EECB0_0x1eecb0', 0x1eecc4, 19104, 0x694AA0),
    ('sub_001EF060_0x1ef060', 0x1ef068, 19104, 0x694AA0),
    ('sub_001EFBD0_0x1efbd0', 0x1efc80, 19104, 0x694AA0),
    ('sub_001EFE00_0x1efe00', 0x1efea0, 19104, 0x694AA0),
    ('sub_001F0290_0x1f0290', 0x1f0384, 19119, 0x694AAF),
    ('sub_001F4B40_0x1f4b40', 0x1f5120, 19104, 0x694AA0),
    ('sub_001F8C70_0x1f8c70', 0x1fab2c, 19104, 0x694AA0),
    ('sub_00233BF0_0x233bf0', 0x233e5c, 19104, 0x694AA0),
    ('sub_00233FD0_0x233fd0', 0x23401c, 19072, 0x694A80),
    ('sub_00265E20_0x265e20', 0x265f44, 19104, 0x694AA0),
    ('sub_00265E20_0x265e20', 0x26614c, 19104, 0x694AA0),
    ('sub_00267150_0x267150', 0x267158, 19125, 0x694AB5),
    ('sub_00267170_0x267170', 0x267178, 19114, 0x694AAA),
    ('sub_00267490_0x267490', 0x2674a4, 19114, 0x694AAA),
    ('sub_0027ACF0_0x27acf0', 0x27b31c, 19117, 0x694AAD),
    ('sub_00283960_0x283960', 0x2839cc, 19104, 0x694AA0),
    ('sub_00283B30_0x283b30', 0x283b94, 19104, 0x694AA0),
    ('sub_00284E90_0x284e90', 0x284ee0, 19104, 0x694AA0),
    ('sub_00284E90_0x284e90', 0x284f6c, 19104, 0x694AA0),
    ('sub_00287B50_0x287b50', 0x287c20, 19104, 0x694AA0),
    ('sub_00287DB0_0x287db0', 0x287e34, 19104, 0x694AA0),
    ('sub_00288050_0x288050', 0x288170, 19104, 0x694AA0),
    ('sub_00288050_0x288050', 0x2882b4, -29856, 0x698B60),
    ('sub_00288050_0x288050', 0x2883a8, -30528, 0x6988C0),
    ('sub_00288050_0x288050', 0x2884a4, -30496, 0x6988E0),
    ('sub_00288050_0x288050', 0x2885a8, -30464, 0x698900),
    ('sub_00288050_0x288050', 0x2886fc, -30400, 0x698940),
    ('sub_00288050_0x288050', 0x2887f4, -30368, 0x698960),
    ('sub_00289060_0x289060', 0x2891e8, 19104, 0x694AA0),
    ('sub_00289DF0_0x289df0', 0x28a218, 19117, 0x694AAD),
    ('sub_00289DF0_0x289df0', 0x28a30c, 19104, 0x694AA0),
    ('sub_0028B8C0_0x28b8c0', 0x28b8d4, 19136, 0x694AC0),
    ('sub_0028B9C0_0x28b9c0', 0x28b9d8, 19104, 0x694AA0),
    ('sub_0028B9C0_0x28b9c0', 0x28ba04, 19136, 0x694AC0),
    ('sub_0028BB10_0x28bb10', 0x28bb24, 19104, 0x694AA0),
    ('sub_0028BB10_0x28bb10', 0x28bb6c, 19104, 0x694AA0),
    ('sub_0028BB10_0x28bb10', 0x28bc60, 19104, 0x694AA0),
    ('sub_0028C260_0x28c260', 0x28c2d0, 19104, 0x694AA0),
    ('sub_0028C960_0x28c960', 0x28ca2c, 19115, 0x694AAB),
    ('sub_0028CBA0_0x28cba0', 0x28cc0c, 19112, 0x694AA8),
    ('sub_0028CBA0_0x28cba0', 0x28ccfc, 19115, 0x694AAB),
    ('sub_0028D240_0x28d240', 0x28d584, 19112, 0x694AA8),
    ('sub_0028E6F0_0x28e6f0', 0x28e794, 19104, 0x694AA0),
    ('sub_0028E820_0x28e820', 0x28e918, 19104, 0x694AA0),
    ('sub_0028E970_0x28e970', 0x28e9cc, 19104, 0x694AA0),
    ('sub_0028EF30_0x28ef30', 0x28f05c, 19104, 0x694AA0),
    ('sub_0028F1C0_0x28f1c0', 0x28f1c8, 19104, 0x694AA0),
    ('sub_00290020_0x290020', 0x290068, 19136, 0x694AC0),
    ('sub_00290020_0x290020', 0x29011c, 19136, 0x694AC0),
    ('sub_00290220_0x290220', 0x290254, 19104, 0x694AA0),
    ('sub_00292B00_0x292b00', 0x292b50, 19104, 0x694AA0),
    ('sub_00292BC0_0x292bc0', 0x292c00, 19104, 0x694AA0),
    ('sub_00292CD0_0x292cd0', 0x293bac, 19117, 0x694AAD),
    ('sub_00292CD0_0x292cd0', 0x296064, 19117, 0x694AAD),
]

LOOPS = [
    ('sub_00283960_0x283960', 0x283a44), ('sub_00284E90_0x284e90', 0x284fb4), ('sub_00287DB0_0x287db0', 0x287f5c),
    ('sub_0028B8C0_0x28b8c0', 0x28b964), ('sub_0028B9C0_0x28b9c0', 0x28ba6c), ('sub_0028F450_0x28f450', 0x28f54c),
    ('sub_0028F450_0x28f450', 0x28f584), ('sub_0028FAD0_0x28fad0', 0x28fb90), ('sub_0028FAD0_0x28fad0', 0x28fbb4),
    ('sub_0028FDE0_0x28fde0', 0x28fe54), ('sub_0028FDE0_0x28fde0', 0x28fea8), ('sub_0028FDE0_0x28fde0', 0x28fee4),
    ('sub_00290020_0x290020', 0x29008c), ('sub_00290020_0x290020', 0x2900d0), ('sub_00290020_0x290020', 0x290154),
    ('sub_00290020_0x290020', 0x2901b4), ('sub_00290020_0x290020', 0x2901c8), ('sub_00290020_0x290020', 0x2901f4),
]
LISTS = [('sub_00283960_0x283960', 0x283994), ('sub_00283960_0x283960', 0x2839b0),
         ('sub_00287DB0_0x287db0', 0x287de4), ('sub_00287DB0_0x287db0', 0x287e00)]

EDITS = []
for name, address, immediate, target in TABLE:
    EDITS.append((name, address, re.compile(r'ADD32\(GPR_U32\(ctx, \d+\), %d\)' % (immediate & 0xFFFFFFFF)),
                  'openkai2::mods::capsule_address(0x%Xu)' % target))
for name, address in LOOPS:
    EDITS.append((name, address, '(int32_t)520)', '(int32_t)openkai2::mods::capsule_count())'))
for name, address in LISTS:
    EDITS.append((name, address, 'ADD32(GPR_U32(ctx, 0), 2080)', 'openkai2::mods::capsule_list_bytes(2080)'))
EDITS.append(('sub_00280510_0x280510', 0x2809e4, 'ADD32(GPR_U32(ctx, 0), 520)', 'openkai2::mods::capsule_count()'))
EDITS.append(('sub_00280510_0x280510', 0x280a18, 'ADD32(GPR_U32(ctx, 29), 128)',
              'ADD32(openkai2::mods::capsule_seen(GPR_U32(ctx, 29)), 128)'))
EDITS.append(('sub_00280510_0x280510', 0x280a38, 'ADD32(GPR_U32(ctx, 16), GPR_U32(ctx, 29))',
              'ADD32(GPR_U32(ctx, 16), openkai2::mods::capsule_seen(GPR_U32(ctx, 29)))'))

BACKUP = [
    ('sub_00236CE0_0x236ce0', 0x2373a4), ('sub_002377D0_0x2377d0', 0x237c3c), ('sub_00238160_0x238160', 0x23818c),
    ('sub_002386B0_0x2386b0', 0x2386e4), ('sub_0027ACF0_0x27acf0', 0x27b298), ('sub_0027ACF0_0x27acf0', 0x27b2ac),
    ('sub_0027ACF0_0x27acf0', 0x27b2ec), ('sub_0027ACF0_0x27acf0', 0x27b320), ('sub_00292CD0_0x292cd0', 0x293af8),
    ('sub_00292CD0_0x292cd0', 0x293b14), ('sub_00292CD0_0x292cd0', 0x293b3c), ('sub_00292CD0_0x292cd0', 0x293ba4),
    ('sub_00292CD0_0x292cd0', 0x295fc0), ('sub_00292CD0_0x292cd0', 0x295fd4), ('sub_00292CD0_0x292cd0', 0x295ffc),
    ('sub_00292CD0_0x292cd0', 0x29605c), ('sub_002A1880_0x2a1880', 0x2a18d0),
]
for name, address in BACKUP:
    EDITS.append((name, address, re.compile(r'ADD32\(GPR_U32\(ctx, \d+\), %d\)' % (-26544 & 0xFFFFFFFF)),
                  'openkai2::mods::capsule_backup_address(0x699850u)'))
for name, address in [('sub_00238160_0x238160', 0x238194), ('sub_002386B0_0x2386b0', 0x2386f0)]:
    EDITS.append((name, address, 'ADD32(GPR_U32(ctx, 0), 528)', 'openkai2::mods::capsule_inventory_bytes(528)'))


HOOKS = [('sub_002334E0_0x2334e0', 0x2334e0, 'openkai2::mods::capsule_extra_load(rdram, GPR_U32(ctx, 4));'),
         ('sub_00233810_0x233810', 0x233810, 'openkai2::mods::capsule_extra_save(rdram, GPR_U32(ctx, 5));')]
HOOK_ANCHOR = '    switch (ctx->pc) {'


def hook(text, address, call):
    line = '    if (ctx->pc == 0x%xu) %s\n' % (address, call)
    if line in text:
        return text
    if text.count(HOOK_ANCHOR) < 1:
        raise ValueError(f'Function 0x{address:x} has no entry dispatch')
    return text.replace(HOOK_ANCHOR, line + HOOK_ANCHOR, 1)


def blocks(text, address):
    marker = re.compile(r'^[ ]+// 0x%x:' % address, re.M)
    for match in marker.finditer(text):
        end = re.compile(r'^[ ]+// 0x[0-9a-f]+:', re.M).search(text, match.end())
        yield match.start(), end.start() if end else len(text)


def patch(text, edits):
    """Applies the edits for one file. Each edit already made is checked, not
    changed, so files patched by an earlier version gain only the new edits."""
    for _, address, original, replacement in edits:
        found = list(blocks(text, address))
        if not found:
            raise ValueError(f'Instruction 0x{address:x} is missing')
        for start, end in reversed(found):
            block = text[start:end]
            if replacement in block:
                continue
            count = len(original.findall(block)) if hasattr(original, 'findall') else block.count(original)
            if count != 1:
                raise ValueError(f'Instruction 0x{address:x} does not hold {original!r} once')
            block = original.sub(replacement, block) if hasattr(original, 'sub') else block.replace(original, replacement)
            text = text[:start] + block + text[end:]
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
    for name, _, _ in HOOKS:
        files.setdefault(name, [])
    for name, edits in files.items():
        path = args.directory / f'{name}.cpp'
        original = path.read_text()
        corrected = patch(original, edits)
        for hooked, address, call in HOOKS:
            if hooked == name:
                corrected = hook(corrected, address, call)
        if corrected != original:
            path.write_text(corrected)
    print(f'Verified {len(EDITS)} capsule edits and {len(HOOKS)} save hooks in {len(files)} functions.')


if __name__ == '__main__':
    main()
