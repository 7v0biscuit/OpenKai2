# PCSX2 vector core

Selected PCSX2 sources from [v2.1.27](https://github.com/PCSX2/pcsx2/tree/dcfd663c1903025864a865d3fcf508d0caab70c7),
commit `dcfd663c1903025864a865d3fcf508d0caab70c7`, provide the VU interpreter,
microVU x86-64 recompiler, and native instruction emitter. The original source
paths, file hashes, copyright notices, and licenses are retained.

`standalone.patch` removes the EE macro compiler include and unused emulator
save-state serializer and enables D-bit handling so FBRST-controlled debug
halts retain the runtime contract. Windows dispatcher frames also preserve
XMM6-XMM15 for standard C++ callers. OpenKai2 supplies host integration in `src/vector/`.
The game executable continues to use PS2Recomp's statically translated EE code.

The PCSX2 sources use LGPL-3.0-or-later. The included fast_float headers use the
MIT license. See `COPYING.GPLv3`, `COPYING.LGPLv3`, and the upstream fast_float
license and contributor files.

Four fmt 12.1.0 headers (commit
`407c905e45ad75fc29bf0f9bb7c5c2fd3475976f`) support diagnostic formatting.
They retain their MIT license and are compiled in header-only mode.
