# Phase 222 — Original handler post-helper continuations and dual-call chain

**Stand-alone source-only package:** extract this ZIP to a *new empty folder*.
No prior-phase files, archives, registry, runtime, PS2Recomp checkout, or old
build directory are needed. Install Python 3.10+, CMake, and Visual Studio C++
Build Tools. On Windows, double-click `DOUBLE_CLICK_PHASE222.cmd`.

Optional retail verification requires your legally owned **Dragon Ball Z:
Budokai 2 (USA)** ISO or extracted `SLUS_207.79`, whose extracted executable
SHA-256 must match `431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898`.
The package itself never distributes the executable or assets. The Windows
runner accepts `--game "C:\path\to\game.iso"` for a noninteractive input.

## New native translation

Phase 221 stopped at either original MIPS JAL boundary. Phase 222 compiles
**19 additional original MIPS instruction positions** into native C++,
covering both post-helper continuation windows:

* `0x0011388C` through `0x001138AC`, followed by a tail jump to `0x00113400`;
* `0x001138C0` through `0x001138E4`, followed by a tail jump to `0x00113410`.

Each window restores the saved `ra`, `s0`, `s1`, `s2`, and `sp` values under the
original MIPS ABI, and runs the **mandatory jump delay slot** before entering
the *other* verified helper wrapper. The `DSRLV` at `0x001138C8` is decoded
as **`a0 = s0 >> (a0 & 63)`**, not the operands in reverse order. Return from
the first helper must match the original JAL+8 address; a bounded poll stop
must NOT silently resume handler instructions. The wrapper and shared helper
are executed in the **same native process**, using in-memory virtual CP0 and
MMIO state. MIPS ERET behavior remains modeled, not certified on real PS2
hardware.

A separate Python MIPS **opcode interpreter** validates the two post-JAL
windows, with SHA-256 pinning of all 19 original instruction words read from
your owned game executable. It checks both paths and the second helper's
virtual store/return behavior. The 180 new scenario matrix includes:

* 30 first-helper polling stops;
* 50 second-helper polling stops;
* 100 modeled second-helper returns;
* 90 tail jumps into `0x00113400`, 60 into `0x00113410`.

Other existing regessions (original handler call boundaries, virtual IRQ11
admission, virtual CP0, wrapper/shared helper) remain in this archive.

## Still not playable

There is **no original guest IRQ11 dispatch, whole-game translated runtime,
verified real-PS2 interrupt timing, graphics, assets, or bootable game EXE**.
Only bounded native fragments run against synthetic memory and CPU snapshots.
No actual host hardware registers or guest privileged instructions are used.

After the Windows run, upload `UPLOAD_PHASE222_RESULTS.zip` and
`UPLOAD_PHASE222_DIAGNOSTICS.zip` for the next engineering step.
