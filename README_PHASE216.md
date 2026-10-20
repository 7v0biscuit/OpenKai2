# Phase 216: Virtual MMIO store, ErrorEPC, and ERET translation

Self-contained Phase 216 native C++ differential probe for the PS2 USA retail
shared routine at `0x00113380`. Does not need Phase 215 or any prior release.

## Retail instruction coverage

The latest hardware-side suffix is exactly `0x001133D8` `sw a1,0(a0)`,
`0x001133DC` `mtc0 ra,ErrorEPC` (CP0 register 30), `0x001133E0` `sync`,
and `0x001133E4` `eret`. The following `jr ra` is not executed after ERET
on the modeled path; `ERET` has **no delay slot**. The remaining earlier
instruction semantics and EI polling are retained from Phase 215.

The single store records a **virtual, low-32-bit** value, only for exactly
`0xFFFFFFFFB0001000`, `0xFFFFFFFFB0001010`, or `0xFFFFFFFFB0001020`.
Unrecognized/unaligned addresses are rejected before the virtual store.
No host hardware memory is dereferenced.

The CP0 model records virtual ErrorEPC from the **low 32 bits of `ra`**,
per `mtc0`, and models ERET selecting virtual ErrorEPC when ERL is set, else
virtual EPC when EXL is set. It models ERL/EXL clearing. This is bounded
instruction testing against pinned retail opcode words, **not confirmation
of PS2 real-hardware EI/ERET behavior**. The EI status results come from
fixture inputs. Sync is an event/ordering fence in the native probe.

## Run

On Windows, double-click `DOUBLE_CLICK_PHASE216.cmd` from a clean folder.
Requires Python, CMake, MSVC C++ tools; option to choose owned USA ISO/ELF.
Previous phases, build folders, stage registry, and PS2Runtime NOT required.

Runs four independent probes: 560 Phase216 virtual-MMIO/ERET scenarios,
112 Phase215 CP0 scenarios, 63 Phase214 entry scenarios, and 26 Phase213
wrapper scenarios, first with synthetically assembled opcodes, then, if
provided, using the SHA256-verified retail executable and complete pinned
64-instruction shared window.

- Uploaded reports: `UPLOAD_PHASE216_RESULTS.zip`, `UPLOAD_PHASE216_DIAGNOSTICS.zip`
- Offline-only: `python RUN_PHASE216.py --skip-retail`
- Owned image: `python RUN_PHASE216.py --game "C:\path\owned.iso"`

## Limitations

The C++ functions are isolated differential probes and have not been registered
with the game dispatcher or integrated into a PS2Runtime build. No native
playable Windows game EXE is produced. The synthetic-EI transition and virtual
MMIO effects cannot establish complete console hardware behavior. The
bounded test comparisons likewise do not prove the complete game works.
