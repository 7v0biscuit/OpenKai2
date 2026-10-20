# OpenKai2

Dragon Ball Z: Budokai 2 recompilation project using
[PS2Recomp](https://github.com/ran-j/PS2Recomp).

**Status: native startup reaches IOP module loading; a title-screen boot and
gameplay have not been verified.** The current missing-disc blocker is
`MODULES/CRI_ADXI.IRX`. The complete runtime path still needs further validation.

This repository contains the Phase227 C++/Python probes, their existing tests,
and the source scripts used to build and diagnose the native runtime. Supply
your own game executable and disc files locally. Game images, assets, generated
retail C++, native game binaries, dependency checkouts, and build outputs are
excluded from Git.

## Component tests

Requirements: Python 3, CMake 3.16+, and a C++17 compiler.

```bash
python -m unittest discover -s tests -v
python RUN_PHASE227.py --skip-retail
```

The runner configures and builds the 16 standalone probes. The 113 unit tests
and synthetic native comparisons pass on Linux. These component checks do not
demonstrate game boot. For the Windows launchers and the exact scope of the
models, see [README_PHASE227.md](README_PHASE227.md).

Retail comparisons require your extracted USA executable:

```bash
python RUN_PHASE227.py --game /path/to/SLUS_207.79
```

Expected executable SHA-256:

```text
431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898
```

All existing retail comparisons pass against that executable. Original source
checksums remain in `RELEASE_MANIFEST_SHA256.json`.

## Native cloud development

The [cloud setup script](tools/cloud-native/setup.sh) reproduces the prepared
Debian 13 x86-64 `/workspace` layout. It requires Python with `venv`, GCC/G++,
Git, APT, `dpkg-deb`, `pkg-config`, and `xdpyinfo` from the host. It downloads
signed Debian dependencies into a local sysroot and fetches PS2Recomp at
`eb8688b2fd191129f103a39543002c005b33409c`. It does not install system packages.
Allow GitHub, PyPI, and `deb.debian.org` access for dependency installation.

```bash
bash tools/cloud-native/setup.sh /path/to/SLUS_207.79
/workspace/.onboarding/toolchain/bin/python -B \
  /workspace/.onboarding/budokai2-native/diagnose-startup.py
```

The setup source is published here; its retained game input, dependencies,
translation and binaries live outside the checkout under
`/workspace/.onboarding`. This cloud recipe is currently specific to that
layout. Windows users can run the component probes with the supplied launchers.

The native launcher creates its own Xvfb display, uses Mesa software rendering,
and initializes audio through an ALSA null output sink. Physical audio and input
are unverified. GCC 14.2.0 and Clang 19.1.7 were used for validation; Clang
compiles one oversized generated unit while GCC compiles the other objects.
The four upstream IOP test groups also pass.

The bounded startup diagnostic writes `startup-status.json` and
`startup-verified.log` under the native working directory. With only the ELF,
it deliberately stops at the missing-disc module and exits **3**. Put the
original `MODULES` and `USR` directories alongside the retained
`/workspace/.onboarding/game/SLUS_207.79` before continuing disc validation.
No game download service or expiring download URL is built into these tools.

The fallback analyzer processes a stripped combined code/data ELF. Some data
and VU regions become false EE function candidates, and the recompiler reports
unhandled instructions despite returning zero. Successful compilation and early
startup do not establish a correct translation of the entire game. Ghidra
function boundaries and further runtime integration may be needed.

## License

OpenKai2 source and development tooling are licensed under
[GNU GPL version 3](LICENSE), **GPL-3.0-only**, matching the PS2Recomp runtime's
license. See [NOTICE](NOTICE) for upstream attribution. Third-party dependencies
retain their own licenses. The license covers this project's code, not the
commercial game's separately supplied executable or assets.
