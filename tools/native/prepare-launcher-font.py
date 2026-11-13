#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regenerate the launcher font from the unmodified OFL-licensed source."""
from pathlib import Path
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[2]
font = TTFont(root / 'third_party/bangers/Bangers-Regular.ttf', recalcTimestamp=False)
metrics = font['OS/2']
metrics.usWinAscent = metrics.sTypoAscender
metrics.usWinDescent = abs(metrics.sTypoDescender)
metrics.fsSelection |= 0x80
names = {1: 'OpenKai2 Display', 2: 'Regular', 3: 'OpenKai2 Display Regular 1.000',
         4: 'OpenKai2 Display Regular', 6: 'OpenKai2Display-Regular',
         16: 'OpenKai2 Display', 17: 'Regular'}
for record in font['name'].names:
    if record.nameID in names:
        record.string = names[record.nameID].encode(record.getEncoding())
font.save(root / 'third_party/openkai2-display/OpenKai2Display-Regular.ttf')
