# Development status

OpenKai2 is in early development. There is no verified playable build yet.

## Verified

- All 16 native component probes build on Linux.
- All 103 Python regression tests pass.
- Synthetic comparisons and comparisons against the USA executable pass.
- The native runtime loads the ELF and starts execution at `0x00100008`.
- OpenGL initializes with Mesa llvmpipe; audio initializes with a null sink.
- Startup reaches IOP module loading.
- Four upstream IOP test groups pass.

## Current blocker

With only the executable available, startup cannot load
`cdrom0:\MODULES\CRI_ADXI.IRX`. The diagnostic stops this retry loop and reports
`BLOCKED_MISSING_DISC_MODULE`. Original disc files are required to continue
runtime validation.

`SIO2MAN.IRX` and `LIBSD.IRX` are also absent in that configuration; upstream
HLE providers handle those early requests. This does not establish compatibility
with the rest of the game's IOP modules.

## Remaining work

The fallback analyzer reads a stripped ELF containing both code and data. It
identified 5,122 function candidates: 4,953 translated functions and 169 SDK
stubs. Some data and VU regions were treated as EE functions, producing 12,755
unhandled-instruction diagnostics despite the recompiler returning zero.
More accurate function boundaries and further runtime integration may be needed.

Title-screen boot, gameplay, physical audio output, controller input, graphics
accuracy, and full interrupt timing remain unverified. Component probes exercise
bounded models and do not demonstrate a complete game boot.
