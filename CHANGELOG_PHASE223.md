# Phase 223 — bounded original IRQ11 handoff integration

- Verified Phase 222 Windows native build and 180 retail-opcode double-helper comparisons.
- Created `src/phase223_virtual_irq_original_handler.cpp`, a native probe executing the existing bounded ORIGINAL Timer2 handler instructions inside a virtual IRQ11 gate, then both existing wrapper/helper paths.
- New 2,880-case synthetic and SHA-pinned retail differential comparisons exercise all 16 gate flag combinations for 180 handler scenarios.
- Suppressed events never enter the original handler. Acknowledgment is separate from route admission; helper calls are counted.
- Preserved legacy regressions and independent CMake targets. Source-only ZIP; no owned game bytes, stage registry or previous phase is needed.
- This is NOT an actual PS2 Runtime callback or native game startup. There are no new original function registry entries.
