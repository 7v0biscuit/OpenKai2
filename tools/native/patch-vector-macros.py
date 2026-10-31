#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Give the translated VU0 macro conversions and divides their VU results."""
import argparse
import re
from pathlib import Path

VFTOI = ('__m128i res_i = _mm_cvttps_epi32(src);', '__m128i res_i = ps2_vftoi(src);')
VDIV = ('ctx->vu0_q = (ft != 0.0f) ? (fs / ft) : 0.0f;', 'ctx->vu0_q = ps2_vdiv(fs, ft);')
VSQRT = ('ctx->vu0_q = sqrtf(std::max(0.0f, ft));', 'ctx->vu0_q = ps2_vsqrt(ft);')
RSQRT = re.compile(
    r'(    // 0x[0-9a-f]+: 0x(?P<word>[0-9a-f]{8})  vrsqrt[^\n]*\n(?:[^\n]*\n)??)'
    r'    \{ float ft = (?P<ft>[^;]+); ctx->vu0_q = \(ft > 0\.0f\) \? \(1\.0f / sqrtf\(ft\)\) : 0\.0f; \}\n')


def numerator(word):
    fs, field = (word >> 11) & 31, (word >> 21) & 3
    return f'_mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{fs}], ctx->vu0_vf[{fs}], _MM_SHUFFLE(0,0,0,{field})))'


def patch_text(text):
    for original, replacement in (VFTOI, VDIV, VSQRT):
        text = text.replace(original, replacement)
    return RSQRT.sub(lambda m: f'{m.group(1)}    {{ float fs = {numerator(int(m.group("word"), 16))}; '
                               f'float ft = {m.group("ft")}; ctx->vu0_q = ps2_vrsqrt(fs, ft); }}\n', text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path)
    args = parser.parse_args()
    changed = 0
    for path in sorted(args.directory.glob('*.cpp')):
        original = path.read_text()
        if 'vu0_' not in original:
            continue
        patched = patch_text(original)
        if '1.0f / sqrtf(ft)' in patched or '_mm_cvttps_epi32(src)' in patched:
            raise ValueError(f'{path.name} has a VU0 conversion this script does not recognize')
        if patched != original:
            path.write_text(patched)
            changed += 1
    print(f'Verified VU0 macro conversions and divides; updated {changed} translated files.')


if __name__ == '__main__':
    main()
