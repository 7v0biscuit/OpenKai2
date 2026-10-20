# Phase 219 — Standalone CP0 Observation Replay and Real-Trace Intake

This is a **source-only, fresh-install Windows test package**, not a bootable PC port.
It carries the complete bounded native probes of Phases 213–218 *inside the archive*; no earlier phases on your PC are needed. No retail executable or game assets are bundled.

## One-click run

Extract into a NEW directory. Double-click `DOUBLE_CLICK_PHASE219.cmd`. Choose your OWNED Dragon Ball Z: Budokai 2 USA ISO/ELF when prompted. Upload `UPLOAD_PHASE219_RESULTS.zip` and `UPLOAD_PHASE219_DIAGNOSTICS.zip`. Requires Python 3.10+, CMake and Visual Studio C++ Build Tools.

Phase 219 adds a C++ **observation replay** binary with **432 independent modeled cases** spanning EI, MTC0 Status, and ERET, including explicitly supplied external EIE-clear/set events. Python computes predictions independently. All operations are on in-memory simulated state: there are NO PS2 MMIO accesses, host CP0 instructions, real IRQ injection, or emulator hooks. The earlier 384/448/560/112/63/26 tests remain part of the one-click run.

## Optional externally observed instruction trace

To assess the model against genuine guest observations, record a CP0 Status snapshot immediately **before and after** execution of an instruction of interest in your own PCSX2 debugger or PS2 debugging environment. Recording observations manually is supported; this package does **not** automatically hook PCSX2 or instrument physical PS2 hardware. If interrupts may have changed between snapshots, declare `external_event="unknown"`; those comparisons are **INCONCLUSIVE**, not falsely marked matching.

Place a UTF-8 JSONL file named `PHASE219_EXTERNAL_EE_OBSERVATIONS.jsonl` alongside `DOUBLE_CLICK_PHASE219.cmd`, or invoke `python RUN_PHASE219.py --trace C:\path\to\your.jsonl --game C:\path\to\game.iso`. One JSON object per line, no enclosing array. Supported fields:

- `source`: `pcsx2`, `real_ps2`, `other_emulator`, `unverified_manual` (a user-supplied label, **not** provenance verification)
- `capture_id`: nonempty trace session identifier
- `instruction`: `ei` (`pc=0x00113398`), `mtc0_status` (`pc=0x001133D0`), or `eret` (`pc=0x001133E4`)
- `status_before`, `status_after`, `pc`: REQUIRED `0x`-prefixed, up to 8-digit hexadecimal strings
- `external_event`: `none`, `clear_eie`, `set_eie`, or `unknown`; **never guess** `none` if asynchronous changes are possible
- `write_value`: REQUIRED for MTC0 Status comparison (what the guest wrote); `epc` and `error_epc` for ERET prediction
- `pc_after`: optional instruction successor to compare with the simplified model

Example `examples/SYNTHETIC_DO_NOT_SUBMIT_AS_HARDWARE_TRACE.jsonl` illustrates syntax only; **it was not captured on hardware/emulator**. For actual captures, PC and instruction identity must match the SHA-256 verified USA build. Field `status_after` must represent the same chosen sampling instant relative to any asynchronous event.

Report semantics: `MATCH` = supplied observation is consistent with limited modeled effects, `MISMATCH` = model disagrees, `INCONCLUSIVE_UNKNOWN_EXTERNAL_EVENT` = insufficient information. A matched observation does not certify trace authenticity, interrupt timing, correct privilege semantics in all contexts, or full-game integration. If you cannot get external observations, the one-click pipeline still runs all native + retail-word regressions and explicitly reports `NO_EXTERNAL_TRACE_SUPPLIED`.

**Known limitations:** Real guest interrupt timing, pipeline and exception side effects, CP0 hazards, precise MMIO bus behavior, full Timer2 handler, and game boot remain unverified. The Phase 216/218 CP0 and ERET algorithms are *hypotheses* for a bounded differential harness until checked against real observations.
