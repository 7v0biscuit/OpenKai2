# Phase 224 — Native original-handler linked-list traversal

**Install from an empty directory:** Extract this ZIP and double-click `DOUBLE_CLICK_PHASE224.cmd`. Python 3.10+, CMake and Visual Studio C++ Build Tools are required on Windows. The runner may prompt for your owned **Budokai 2 USA ISO** for instruction identity verification. No prior phase packages, diagnostic results, registry, PS2Recomp or PS2Runtime checkout are needed. Upload `UPLOAD_PHASE224_RESULTS.zip` and `UPLOAD_PHASE224_DIAGNOSTICS.zip`.

## New original MIPS translation

The new independent native C++ probe executes the bounded original handler **0x001137F4 through 0x0011385C**, starting after the original `beq` at 0x001137EC. This formerly uncovered control-flow region traverses linked nodes with `LD`, `LW`, 64-bit `DADDIU/DADDU/DSUBU`, unsigned `SLTU`, `SLTI`, two conditional branches, `BEQL` with conditional delay slot, and an unconditional loop backedge. MIPS 32-bit signed loads and 64-bit wraparound are modeled explicitly. It stops at 0x00113860 or at a **four-backedge test limit**.

An independent generic opcode interpreter decodes and evaluates the exact 27 MIPS words. The retail verification refuses any ELF other than the pinned USA SHA-256 identity and separately checks the 27-word SHA-256 digest `e7e424288479bb43b5ceac2683a2d39d749072625ad42e16a8da863311047315`. The release includes only minimal disassembly/testing data, never the game's executable or assets.

The tests cover early exit, end-of-list, BEQL-taken delay execution, BEQL-untaken annulment, multiple node visits, cyclic link guard, 64-bit edge cases, and distinct hardware-state masks. All reads occur in small **virtual node fixtures**; there are no host MMIO access or privileged guest instructions. Previous bounded handler, wrapper/helper, IRQ11, CP0 and MMIO regressions are retained in the same package.

## Important integration boundary

This is an *additional isolated original handler code region*, **not a connected execution of the entire original handler**. The Phase 223 IRQ-to-handler test remains bounded to earlier paths. The new region must be integrated with the real preceding branch and the downstream threshold/helper stages after memory-state propagation and control flow have been independently tested. No guest callback is registered, no actual PS2 hardware timing has been confirmed, and no full native Windows game is bootable.

Optional CLI: `python RUN_PHASE224.py --game "C:\path\to\owned.iso"`; `--skip-retail` runs synthetic comparisons only. `ONE_CLICK_PHASE224_DIAGNOSTICS.cmd` packages existing reports.
