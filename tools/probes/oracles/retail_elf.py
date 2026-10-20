#!/usr/bin/env python3
"""Read-only retail MIPS dependency audit for RegistryValidation. No game code is generated."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import traceback
import zipfile

EXPECTED_SHA256 = '431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898'
ENTRY = 0x001132A8
CHECKPOINT = 0x001132D4
MAX_WORDS = 192
MAX_ELF_BYTES = 256 * 1024 * 1024
SOURCES = ('B2_STAGE3_HW_GATE_0x1132a8.cpp', 'register_functions.cpp',
           'registry_seed_harness.cpp', 'registry_seed_hooks.cpp',
           'diagnostic_B2_STAGE3_HW_GATE_0x1132a8.cpp')


class AuditError(Exception):
    pass


def hx(n):
    return '0x%08X' % n


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            part = f.read(1024 * 1024)
            if not part:
                break
            h.update(part)
    return h.hexdigest()


class MipsELF:
    """Checks 32-bit little-endian MIPS ELF and uses only file-backed PT_LOAD bytes."""
    def __init__(self, blob):
        self.blob = blob
        if len(blob) < 52 or blob[:4] != b'\x7fELF':
            raise AuditError('Not an ELF file')
        if blob[4] != 1 or blob[5] != 1:
            raise AuditError('Expected ELFCLASS32 and little-endian ELF')
        (etype, machine, version, entry, phoff, shoff, flags, ehsize, phentsize,
         phnum, shentsize, shnum, shstrndx) = struct.unpack_from('<HHIIIIIHHHHHH', blob, 16)
        if machine != 8:  # EM_MIPS
            raise AuditError('Not a MIPS executable (e_machine=%d)' % machine)
        if phentsize < 32 or phnum < 1 or phnum > 256 or phoff + phentsize * phnum > len(blob):
            raise AuditError('Malformed ELF program header table')
        self.entry = entry
        self.segments = []
        for i in range(phnum):
            off = phoff + i * phentsize
            ptype, poff, vaddr, paddr, filesz, memsz, pflags, align = struct.unpack_from('<IIIIIIII', blob, off)
            if ptype != 1:
                continue
            if poff + filesz > len(blob) or filesz > memsz:
                raise AuditError('Malformed PT_LOAD segment %d' % i)
            self.segments.append(dict(index=i, offset=poff, vaddr=vaddr, paddr=paddr,
                                      filesz=filesz, memsz=memsz, flags=pflags))
        if not self.segments:
            raise AuditError('No PT_LOAD segment found')

    def file_offset(self, vaddr, length=4):
        for seg in self.segments:
            if vaddr >= seg['vaddr'] and vaddr + length <= seg['vaddr'] + seg['filesz']:
                return seg['offset'] + vaddr - seg['vaddr']
        raise AuditError('%s not backed by ELF PT_LOAD bytes' % hx(vaddr))

    def word(self, vaddr):
        if vaddr % 4:
            raise AuditError('Unaligned MIPS address %s' % hx(vaddr))
        return struct.unpack_from('<I', self.blob, self.file_offset(vaddr))[0]


def sign16(v):
    return v - 0x10000 if (v & 0x8000) else v


def decode_mips(pc, word):
    op = (word >> 26) & 63
    rs = (word >> 21) & 31
    rt = (word >> 16) & 31
    rd = (word >> 11) & 31
    imm = word & 65535
    funct = word & 63
    record = dict(pc=hx(pc), word=hx(word), mnemonic='', target='', kind='', has_delay_slot=False)
    def set_rec(mnemonic, kind='', target=None, delay=False):
        record.update(mnemonic=mnemonic, kind=kind, target=hx(target) if target is not None else '', has_delay_slot=delay)
    if op in (2, 3):
        dest = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        set_rec('jal' if op == 3 else 'j', 'direct_call' if op == 3 else 'direct_jump', dest, True)
    elif op in (4, 5, 6, 7, 20, 21, 22, 23):
        n = {4:'beq',5:'bne',6:'blez',7:'bgtz',20:'beql',21:'bnel',22:'blezl',23:'bgtzl'}[op]
        set_rec('%s $%d,$%d,%+d' % (n,rs,rt,sign16(imm)), 'conditional_branch', pc + 4 + (sign16(imm) << 2), True)
    elif op == 1:
        n = {0:'bltz',1:'bgez',2:'bltzl',3:'bgezl',16:'bltzal',17:'bgezal',18:'bltzall',19:'bgezall'}.get(rt)
        if n:
            set_rec('%s $%d,%+d' % (n,rs,sign16(imm)), 'branch_and_link' if rt in (16,17,18,19) else 'conditional_branch', pc + 4 + (sign16(imm) << 2), True)
    elif op == 0 and funct in (8,9):
        set_rec(('jr' if funct == 8 else 'jalr') + ' $%d' % rs, 'indirect_jump' if funct == 8 else 'indirect_call', None, True)
    elif op == 0 and funct == 0 and word == 0:
        set_rec('nop')
    elif op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F):
        m = {8:'addi',9:'addiu',10:'slti',11:'sltiu',12:'andi',13:'ori',14:'xori',15:'lui'}[op]
        set_rec('%s $%d,$%d,0x%04X' % (m, rt, rs, imm))
    elif op in (0x20,0x21,0x23,0x24,0x25,0x28,0x29,0x2B,0x37,0x3F):
        m = {0x20:'lb',0x21:'lh',0x23:'lw',0x24:'lbu',0x25:'lhu',0x28:'sb',0x29:'sh',0x2B:'sw',0x37:'ld',0x3F:'sd'}[op]
        set_rec('%s $%d,%d($%d)' % (m,rt,sign16(imm),rs))
    if not record['mnemonic']:
        set_rec('.word %s' % hx(word))
    record['at_checkpoint'] = (pc == CHECKPOINT)
    return record


def function_audit(elf):
    words = []
    for i in range(MAX_WORDS):
        pc = ENTRY + i*4
        try:
            words.append(decode_mips(pc, elf.word(pc)))
        except AuditError:
            break
    if not words or not any(r['at_checkpoint'] for r in words):
        raise AuditError('Both stage-3 function and checkpoint must be mapped to file-backed executable bytes')
    # Linear decode is not a control-flow graph. These are potential references only.
    calls = [r for r in words if r['kind'] in ('direct_call','branch_and_link','indirect_call')]
    control = [r for r in words if r['kind']]
    return words, calls, control


def is_game_image_name(name):
    """Only plausible standalone images; do not select our own RegistryValidation ZIPs."""
    lower = name.lower()
    return 'budokai' in lower and '2' in lower and lower.endswith(('.iso', '.bin'))


def _zip_game_members(path):
    """Inspect ZIP directory without extracting any contents or copying game files."""
    try:
        with zipfile.ZipFile(path) as z:
            members = [i for i in z.infolist() if not i.is_dir()]
            elf = [i for i in members if Path(i.filename.replace('\\', '/')).name.upper().split(';')[0] == 'SLUS_207.79']
            discs = [i for i in members if is_game_image_name(Path(i.filename.replace('\\', '/')).name)]
            return elf, discs
    except (zipfile.BadZipFile, OSError):
        return [], []


def _is_candidate(path):
    name = path.name
    if name.upper() == 'SLUS_207.79':
        return True
    if is_game_image_name(name):
        return True
    if name.lower().endswith('.zip'):
        elf, discs = _zip_game_members(path)
        return len(elf) == 1 or (len(elf) == 0 and len(discs) == 1)
    return False


def find_input_in_root(root):
    """Choose only a retail ELF or a verifiably relevant archive/image.

    Never select output ZIPs just because their filename includes BUDOKAI2.
    Does not read entire game files to decide candidate eligibility.
    """
    root = Path(root)
    if not root.is_dir():
        return None
    base_depth = len(root.parts)
    visited = 0
    discovered = []
    for parent, dirs, files in os.walk(str(root), followlinks=False):
        visited += 1
        depth = len(Path(parent).parts) - base_depth
        if visited > 12000 or depth >= 3:
            dirs[:] = []
        else:
            dirs[:] = [d for d in dirs if d.lower() not in (
                '.git', 'node_modules', '_deps', '.vs', 'build', 'reports', 'windowsapps')]
        for f in files:
            entry = Path(parent) / f
            if f.upper() == 'SLUS_207.79':
                discovered.append((0, str(entry).lower(), entry))
            elif is_game_image_name(f):
                discovered.append((1, str(entry).lower(), entry))
            elif f.lower().endswith('.zip') and _is_candidate(entry):
                discovered.append((2, str(entry).lower(), entry))
    discovered.sort(key=lambda x: (x[0], x[1]))
    return discovered[0][2] if discovered else None


def search_elf(path_arg, roots):
    # An explicit path always takes precedence. Do not silently switch to a
    # different game file if the user specified an unusable path.
    if path_arg:
        p = Path(path_arg.strip().strip('"').strip("'")).expanduser()
        return find_input_in_root(p) if p.is_dir() else (p if p.is_file() else None)
    if os.environ.get('BUDOKAI2_ELF'):
        p = Path(os.environ['BUDOKAI2_ELF'].strip().strip('"').strip("'")).expanduser()
        found = find_input_in_root(p) if p.is_dir() else (p if p.is_file() else None)
        if found is not None:
            return found
    extras = [Path.home() / n for n in ('Downloads', 'Desktop', 'Documents')]
    for folder in list(roots) + extras:
        found = find_input_in_root(folder) if folder else None
        if found is not None:
            return found
    return None


def _iso_sector(f, sector, sector_bytes, payload_offset):
    if sector < 0:
        raise AuditError('Invalid ISO sector')
    f.seek(sector * sector_bytes + payload_offset)
    data = f.read(2048)
    if len(data) != 2048:
        raise AuditError('Truncated ISO data sector')
    return data


def _iso_stream(f, sector, count, sector_bytes, payload_offset):
    if count < 0 or count > MAX_ELF_BYTES:
        raise AuditError('Unreasonable retail ELF size in ISO')
    out = bytearray()
    for i in range((count + 2047) // 2048):
        out.extend(_iso_sector(f, sector + i, sector_bytes, payload_offset))
    return bytes(out[:count])


def _iso_dirrec(data):
    if len(data) < 34 or data[0] > len(data):
        raise AuditError('Malformed ISO9660 directory record')
    extent = int.from_bytes(data[2:6], 'little')
    length = int.from_bytes(data[10:14], 'little')
    namelen = data[32]
    if namelen > len(data) - 33:
        raise AuditError('Malformed ISO9660 filename')
    name = data[33:33 + namelen].decode('ascii', 'replace')
    return extent, length, bool(data[25] & 2), name


def read_iso9660_elf_stream(f):
    """Read SLUS_207.79 from a seekable ISO/BIN stream, without copying image."""
    layout = None
    for sector_bytes, payload_offset in ((2048, 0), (2352, 16)):
        try:
            pvd = _iso_sector(f, 16, sector_bytes, payload_offset)
        except (AuditError, ValueError, OSError, EOFError):
            continue
        if pvd[:7] == b'\x01CD001\x01':
            layout = sector_bytes, payload_offset
            break
    if layout is None:
        raise AuditError('Not a supported ISO9660 image (2048-byte ISO or Mode1/2352 BIN)')
    sector_bytes, payload_offset = layout
    root_len = pvd[156]
    if root_len < 34:
        raise AuditError('ISO9660 root directory record is missing')
    root = _iso_dirrec(pvd[156:156 + root_len])
    queue = [(root[0], root[1], 0)]
    visited = set()
    while queue:
        extent, length, depth = queue.pop(0)
        if (extent, length) in visited:
            continue
        visited.add((extent, length))
        if length <= 0 or length > 8 * 1024 * 1024:
            raise AuditError('Invalid ISO9660 directory length')
        directory = _iso_stream(f, extent, length, sector_bytes, payload_offset)
        pos = 0
        while pos < len(directory):
            rec_len = directory[pos]
            if rec_len == 0:
                pos = ((pos // 2048) + 1) * 2048
                continue
            if rec_len < 34 or pos + rec_len > len(directory):
                raise AuditError('Malformed ISO9660 record boundary')
            item_sector, item_len, is_dir, name = _iso_dirrec(directory[pos:pos + rec_len])
            pos += rec_len
            if name in ('\x00', '\x01'):
                continue
            plain = name.split(';', 1)[0].upper()
            if not is_dir and plain == 'SLUS_207.79':
                if not (52 <= item_len <= MAX_ELF_BYTES):
                    raise AuditError('Unreasonable retail ELF size in disc image')
                return _iso_stream(f, item_sector, item_len, sector_bytes, payload_offset)
            if is_dir and depth < 3 and len(visited) + len(queue) < 100:
                queue.append((item_sector, item_len, depth + 1))
    raise AuditError('ISO9660 image does not contain SLUS_207.79; select Budokai 2 USA')


def read_iso9660_elf(path):
    with path.open('rb') as f:
        return read_iso9660_elf_stream(f)


def load_retail_elf(path):
    """Returns (retail ELF bytes, input-kind); ships no game content."""
    suffix = path.suffix.lower()
    if suffix in ('.iso', '.bin'):
        return read_iso9660_elf(path), 'iso9660_disc_image'
    if suffix == '.zip':
        with zipfile.ZipFile(path) as z:
            direct, images = _zip_game_members(path)
            if len(direct) == 1 and 52 <= direct[0].file_size <= MAX_ELF_BYTES:
                return z.read(direct[0]), 'zip_contained_elf'
            if len(direct) > 1:
                raise AuditError('Ambiguous ZIP: more than one SLUS_207.79')
            if len(images) == 1 and images[0].file_size <= 9 * 1024**3:
                # Python ZipExtFile is seekable: no multi-GB temporary image.
                with z.open(images[0], 'r') as raw_iso:
                    return read_iso9660_elf_stream(raw_iso), 'zip_contained_iso9660_disc_image'
            raise AuditError('ZIP must contain one SLUS_207.79 or one Budokai 2 ISO/BIN; this may be an audit/output ZIP')
    if suffix in ('.rar', '.7z'):
        raise AuditError('RAR/7Z input must be extracted first. Use 7-Zip to extract the ISO or SLUS_207.79, then drag it onto the launcher.')
    if path.stat().st_size > MAX_ELF_BYTES:
        raise AuditError('Input too large for an extracted ELF. Pass the game .iso or extracted SLUS_207.79.')
    return path.read_bytes(), 'extracted_elf_file'


def search_project(project_arg):
    if project_arg:
        x = Path(project_arg).expanduser()
        return x if x.is_dir() else None
    if os.environ.get('OPENKAI2_SOURCE_DIR'):
        x = Path(os.environ['OPENKAI2_SOURCE_DIR'])
        if x.is_dir():
            return x
    candidates = [Path.cwd(), Path.cwd().parent]
    for p in candidates:
        if p.is_dir():
            if (p / 'CMakeLists.txt').is_file():
                return p
            for src in ('src/probes/B2_STAGE3_HW_GATE_0x1132a8.cpp','B2_STAGE3_HW_GATE_0x1132a8.cpp'):
                if (p / src).is_file():
                    return p
    return None


def discover_sources(root):
    if not root:
        return {}
    files = {}
    names = set(SOURCES)
    for parent, dirs, filenames in os.walk(str(root), followlinks=False):
        if len(Path(parent).parts) - len(root.parts) > 7:
            dirs[:] = []
            continue
        dirs[:] = [d for d in dirs if d.lower() not in ('reports','_deps','.git','.vs','build','release','debug')]
        for n in filenames:
            if n in names and n not in files:
                files[n] = Path(parent) / n
        if len(files) == len(SOURCES):
            break
    return files


def source_evidence(files, directory):
    results = []
    for key, path in sorted(files.items()):
        before = sha256_file(path)
        lines = path.read_text(encoding='utf-8-sig',errors='replace').splitlines()
        patterns = [re.compile(s,re.I) for s in [r'1132d4',r'1132a8',r'10001810',r'0x100',r'register_function',r'registerFunctions']]
        hits = []
        for i,line in enumerate(lines):
            if any(p.search(line) for p in patterns):
                hits.append(dict(line=i+1, text=line.strip()[:300]))
        results.append(dict(name=key,path=str(path),sha256=before,length_lines=len(lines),hits=hits[:70]))
        # Scrape no entire generated retail source: small relevant snippets only.
        after = sha256_file(path)
        if after != before:
            raise AuditError('Source changed during read: ' + str(path))
    (directory / 'REGISTRY_VALIDATION_SOURCE_AUDIT.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    with (directory / 'REGISTRY_VALIDATION_SOURCE_AUDIT.txt').open('w',encoding='utf-8') as f:
        for r in results:
            f.write('%s SHA256=%s PATH=%s\n' % (r['name'],r['sha256'],r['path']))
            for h in r['hits']:
                f.write('  L%d: %s\n' % (h['line'],h['text']))
    return results


# All entries below come from comments in the *local generated source*.
# They are not trusted as retail ELF bytes or evidence of executed control flow.
GENERATED_INSTRUCTION_RE = re.compile(r'^\s*//\s*0x([0-9a-fA-F]{6,8}):\s*0x([0-9a-fA-F]{8})\s+(.*)$')


def extract_generated_instruction_rows(path):
    rows = []
    addresses = set()
    for line_num, line in enumerate(path.read_text(encoding='utf-8-sig', errors='replace').splitlines(), 1):
        m = GENERATED_INSTRUCTION_RE.match(line)
        if not m:
            continue
        address, word, comment = int(m[1], 16), int(m[2], 16), m[3].strip()
        if address % 4 or address in addresses:
            raise AuditError('Duplicate/unaligned instruction comment at line %d in %s' % (line_num, path.name))
        addresses.add(address)
        row = decode_mips(address, word)
        row.update(source_file=path.name, source_line=line_num, original_comment=comment[:160])
        rows.append(row)
    return rows


def generated_source_dependency_audit(files, out):
    """Independent progress when the exact retail ELF is not yet available.

    Compare source comments from the original generated function and diagnostic
    copy, then emit a bounded candidate list with explicit evidence provenance.
    """
    main_file = files.get('B2_STAGE3_HW_GATE_0x1132a8.cpp')
    if main_file is None:
        return {'status':'NO_GENERATED_SOURCE', 'instruction_comments':0, 'post_checkpoint_candidates':0}
    rows = extract_generated_instruction_rows(main_file)
    if not rows:
        return {'status':'NO_INSTRUCTION_COMMENTS', 'instruction_comments':0, 'post_checkpoint_candidates':0}
    diag_file = files.get('diagnostic_B2_STAGE3_HW_GATE_0x1132a8.cpp')
    diag_match = None
    diag_mismatch = []
    if diag_file is not None:
        diag = extract_generated_instruction_rows(diag_file)
        ref = {int(x['pc'],16):x['word'] for x in rows}
        seen = {int(x['pc'],16):x['word'] for x in diag}
        diag_mismatch = [hx(pc) for pc in sorted(set(ref)|set(seen)) if ref.get(pc)!=seen.get(pc)]
        diag_match = len(diag_mismatch)==0
    ordered=sorted(rows,key=lambda x:int(x['pc'],16))
    candidates=[r for r in ordered if int(r['pc'],16)>=CHECKPOINT and r['kind'] in
                ('direct_call','indirect_call','branch_and_link','direct_jump','indirect_jump','conditional_branch')]
    fields=['source_file','source_line','pc','word','original_comment','mnemonic','kind','target','has_delay_slot','at_checkpoint']
    with (out/'REGISTRY_VALIDATIONC_GENERATED_SOURCE_DECODE.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader();w.writerows(ordered)
    with (out/'REGISTRY_VALIDATIONC_SOURCE_SLOWPATH_CANDIDATES.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader();w.writerows(candidates)
    summary = dict(status='GENERATED_COMMENTS_ONLY', source_sha256=sha256_file(main_file),
                   instruction_comments=len(ordered), min_pc=ordered[0]['pc'], max_pc=ordered[-1]['pc'],
                   checkpoint_comment_present=any(int(r['pc'],16)==CHECKPOINT for r in ordered),
                   diagnostic_opcode_comments_match=diag_match,
                   diagnostic_opcode_mismatches=diag_mismatch[:40],
                   post_checkpoint_candidates=len(candidates),
                   candidate_calls=sum(r['kind'] in ('direct_call','indirect_call','branch_and_link') for r in candidates),
                   warning='Generated C++ source instruction comments are not independently verified retail code or proof of execution; candidate calls include unreachable instructions.')
    (out/'REGISTRY_VALIDATIONC_GENERATED_SOURCE_STATUS.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print('REGISTRY_VALIDATIONC_GENERATED_SOURCE',summary['instruction_comments'],'commented words;',len(candidates),'post-checkpoint control-flow candidates; source/diagnostic match:',diag_match)
    return summary


def save_tables(directory, words, calls, control):
    fields = ['pc','word','mnemonic','kind','target','has_delay_slot','at_checkpoint']
    with (directory / 'REGISTRY_VALIDATION_DISASSEMBLY.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader(); w.writerows(words)
    with (directory / 'REGISTRY_VALIDATION_TARGET_CANDIDATES.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader(); w.writerows(calls)
    with (directory / 'REGISTRY_VALIDATION_SLOW_PATH_DISASSEMBLY.txt').open('w',encoding='utf-8') as f:
        f.write('LINEAR DISASSEMBLY ONLY. NOT A PROVEN CONTROL-FLOW PATH OR FUNCTION BOUNDARY.\n')
        f.write('Anchor entry %s; slow-path checkpoint %s\n\n'%(hx(ENTRY),hx(CHECKPOINT)))
        for r in words:
            f.write('%s  %s  %-31s %-20s %s%s\n' % (r['pc'],r['word'],r['mnemonic'],r['kind'],r['target'], '   <-- CHECKPOINT' if r['at_checkpoint'] else ''))
    # Prioritize *observed decoded* JAL sites after checkpoint, not invented names.
    near = [r for r in calls if int(r['pc'],16) >= CHECKPOINT]
    near.sort(key=lambda r: int(r['pc'],16))
    with (directory / 'REGISTRY_VALIDATION_TARGET_TRIAGE.md').open('w',encoding='utf-8') as f:
        f.write('# RegistryValidation — candidate direct calls and handoff triage\n\n')
        f.write('This table is a linear-scan **candidate list**, not proven reachability, function boundaries, or execution.\n\n')
        f.write('Checkpoint: `%s` (downstream execution was NOT tested in RegistrySeedC).\n\n' % hx(CHECKPOINT))
        f.write('| Candidate site | Instruction | Target | Review order |\n|---|---|---|---|\n')
        for r in near[:40]:
            delta = (int(r['pc'],16) - CHECKPOINT)//4
            f.write('| `%s` | `%s` | `%s` | +%d decoded instructions |\n' %
                    (r['pc'],r['mnemonic'],r['target'] or 'INDIRECT — register target unresolved',delta))
        if not near:
            f.write('| *No call candidates located in this bounded linear scan* | | | |\n')
        f.write('\nNext: inspect the checkpoint and each prioritized call site in Ghidra, ')
        f.write('prove reachability/branch delay behavior, and export validated function ranges.\n')
    return len(calls)


def run(args):
    out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
    status=dict(component='retail_validation',status='INCOMPLETE',stage='starting',retail_elf_verified=False,
                game_booted=False,game_playable=False,real_runtime_executed_by_registry_validation=False,
                host_timer3_semantics_verified=False,
                retail_sha256_expected=EXPECTED_SHA256,checkpoint=hx(CHECKPOINT),
                original_sources_modified=False)
    rc=2
    try:
        project=search_project(args.project)
        files=discover_sources(project)
        src=source_evidence(files,out)
        try:
            source_summary = generated_source_dependency_audit(files, out)
        except (AuditError, OSError, UnicodeError) as source_err:
            source_summary = {'status':'SOURCE_AUDIT_ERROR', 'error':str(source_err)}
            print('REGISTRY_VALIDATIONC_SOURCE_AUDIT_ERROR', source_err, file=sys.stderr)
        status['generated_source_evidence'] = source_summary
        status['source_project_found']=bool(project)
        status['source_project_path']=str(project) if project else ''
        status['project_sources_found']=[x['name'] for x in src]
        print('REGISTRY_VALIDATION_PROJECT',project or 'NOT_FOUND (optional; pass second argument to .cmd)')
        print('REGISTRY_VALIDATION_SOURCES',', '.join(files) or 'none')
        roots=[p for p in (project,Path.cwd()) if p]
        elfpath=search_elf(args.elf,roots)
        status['input_was_explicit'] = bool(args.elf)
        status['selected_input_path'] = str(elfpath) if elfpath else ''
        if elfpath is None:
            status.update(status='NEEDS_RETAIL_ELF',stage='awaiting_exact_user_owned_elf',error='No plausible retail game executable/disc was discovered. Drag your original SLUS_207.79, ISO/BIN, or a ZIP containing one of those onto the CMD; or provide a full path at the prompt.')
            print('REGISTRY_VALIDATION_NEEDS_RETAIL_ELF: Pass user-owned SLUS_207.79 to launcher. No generated game code was assumed.')
        else:
            retail_bytes, input_kind = load_retail_elf(elfpath)
            digest=hashlib.sha256(retail_bytes).hexdigest()
            status['retail_input_path']=str(elfpath)
            status['retail_input_kind']=input_kind
            status['retail_sha256_observed']=digest
            print('REGISTRY_VALIDATION_ELF',elfpath)
            print('REGISTRY_VALIDATION_SHA256',digest)
            if digest != EXPECTED_SHA256:
                raise AuditError('Retail SHA256 mismatch. Expected exactly %s; no code decoded.' % EXPECTED_SHA256)
            elf=MipsELF(retail_bytes)
            words,calls,controls=function_audit(elf)
            save_tables(out,words,calls,controls)
            status.update(status='REGISTRY_VALIDATION_STATIC_AUDIT_PASS',stage='completed_static_evidence',retail_elf_verified=True,
                          words_decoded=len(words),direct_call_candidates=sum(r['kind']=='direct_call' for r in calls),
                          indirect_call_sites=sum(r['kind']=='indirect_call' for r in calls),
                          branch_and_link_candidates=sum(r['kind']=='branch_and_link' for r in calls),
                          control_flow_references=len(controls),
                          post_checkpoint_call_candidates=sum(int(r['pc'],16)>=CHECKPOINT for r in calls),elf_entry=hx(elf.entry),
                          note='Candidates are linear-scan evidence, not executed calls or verified function boundaries')
            print('REGISTRY_VALIDATION_RETAIL_ELF_VERIFIED',EXPECTED_SHA256)
            print('REGISTRY_VALIDATION_CHECKPOINT_WORD',hx(elf.word(CHECKPOINT)))
            print('REGISTRY_VALIDATION_STATIC_AUDIT_PASS words=%d direct_calls=%d indirect_calls=%d controls=%d' % (
                len(words),status['direct_call_candidates'],status['indirect_call_sites'],len(controls)))
            rc=0
    except Exception as e:
        status.update(status='REGISTRY_VALIDATION_ERROR',stage='failed',error=str(e))
        print('REGISTRY_VALIDATION_ERROR',e,file=sys.stderr)
    finally:
        (out / 'REGISTRY_VALIDATION_WINDOWS_STATUS.json').write_text(json.dumps(status,indent=2),encoding='utf-8')
        with (out / 'REGISTRY_VALIDATION_NEXT_ACTIONS.txt').open('w',encoding='utf-8') as f:
            f.write('REGISTRY_VALIDATIONC: READ-ONLY SOURCE/RETAIL EVIDENCE, NOT A GAME BOOT OR NATIVE PORT\n\n')
            f.write('Observed state: '+status['status']+'\n')
            if status.get('generated_source_evidence',{}).get('status')=='GENERATED_COMMENTS_ONLY':
                f.write('LOCAL GENERATED SOURCE COMMENTS ANALYZED. See REGISTRY_VALIDATIONC_SOURCE_SLOWPATH_CANDIDATES.csv.\n')
                f.write('Do not treat generated source comments as independently verified retail execution.\n')
            if status['status']=='NEEDS_RETAIL_ELF':
                f.write('NEXT: drag your extracted USA SLUS_207.79 / Budokai 2 ISO onto the CMD, or enter its full path at the prompt.\n')
            elif status['status']=='REGISTRY_VALIDATION_STATIC_AUDIT_PASS':
                f.write('NEXT: review REGISTRY_VALIDATION_DISASSEMBLY.csv / TARGET_CANDIDATES.csv and source audit;\n')
                f.write('classify exact downstream call targets in Ghidra; export validated boundaries.\n')
                f.write('Do NOT treat linear bytes as a proven function, mark unknown targets implemented,\n')
                f.write('or assume slow-path functions were run. Port requires runtime clock/IRQ,\n')
                f.write('IOP, GS, DMA, assets, full game-code coverage and gameplay testing.\n')
            else:
                f.write('FIX: '+status.get('error','unknown')+'\n')
    return rc


def main():
    p=argparse.ArgumentParser(description='RegistryValidation read-only Budokai 2 retail ELF slow-path audit')
    p.add_argument('--elf',default='',help='USA SLUS_207.79 executable, ISO/BIN disc image, or ZIP containing ELF or ISO')
    p.add_argument('--project',default='',help='Optional source project root')
    p.add_argument('--output',default='reports')
    args=p.parse_args()
    return run(args)


if __name__=='__main__':
    sys.exit(main())
