# Phase 225 — Original linked-list to original handler/helper chain

This independent, source-only test harness now composes the bounded original handler prefix, virtual linked-list loop at 0x001137F4..0x0011385C, threshold decision at 0x00113860, both original JAL call sites, wrapper entry points, shared virtual CP0/MMIO helper, and the original post-call continuation (when the first helper models a return).

A virtual IRQ gate suppresses all handler work unless the test fixture enables it. The linked-list is in bounded local virtual memory. A cyclic traversal halts after four backedges; no handler continuation or helpers run after that stop. Tests validate both helper targets, first/second polling stops, and both modeled returns. These gates are *not* the original game scheduler, IRQ controller, or a PS2 emulator.

The independent Python reference interprets SHA-pinned original loop and post-JAL opcodes. For the full chain it composes the previously verified original handler prefix, threshold, and helper semantics; this is bounded differential model checking, not proof of a complete executable guest function.

## Usage (Windows)
1. Extract into a fresh directory; no earlier phase files are needed.
2. Run `DOUBLE_CLICK_PHASE225.cmd` (requires Python 3, CMake, MSVC C++ Build Tools).
3. Select your personally owned Budokai 2 USA ISO (or correctly extracted original ELF) to SHA-check the original executable and compare retail-opcode paths. Without the game, synthetic comparisons still run but are not retail validation.
4. Upload `UPLOAD_PHASE225_RESULTS.zip` and `UPLOAD_PHASE225_DIAGNOSTICS.zip`.

Optional: `python RUN_PHASE225.py --skip-retail` for isolated local regression tests. `ONE_CLICK_PHASE225_DIAGNOSTICS.cmd` collects current reports. An optional external debugger trace remains supported via `--trace` and is never fabricated.

**Does not**: bundle an original game executable or original instruction bytes; register the original guest handler; execute host privileged instructions; access real PS2 MMIO; replace PS2Runtime; boot the game.

## Next milestone
Identify further actual original handler loop/interrupt paths and requirements for CPU/memory integration into a real guest-execution runtime. The game is still not bootable or playable.
