#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Configure reviewed guest callbacks and SDK routines for native translation."""
import argparse
import hashlib
from pathlib import Path
import re
import struct

ELF_SHA256 = '431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'
# The startup dispatcher at 0x0019DF48 walks seven active subsystem tables
# rooted at 0x00385748. Each table has 14 verified indirect method entries.
DISPATCH_TABLES = (
    (0x00387110, (
        0x001908E0, 0x001908E8, 0x001908F0, 0x00190918,
        0x00190920, 0x00190928, 0x00190930, 0x00190938,
        0x00190940, 0x00190948, 0x00190968, 0x00190990,
        0x001909B0, 0x001909D0,
    )),
    (0x00387148, (
        0x00190A88, 0x00190B00, 0x00190B20, 0x001921D8,
        0x00192328, 0x00192390, 0x00192398, 0x001923A0,
        0x001923A8, 0x001923B0, 0x001923D0, 0x001923F0,
        0x00192410, 0x00192430,
    )),
    (0x00387190, (
        0x00192C10, 0x00192CF8, 0x00192D18, 0x00196180,
        0x00196720, 0x001968A8, 0x001968B0, 0x001968B8,
        0x001968C0, 0x001968C8, 0x001968E8, 0x00196908,
        0x00196AB8, 0x00196BE0,
    )),
    (0x00387660, (
        0x0019F710, 0x0019F718, 0x0019F720, 0x0019F8D0,
        0x0019F8D8, 0x0019F8E0, 0x0019F8E8, 0x0019F8F0,
        0x0019F8F8, 0x0019F900, 0x0019F920, 0x0019F940,
        0x0019F9B8, 0x0019F9D8,
    )),
    (0x00386ED8, (
        0x0018B340, 0x0018B380, 0x0018B438, 0x0018C160,
        0x0018C698, 0x0018C798, 0x0018C7A0, 0x0018C7C0,
        0x0018C7E8, 0x0018CA00, 0x0018CA20, 0x0018CA40,
        0x0018CA60, 0x0018CA80,
    )),
    (0x00386F30, (
        0x0018CD90, 0x0018CD98, 0x0018CDA0, 0x0018CEF0,
        0x0018CF38, 0x0018CF40, 0x0018CF88, 0x0018CFD0,
        0x0018D018, 0x0018D070, 0x0018D090, 0x0018D0B0,
        0x0018D0D0, 0x0018D0F0,
    )),
    (0x00387628, (
        0x0019F408, 0x0019F410, 0x0019F418, 0x0019F578,
        0x0019F660, 0x0019F668, 0x0019F670, 0x0019F678,
        0x0019F680, 0x0019F688, 0x0019F6A8, 0x0019F6C8,
        0x0019F6E8, 0x0019F708,
    )),
)
# These callbacks are JR $ra with DADDU $v0, $zero, $zero in the delay slot.
ZERO_RETURN_ENTRIES = (
    0x001908E0, 0x001908E8, 0x00190918, 0x00190920,
    0x00190928, 0x00190930, 0x00190938, 0x00190940,
    0x001909D0, 0x00192390, 0x00192398, 0x001923A0,
    0x001923A8, 0x001968A8, 0x001968B0, 0x001968B8,
    0x001968C0, 0x0019F710, 0x0019F718, 0x0019F8D0,
    0x0019F8D8, 0x0019F8E0, 0x0019F8E8, 0x0019F8F0,
    0x0019F8F8, 0x0019F9D8, 0x0018C798, 0x0018CD90,
    0x0018CD98, 0x0018CF38, 0x0018D0F0, 0x0019F408,
    0x0019F410, 0x0019F660, 0x0019F668, 0x0019F670,
    0x0019F678, 0x0019F680, 0x0019F708,
)
# The file-driver interface starts at 0x0035FB38. Its last reviewed slot
# (offset 0x60) is queried by the compiled caller at 0x00164940.
FILE_CALLBACK_TABLES = ((0x0035FB38, (
    0x00167110, 0x00167188, 0x001672A0, 0x00000000,
    0x001673C8, 0x00167550, 0x001675A8, 0x00167640,
    0x00167678, 0x00000000, 0x00167820, 0x00167920,
    0x00167958, 0x00000000, 0x00167960, 0x00000000,
    0x00167218, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00167AF8,
)),)
FILE_CALLBACK_ENTRIES = tuple(address for _, row in FILE_CALLBACK_TABLES for address in row if address)
# SJRBF stream interface used by the indirect cleanup caller at 0x00156FE8.
# Preserve its three reserved null slots and all nine callable methods.
STREAM_CALLBACK_TABLES = ((0x00367B68, (
    0x00000000, 0x00000000, 0x00000000,
    0x0016D310, 0x0016D350, 0x0016D368,
    0x0016D400, 0x0016D6D0, 0x0016D570,
    0x0016D3B0, 0x0016D868, 0x0016D358,
)),)
STREAM_CALLBACK_ENTRIES = tuple(address for _, row in STREAM_CALLBACK_TABLES for address in row if address)
# This SDK routine submits SIF DMA and optionally waits for completion. Its
# numeric SIF syscalls are supported; the named runtime stub is unimplemented.
TRANSLATED_SDK_ENTRIES = (('sceSdTransToIOP', 0x00116268),)
# The dispatcher at 0x0012A680 selects asset relocation routines by a
# three-letter tag. Each eight-byte record is a function and a terminated tag.
ASSET_RELOCATION_BASE = 0x0033D680
ASSET_RELOCATION_HANDLERS = (
    (0x0012A860, b'AMG\0'), (0x0012AC20, b'AMT\0'),
    (0x0012AC30, b'AMM\0'), (0x0012AEC0, b'AMO\0'),
    (0x0012B120, b'AMC\0'), (0x0012B270, b'AML\0'),
    (0x0012B4D0, b'AMP\0'),
)
ASSET_RELOCATION_ENTRIES = tuple(address for address, _ in ASSET_RELOCATION_HANDLERS)
ASSET_RELOCATION_WORDS = tuple(value for address, tag in ASSET_RELOCATION_HANDLERS
                               for value in (address, int.from_bytes(tag, 'little'))) + (0, 0)
