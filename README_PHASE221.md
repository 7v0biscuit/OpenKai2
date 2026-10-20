# Phase 221 — Original handler to shared helper bridge

This **standalone source-only ZIP** needs no earlier phase archives, staged
registry, PS2Recomp, PS2Runtime, or existing build directory. Run
`DOUBLE_CLICK_PHASE221.cmd` on Windows. Prerequisites: Python 3.10+, CMake,
Visual Studio C++ Build Tools. Choose an owned **Budokai 2 USA** ISO or
extracted SLUS_207.79; it must match ELF SHA-256
`431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898`.

## Actual engineering change

Earlier work had two separate execution tests: (1) translated **original
handler** basic blocks from `0x00113770` stopping at one of two `JAL` call
boundaries, and (2) the original two four-instruction wrapper functions
`0x00113400` and `0x00113410` feeding a virtual implementation of
`0x00113380`. Phase 221 combines these into **one compiled C++ process**.
The handler's calculated guest `a0`, `a1`, `v0`, `v1`, `a2`, `ra`, and `sp`
are passed without recreation into the wrapper and then virtual shared
helper. The wrapper's actual output feeds virtual CP0/MMIO. A separate Python
MIPS instruction interpreter constructs expected values from SHA-pinned
original retail opcode windows. Cases visit **both actual JAL call sites**
`0x001138B8` and `0x00113884`.

Phase 221 runs its own 60 bridge comparisons (24 for helper 0x113400 and 36
for 0x113410), plus the earlier original handler call-boundary tests and the
complete Phase 220 virtual regressions. If no owned image is selected the
synthetic comparisons still run; the retail identity check is then **not**
claimed. The source-only package intentionally does not contain game bytes.

## Boundaries and remaining work

- The native handler translation is **bounded and not complete**; several
  reachable control paths remain untranslated.
- Phase 220's IRQ11 queue/routing/ack test is still a **virtual admission
  contract**, **not** an actual guest Timer2 handler callback.
- The shared helper uses a virtual CP0/MMIO/ERET model with fixture-supplied
  external interrupt changes. Do not infer actual PS2 interrupt timing.
- The helper's ERET stops the modeled chain. The original handler's subsequent
  instructions after external calls are not resumed or validated here.
- **No game executable, native scheduler, graphics, input, assets, or game
  runtime integration** is delivered. Full game remains unbootable.

Output: `UPLOAD_PHASE221_RESULTS.zip`, `UPLOAD_PHASE221_DIAGNOSTICS.zip`.
Keep backups of your privately owned game images. To verify without a dialog:
`python RUN_PHASE221.py --game "C:\\path\\to\\your-game.iso"`.
