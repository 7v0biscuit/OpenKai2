# Vector execution

VU1 microprograms use PCSX2's microVU x86-64 recompiler. The game’s EE functions
remain statically translated by PS2Recomp, and VU0 execution retains the
existing runtime. The original game ISO is required at every boot.

The adapter imports vector, integer, accumulator, scalar, and flag registers;
retains continuation state between MSCAL and MSCNT; and exports stop flags and
64-bit elapsed cycle totals. MPG code-generation changes invalidate the native
program cache. Unchanged uploads retain compiled programs. The backend uses
SSE4.1 and restores the caller's floating-point control and nonvolatile
register state.

PATH1 submission assembles complete GIF packets, including packets that wrap
around VU memory. Host callbacks see current VU data. Callback exceptions are
reported after native execution returns. Host audio remains disabled.

Executable-memory allocation failure selects the existing interpreter before
any game vector program executes. Errors after execution starts propagate to
the runtime; partially executed programs are not retried.

## Diagnostics

Startup records `[vector:engine] backend=pcsx2-microvu`. For development
comparisons, `OPENKAI2_VECTOR_BACKEND=interpreter` selects PCSX2's reference
interpreter, and `OPENKAI2_VECTOR_BACKEND=legacy` selects the previous runtime.
These settings change VU1 execution, not the renderer or the game clocks.

## Validation

`openkai2_vector_backend_tests` runs both PCSX2 engines against independent
checks for architectural flags, host calculations across native calls,
floating-point control, vector loads, matrix
products, integer branches, continuation, cycle wraparound, scalar waits,
D/T halts, wrapped PATH1 packets, callback failure, constant registers, and code
uploads. The native backend also rejects an unsupported opcode without
unwinding an exception through generated machine code.

The application self-test additionally exercises the default VIF1/PATH1 path.
Original register, transfer, and cycle snapshots run explicitly against the
legacy interpreter whose scheduling they record.

The native backend reached character selection in a local game run. Character
pose distortion and stable game frame rates remain unresolved. Vector replay
timings exclude the EE executor and graphics work and do not establish game FPS.

## Source

The PCSX2 sources are pinned to
`dcfd663c1903025864a865d3fcf508d0caab70c7` (v2.1.27). Selected source files,
checksums, adaptation changes, and licenses are in
[`third_party/pcsx2-vu`](../third_party/pcsx2-vu/README.md). First-party host
integration is in `src/vector/`.
