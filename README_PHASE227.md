# Phase 227 — bounded EE guest-address aliases and fault handling

## Requirements

- Windows 10/11, Python 3, CMake, and Visual Studio 2022 C++ Build Tools (x64).
- Optionally, your legally owned **exact** Dragon Ball Z: Budokai 2 USA ISO.
- No previous phase ZIP, phase registry, PS2Runtime or PS2Recomp checkout is required.

## Run

1. Extract this whole archive into a new folder.
2. Double-click `DOUBLE_CLICK_PHASE227.cmd`.
3. Select your legally owned USA ISO when the file picker appears.
4. After completion, upload `UPLOAD_PHASE227_RESULTS.zip` and `UPLOAD_PHASE227_DIAGNOSTICS.zip`.

If the ISO file picker is canceled, synthetic tests still run, and retail is reported
as **NOT CHECKED**, not as PASS. Command-line options:

`py -3 RUN_PHASE227.py --game C:\path\to\owned\game.iso`

`py -3 RUN_PHASE227.py --skip-retail`

The diagnostics-only collector is `ONE_CLICK_PHASE227_DIAGNOSTICS.cmd`.

## Engineering scope

Phase 227 extends **the same `Mem` guest address backend** used by the
translated original handler entry and Phase 226 linked-list traversal. It adds
32-bit direct-map translation for KSEG0 (`0x80000000..0x9fffffff`) and KSEG1
(`0xa0000000..0xbfffffff`). The old low physical test-fixture addresses remain
supported. The resulting physical byte address is still checked against a
**small allowlisted set of stack, global, linked-node and virtual MMIO regions**.
No actual host hardware address is accessed. Unsupported virtual segments,
misalignment, and out-of-region/overflow accesses fail closed. Writes validate
the entire access before committing any byte.

The new Phase 227 differential test executes the *original, retail-pinned*
handler setup and instruction window `0x001137F4–0x0011385C` through translated
C++ and independently opcode-interpreted MIPS operations, comparing register
state, branch paths, read audits, and the predicted helper target. Test cases
include physical/KSEG0/KSEG1/mixed pointers, writes to the node memory through
a different segment alias, suppressed IRQs, cyclic loops, and fault injections.
Separate memory-access cases check alias visibility and failed writes.

**Important limits:** The new alias test stops at the threshold decision; it
does **not** newly execute the original helper chain via alias pointers.
The previous full live-memory handler/helper regression remains included and
runs separately with the new shared backend. Guest interrupt scheduling and
`EI`, `MTC0`, `ERET`, and hardware MMIO remain virtual. No ELF recompiler,
full guest RAM map, EE TLB, DMA, graphics, audio, native game exe, or game boot
is included. Successful opcode comparisons are not PS2 timing validation.

An optional real PCSX2 or real-PS2 CP0 observation JSONL can be supplied with
`--trace path`; absent such data the report explicitly states
`NO_EXTERNAL_TRACE_SUPPLIED`.
