# Phase 222 changes

- Verified uploaded Windows Phase 221 results: all 60 handler-to-helper
  retail comparisons passed and all inherited virtual CP0/MMIO/IRQ regressions passed.
- Native post-JAL continuation in 2 windows comprising 19 retail instruction
  positions, including two delay-slot tail jumps to the *other* helper.
- Correct 0x1138C8 DSRLV operand direction and use a decoded-opcode oracle.
- Verify stack frame restoration and original caller RA; reject invalid first
  helper ERET return to JAL+8, avoid continuing after polling stop.
- End-to-end two-helper virtual execution and independent 180-case comparison.
- SHA-pin both retail continuation windows; do not bundle retail words or game.
- Independent Phase 222 Windows runner; preserve inherited test suite.
- No new retail bindings, no actual game boot, no physical PS2 observation.
