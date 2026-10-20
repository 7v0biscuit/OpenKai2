# Phase 216

- Added isolated native C++ model for verified instructions `0x001133D8` through `0x001133E4` (`SW`, `MTC0 ErrorEPC`, `SYNC`, `ERET`) and identified the unreachable fallthrough `JR RA`.
- Added independent per-word MIPS oracle with a virtual CP0 ErrorEPC/EPC state and only whitelisted virtual guest memory store addresses.
- Added 560 differentiated scenarios covering normal virtual return, bounded EI polling and rejected virtual MMIO store, with return-address truncation, register side effects and event sequencing.
- Retained Phase 215/214/213 native regressions (112, 63, 26 cases) in the standalone runner.
- Retail executable required only for optional actual-word parity. Full ISO/ELF hash and 64-instruction shared-window fingerprint checked. No retail binaries bundled.
- No prior releases, staged registry, checkout or runtime required. The complete game remains unbootable.
