# Phase 218 changes
- Reused the Phase 217 full standalone source and test suite, requiring no prior downloads.
- Added `phase218_interrupt_transition_chain.cpp` and independent opcode-based comparison module.
- Added EE CP0 EIE/EDI and effective mode permission checks, with externally injected status-change observations kept separate from the EI instruction effect.
- Added 384 integration cases and self-contained tests; retained previous native and retail differential suites.
- Fixed phase identifier and added explanatory status flags, including explicit absence of real-hardware timing validation.
- No guest registers or hardware accesses were applied to host hardware. No original function registered or game booted.
