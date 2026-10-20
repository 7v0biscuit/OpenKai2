# Phase 218 — Standalone EE interrupt-mode contract and external-event separation

**This is not a playable Budokai 2 PC port.** It is a C++ differential test of two bounded retail wrappers and a modeled shared PS2 Emotion Engine (EE) CP0/MMIO/ERET helper.

## New behavioral distinction

Phase 217 provided arbitrary CP0 Status snapshots *after* each `EI`. That method is useful for instruction coverage, but does **not** establish the effect of `EI`, privilege restrictions, or the timing of an actual interrupt. Phase 218 adds a separate software policy:

- CP0 `Status.EIE` is bit 16 (`0x00010000`), `EDI` is bit 17 (`0x00020000`), `EXL` bit 1 and `ERL` bit 2. Effective privilege depends on `KSU` and exception state.
- The `EI` policy raises `EIE` if kernel, or if `EDI` allows the instruction in user/supervisor mode. Otherwise it leaves Status unchanged.
- **Independent asynchronous observation:** an *injected test checkpoint* may clear EIE after the first or second `EI` observation. Neither `EI` itself nor this package is claimed to spontaneously clear EIE on real hardware. This is not a cycle-accurate guest interrupt scheduler.
- The already-implemented bounded helper then runs with these determined Status values; a separate Python MIPS-word interpreter predicts the expected register, branch, virtual store, CP0, and ERET results.
- The new native target is `budokai2_phase218_interrupt_transition_chain`; a 384-case matrix covers two wrappers, 16 starting statuses, four injected-event plans and three 64-bit register variants.
- The 448 previous chain, 560 MMIO/ERET, 112 CP0, 63 shared-entry and 26 wrapper cases are retained.

**Reference:** EE EIE/EDI and mode concepts in [PS2Tek EE documentation](https://israpps.github.io/ps2tek/) and R5900 Status definitions (PCSX2). This is a **documentation-derived contract**, not an observed timing trace. The bounded simulated `ERET` and allowed guest MMIO addresses are still subject to real-hardware verification.

## Fresh Windows installation

1. Extract this ZIP into a NEW, empty directory. You do not need any earlier Phase 1–217 folders, ZIP files, results, staged registry, PS2Recomp or PS2Runtime checkout.
2. Install Python 3.10+, CMake 3.16+, and Visual Studio C++ Build Tools with C++17.
3. Double-click `DOUBLE_CLICK_PHASE218.cmd` and optionally choose your OWNED USA Budokai 2 ISO/ELF. The exact SHA-256 and retail instruction digests must match for retail comparisons.
4. Upload `UPLOAD_PHASE218_RESULTS.zip` and `UPLOAD_PHASE218_DIAGNOSTICS.zip`.

For local CI: `python RUN_PHASE218.py --skip-retail`, then `python -m unittest discover -s tests -p 'test_*.py' -q`.

## Important limits

- The *retail* opcode verification proves that the helper instruction words match the owned USA executable; it does NOT prove that simulated status changes happened during real gameplay.
- Injected event modes are synthetic tests, not observed interrupts or proven interrupt timing.
- There are **zero original retail bindings**, no full function registry, no asset/gameplay extraction and no bootable PC executable.
- No host CP0 or MMIO is accessed; all guest registers and writes live in test-local state.
- Next milestone: obtain an attributable PCSX2/real-PS2 **CP0 before/after trace** around guest `0x00113398` to replace hypothetical interrupt-state checkpoints with observed behavior. The owned ISO by itself does not contain runtime state transitions.
