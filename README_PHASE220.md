# Phase 220 — Standalone Virtual Timer2 IRQ11 Handoff

**Experimental test harness, NOT a bootable Windows game.** The added boundary
is NOT the retail game's `0x00113770` IRQ handler or an actual PS2Runtime IRQ
registration/scheduler. It is a small, explicitly virtual adapter to test how
a pending Timer2/IRQ11 event could be admitted to the already translated
wrapper-to-shared-helper chain. It does not prove the guest scheduler's
admission rules, acknowledgment policy, MMIO behavior, CP0 timing, or interrupt
state transitions in a running PS2 game.

## Run from a fresh folder (no previous phases)

On Windows: extract this ZIP into a new folder, double-click
`DOUBLE_CLICK_PHASE220.cmd`, and optionally choose **your own** Budokai 2 USA
ISO. The ISO is checked against the SHA-256-pinned USA ELF before any retail
instruction comparison. Requires Python 3.10+, CMake, and Visual Studio C++
Build Tools. No saved phase ZIPs/results, registry, PS2Recomp or PS2Runtime
checkout required. Report files:

- `UPLOAD_PHASE220_RESULTS.zip`
- `UPLOAD_PHASE220_DIAGNOSTICS.zip`

On Linux: `python3 RUN_PHASE220.py --skip-retail` runs the synthetic suite,
including CMake builds. For an owned game: use
`python3 RUN_PHASE220.py --game /path/to/game.iso`.

## What is actually tested

The new executable `budokai2_phase220_irq11_handoff` compiles the **existing**
Phase 213 retail-derived wrapper and Phase 216 bounded shared-helper native
implementation into the same executable. Explicit virtual admission flags
are applied first. The admitted path then executes the native wrapper and
shared helper, and an independent Python retail-opcode interpreter checks all
27 previously exposed chain fields plus four admission columns.

Four input bits are **test-adapter policies**, NOT reverse-engineered PS2 INTC
semantics:

| Bit | Meaning in virtual adapter |
| --- | --- |
| 0 | Synthetic Timer2 IRQ11 event pending |
| 1 | Synthetic IRQ route unmasked |
| 2 | Synthetic callback registered/enabled |
| 3 | Synthetic event acknowledged after admitted dispatch |

All combinations are exercised across both retail-derived wrapper addresses
`0x00113400` and `0x00113410`. **Acknowledgment does not follow from calling
the helper**: bit 3 is separate and a suppressed dispatch leaves a queued
pending event intact. No original guest Timer2 IRQ handler is invoked. An
accepted dispatch never touches host MMIO, CP0 or actual interrupts. All
hardware writes and `ERET` transitions are in-memory, not guest-hardware
observations.

The previous Phase 219 native observation replay and Phase 218–213 regressions
are bundled and rerun. The previous trace comparison is available using
`--trace path/to/your.jsonl` or a local file
`PHASE220_EXTERNAL_EE_OBSERVATIONS.jsonl`, following the format documented in
`README_PHASE219.md`. A caller-provided trace is not authenticated simply
because its `source` field says `pcsx2` or `real_ps2`; uncertain asynchronous
events must be labeled `unknown`.

**To make progress toward actual game runtime integration**, the original
Timer2 handler beginning at `0x00113770` must be translated completely and
registered in a build with the full original game dispatch map. Its real
clock/INTC/scheduler/IRQ11 signals then need comparison with controlled PCSX2
or real-PS2 observations. These are separate, unfinished requirements and
are never counted as passing in this release.
