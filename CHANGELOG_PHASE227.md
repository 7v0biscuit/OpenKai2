# Phase 227

- Verified Phase 226 Windows: 1,440 / 1,440 live-memory retail and synthetic comparisons pass with MSVC; all previous regressions passed.
- Reused the original translated handler's CPU/Mem backend for direct-map KSEG0/KSEG1 aliases; the pre-existing low physical fixtures retain compatibility.
- Validates every byte of a 1/2/4/8-byte read/write against a narrow sandbox region map before allowing mutation; rejects unsupported segments and unaligned accesses.
- Added a new native Phase227 mapped guest-memory interpreter probe covering original handler setup, live linked-node traversal, alias mutation, bounded backedges, and faults.
- Added independent Python MIPS opcode interpreter replay, synthetic and verified-retail comparisons, separate read/write fault tests, and Phase227 unit regressions.
- Updated standalone runner, SHA-256 source manifest, one-click launcher and diagnostics collector. No retail executable or prior phase input included.
- Not a playable port; real PS2 interrupt events and complete game memory management are not implemented.
