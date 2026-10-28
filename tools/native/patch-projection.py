#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply the aspect correction to the translated USA camera routine."""
import argparse
from pathlib import Path

MARKER = '// OpenKai2: horizontal projection correction at original 0x001317f0.'
ORIGINAL = ('    // 0x1317f0: 0xc6150010  lwc1        $f21, 0x10($s0)\n'
            '    ctx->pc = 0x1317f0u;\n'
            '    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 16), 16)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[21] = f; }\n')
CORRECTION = ('    '+MARKER+'\n'
              '    if (openkai2::graphics_settings().aspect == 1u)\n'
              '        ctx->f[21] = FPU_MUL_S(ctx->f[21], 0.75f);\n')


def patch_projection(text):
    if text.count(ORIGINAL) != 1:
        raise ValueError('Camera load at 0x001317f0 is missing or ambiguous')
    if MARKER in text:
        if text.count(MARKER) != 1 or ORIGINAL + CORRECTION not in text:
            raise ValueError('Camera correction differs from the expected sequence')
        return text
    return '#include "graphics_settings.h"\n' + text.replace(ORIGINAL, ORIGINAL + CORRECTION)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args()
    path = args.directory / 'sub_001317A0_0x1317a0.cpp'
    original = path.read_text()
    corrected = patch_projection(original)
    if original != corrected:
        path.write_text(corrected)
    print('Verified optional 16:9 horizontal camera correction.')


if __name__ == '__main__':
    main()
