#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Expose verified indirect guest entries omitted by the stripped-ELF analyzer."""
import argparse
import hashlib
from pathlib import Path
import re

ELF_SHA256 = '431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'
# This leaf callback follows another function's return without a stack prologue.
# The game calls it indirectly during startup; it stores the two callback args.
ENTRY_POINTS = (0x00167188,)


def configure(text):
    section = re.search(r'(?ms)^\[general\]\s*\n(.*?)(?=^\[|\Z)', text)
    if not section:
        raise ValueError('Translation configuration has no [general] section')
    body = section.group(1)
    entries = re.search(r'(?ms)^entry_points\s*=\s*\[(.*?)\]', body)
    if entries:
        values = entries.group(1).rstrip()
        for address in ENTRY_POINTS:
            selector = f'0x{address:08x}'
            if not re.search(rf'(?i)(?:@)?0x0*{address:x}["\s]', values):
                if values and not values.endswith(','):
                    values += ','
                values += f'\n  "{selector}",'
        body = body[:entries.start(1)] + values + '\n' + body[entries.end(1):]
    else:
        values = ', '.join(f'"0x{address:08x}"' for address in ENTRY_POINTS)
        body += f'\nentry_points = [{values}]\n\n'
    return text[:section.start(1)] + body + text[section.end(1):]


def verify_generated(directory):
    directory = Path(directory)
    registry = (directory / 'register_functions.cpp').read_text()
    for address in ENTRY_POINTS:
        binding = re.search(
            rf'g_ps2RecompiledFunctionTable\[\d+\]\s*=\s*(\w+);\s*//\s*0x0*{address:x}\b',
            registry, re.IGNORECASE)
        if not binding:
            raise ValueError(f'Generated dispatch table is missing guest entry 0x{address:08x}; regenerate translation')
        owner = (directory / (binding.group(1) + '.cpp')).read_text()
        dedicated = re.search(rf'// Address:\s*0x0*{address:x}\s*-', owner, re.IGNORECASE)
        resumed = (re.search(rf'case\s+0x0*{address:x}u:\s*goto\s+label_{address:x};', owner, re.IGNORECASE)
                   and re.search(rf'^label_{address:x}:', owner, re.MULTILINE | re.IGNORECASE))
        if not (dedicated or resumed):
            raise ValueError(f'Guest entry 0x{address:08x} cannot resume at its own instruction')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--verify-generated', type=Path)
    args = parser.parse_args()
    if args.config:
        if not args.elf or hashlib.sha256(args.elf.read_bytes()).hexdigest() != ELF_SHA256:
            parser.error('Entry hints apply only to the verified Budokai 2 USA executable')
        original = args.config.read_text()
        updated = configure(original)
        if updated != original:
            args.config.write_text(updated)
    if args.verify_generated:
        verify_generated(args.verify_generated)
        print('Verified indirect guest entry coverage.')
    if not args.config and not args.verify_generated:
        parser.error('Provide --config with --elf, or --verify-generated')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
