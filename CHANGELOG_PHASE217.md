# Phase 217 release

- New `src/phase217_wrapper_shared_chain.cpp`: executes two existing retail-derived native tail-jump wrappers and existing virtual shared helper as one controlled flow.
- `B2_PHASE217_LIBRARY_ONLY` guards the legacy probe `main()` functions when imported into the composite executable; standalone legacy probes retain their original entry points.
- `phase217_semantics.py`: separate MIPS opcode-driven interpretation of both function segments, 448 deterministic cases, port/data assertions, and CP0/ERET cross-check.
- Updated CMake and `RUN_PHASE217.py`: five C++ probes, local and optional SHA-pinned retail comparisons, standalone diagnostics and manifest enforcement.
- Preserved the 560 Phase 216, 112 Phase 215, 63 Phase 214, and 26 wrapper baseline regression matrices.
- No old phase files or registry dependencies, no game assets, no executable binaries, no host hardware access, no bootable game.
