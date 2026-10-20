# Phase 226 — original handler: live guest byte-memory reads

This source-only, **standalone** experiment fixes a semantic disconnect in Phase 225: the original handler prefix wrote to a byte-addressable guest memory snapshot, but the list walker read a separate pre-decoded `Node[3]` array. Phase 226 now performs every original list LD/LW through the **same** in-memory guest CPU memory object already used by the original handler entry and successor blocks.

## Newly covered behavior

- Original list MIPS instruction positions `0x001137F4..0x0011385C` are natively translated into explicit memory loads with 4/8-byte alignment and bounded address checks, 32-bit LW sign extension, 64-bit wraparound, null-link exit, BEQ/BEQL delay-slot semantics, and a finite traversal budget.
- A synthetic **post-prefix patch** changes a node's next pointer, +16, +24 or +32 field inside the SAME guest memory snapshot before the list traversal. The handler-to-helper chain then consumes the result. This is fixture testing, not proof that the original game modifies node fields at that checkpoint.
- An independent interpreter of the original 27 SHA-pinned MIPS list opcodes recomputes outputs after each patch. The first prefix read retains its original input; subsequent list reads observe the injected update.
- 1,440 cases cover 360 original entry/IRQ/helper fixtures x four patch modes. Suppressed virtual interrupts perform zero list reads. Both original call sites, double-helper continuations, and all inherited native regressions remain exercised.
- Extra audit outputs count guest loads and bytes read. Real CPU/MMIO/guest interrupt hardware is never accessed.

## Run on Windows

1. Extract the ZIP into an empty directory. **Prior phases are not needed.**
2. Double-click `DOUBLE_CLICK_PHASE226.cmd`. Requires Python 3, CMake and MSVC C++ Build Tools.
3. Select your own **Budokai 2 USA** ISO or original ELF. The runner verifies the exact expected executable SHA-256 and pinned MIPS windows before calling any test "retail-derived".
4. Upload `UPLOAD_PHASE226_RESULTS.zip` and `UPLOAD_PHASE226_DIAGNOSTICS.zip`.

For synthetic-only local execution: `python RUN_PHASE226.py --skip-retail`. For capture comparison, the older optional `--trace` input remains available; absent a true capture it reports NO_EXTERNAL_TRACE_SUPPLIED.

## Limits

The native handler is **bounded and incomplete**. Memory is a deliberately small, simulated byte map; this release is not a PS2 EE memory bus, scheduler, PS2Runtime integration, original IRQ registration, graphics renderer, native game executable or playable port. It models existing test fixtures and original compiled instruction windows, but **does not validate real guest interrupts or ERET timing**.

## Next engineering step

Construct a larger guest memory/CPU interface with explicit mapping/aliasing/fault semantics, then connect the remaining original handler branches to that interface and a genuine native runtime dispatch path. A full native game boot requires translation and integration of far more of the retail executable.