# Preserve ordering and share the argument-store callback between both groups.
ENTRY_POINTS = tuple(dict.fromkeys((0x00167188,)
    + tuple(address for _, row in DISPATCH_TABLES for address in row)
    + FILE_CALLBACK_ENTRIES + STREAM_CALLBACK_ENTRIES
    + tuple(address for _, address in TRANSLATED_SDK_ENTRIES)
    + ASSET_RELOCATION_ENTRIES))


def configure(text):
    section = re.search(r'(?ms)^\[general\]\s*\n(.*?)(?=^\[|\Z)', text)
    if not section:
        raise ValueError('Translation configuration has no [general] section')
    body = section.group(1)
    stubs = re.search(r'(?ms)^stubs\s*=\s*\[(.*?)\]', body)
    if stubs:
        values = stubs.group(1)
        for name, address in TRANSLATED_SDK_ENTRIES:
            values = re.sub(rf'"{re.escape(name)}(?:@0x0*{address:x})?"[ \t]*,?',
                            '', values, flags=re.IGNORECASE)
        body = body[:stubs.start(1)] + values + body[stubs.end(1):]
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
        body += f'\nentry_points = [{values}\n]\n\n'
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


def verify_elf_metadata(data):
    def words(address, count):
        offset = struct.unpack_from('<I', data, 28)[0]
        width, header_count = struct.unpack_from('<HH', data, 42)
        for index in range(header_count):
            kind, file_offset, virtual, _, length, *_ = struct.unpack_from('<8I', data, offset + index * width)
            if kind == 1 and virtual <= address and address + count * 4 <= virtual + length:
                return struct.unpack_from('<' + 'I' * count, data, file_offset + address - virtual)
        raise ValueError(f'Indirect entry metadata is outside the ELF at 0x{address:08x}')

    if words(0x00385748, len(DISPATCH_TABLES)) != tuple(base for base, _ in DISPATCH_TABLES):
        raise ValueError('Startup dispatcher table roots differ from the reviewed metadata')
    for base, entries in DISPATCH_TABLES + FILE_CALLBACK_TABLES + STREAM_CALLBACK_TABLES:
        if words(base, len(entries)) != entries:
            raise ValueError(f'Indirect methods differ from the reviewed table at 0x{base:08x}')
    for address in ZERO_RETURN_ENTRIES:
        if words(address, 2) != (0x03E00008, 0x0000102D):
            raise ValueError(f'Callback return instructions differ at 0x{address:08x}')
    if words(ASSET_RELOCATION_BASE, len(ASSET_RELOCATION_WORDS)) != ASSET_RELOCATION_WORDS:
        raise ValueError('Asset relocation handlers or tags differ from the reviewed table')


def emit_check_header(path):
    lines = ['// SPDX-License-Identifier: GPL-3.0-only', '#pragma once',
             '#include <array>', '#include <cstdint>', 'namespace openkai2::translation {']
    for name, entries in (('entries', ENTRY_POINTS), ('zero_return_entries', ZERO_RETURN_ENTRIES),
                          ('file_callback_entries', FILE_CALLBACK_ENTRIES),
                          ('file_callback_slots', FILE_CALLBACK_TABLES[0][1]),
                          ('stream_callback_entries', STREAM_CALLBACK_ENTRIES),
                          ('stream_callback_slots', STREAM_CALLBACK_TABLES[0][1]),
                          ('asset_relocation_entries', ASSET_RELOCATION_ENTRIES),
                          ('asset_relocation_words', ASSET_RELOCATION_WORDS)):
        lines.append(f'inline constexpr std::array<uint32_t, {len(entries)}> {name} = {{')
        lines.extend(f'    0x{address:08x}u,' for address in entries)
        lines.append('};')
    text = '\n'.join(lines + ['}', ''])
    path = Path(path)
    if not path.exists() or path.read_text() != text:
        path.write_text(text, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--verify-generated', type=Path)
    parser.add_argument('--emit-check-header', type=Path)
    args = parser.parse_args()
    if args.config:
        if not args.elf or hashlib.sha256(args.elf.read_bytes()).hexdigest() != ELF_SHA256:
            parser.error('Entry hints apply only to the verified Budokai 2 USA executable')
        verify_elf_metadata(args.elf.read_bytes())
        original = args.config.read_text()
        updated = configure(original)
        if updated != original:
            args.config.write_text(updated)
    if args.verify_generated:
        verify_generated(args.verify_generated)
        print('Verified reviewed guest entry coverage.')
    if args.emit_check_header:
        emit_check_header(args.emit_check_header)
    if not args.config and not args.verify_generated and not args.emit_check_header:
        parser.error('Provide --config with --elf, --verify-generated, or --emit-check-header')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
