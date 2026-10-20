# Phase 217: standalone wrapper-to-shared integration

**Status:** native regression harness, not a playable Budokai 2 PC build.

## What changed

- Compiles and executes the exact C++ implementations inherited from the verified Phase 213 wrappers and Phase 216 virtual shared helper **in the same host process**. The Phase 217 integration source composes their real functions, rather than hardcoding expected wrapper results.
- Reconstructs the guest handoff from retail wrappers `0x00113400` and `0x00113410` into shared helper `0x00113380`. Each preserves the 64-bit caller `ra` and forwards the original argument into `a1`; each places a different MMIO guest address into `a0` (`...1010` versus `...1020`).
- Exercises **448 synthetic combinations** across the two wrapper entries, diverse 64-bit arguments, CP0 snapshots, two supplied post-`EI` transitions, and return/EPC values. The independent Python oracle decodes *both* 4-word wrapper instruction sequences and the 28-word modeled shared routine (including branch delay slots, virtual CP0, SW and ERET).
- Compares the compiled C++ state with the opcode decoder: register handoff, helper destination, the exact virtual store address and low 32-bit value, `ErrorEPC` and `ERET` result, and bounded poll exhaustion.
- Retains four earlier standalone C++ test executables and all prior regression cases. No old phase directories or archives are required.
- If the owned USA disc or retail executable is present, verifies the **complete ELF SHA-256**, wrapper-window digests, and entire shared 64-word window digest, then reruns every comparison using checked original instruction bytes. No retail bytes or executables ship in the source ZIP.

## Local checks

- `python -m unittest discover -s tests -p 'test_*.py' -q`
- `python RUN_PHASE217.py --skip-retail`
- `python RUN_PHASE217.py --game "C:\\path\\to\\your\\owned.iso"` (Windows)
- `python COLLECT_PHASE217_DIAGNOSTICS.py` (if needed)

## Safety and scope

**Virtual operations only**: no host privileged instructions, no host MMIO writes and no actual game function registration. In particular, the post-`EI` CP0 status values are supplied fixtures and do **not** emulate the physical console's timing. The return model is a bounded interpreter of `ERET`, not evidence that the scheduler and game startup run correctly. The 64-word window is not a proven whole-function boundary. Invalid/misaligned guest stores remain rejected in the earlier isolated test.

**Phase 218 direction:** verify realistic interrupt transition semantics against a PS2/PCSX2 trace or a documented state-transition contract before attempting to bind the shared helper into the genuine recompiled runtime. Preserve the standalone fresh-install workflow.
