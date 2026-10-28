# PCSX2 SPU2 core

Selected PCSX2 sources from [v2.1.27](https://github.com/PCSX2/pcsx2/tree/dcfd663c1903025864a865d3fcf508d0caab70c7),
commit `dcfd663c1903025864a865d3fcf508d0caab70c7`, provide SPU2 voice decoding,
ADSR envelopes, Gaussian interpolation, reverb and the two-core mixer. The
original source paths, file hashes, copyright notices and license are retained.

`standalone.patch` makes the reverb resamplers use PCSX2's scalar reference
filters instead of its GS vector library. Host integration lives in
`src/audio/`: the core runs as a passive mirror of the emulated IOP's SPU2
register writes and transfers, and its output feeds the host audio stream.

The PCSX2 sources use LGPL-3.0-or-later. See `COPYING.LGPLv3`.
