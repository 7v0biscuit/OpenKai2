# OpenKai2

Native PC recompilation of **Dragon Ball Z: Budokai 2**, built with
[PS2Recomp](https://github.com/ran-j/PS2Recomp).

[![Build](https://github.com/7v0biscuit/OpenKai2/actions/workflows/probes.yml/badge.svg)](https://github.com/7v0biscuit/OpenKai2/actions/workflows/probes.yml)
[![Windows downloads](https://img.shields.io/badge/Windows-downloads-0078D4)](https://github.com/7v0biscuit/OpenKai2/releases)
[![GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

[Download for Windows](https://github.com/7v0biscuit/OpenKai2/releases)
· [Setup and controls](docs/windows.md)
· [Development status](docs/status.md)
· [Report an issue](https://github.com/7v0biscuit/OpenKai2/issues/new/choose)

## Windows downloads

Choose a package from the [current release](https://github.com/7v0biscuit/OpenKai2/releases).

| Download | Use |
| --- | --- |
| **OpenKai2-Setup.exe** | Install for your Windows account with Start-menu shortcuts. |
| **OpenKai2.exe** | Launch without installation. |
| **OpenKai2-windows-x64.zip** | Extract the application and runtime DLLs into a folder. |
| **SHA256SUMS.txt** | Verify the downloaded packages. |

Launch OpenKai2 and select your original **Budokai 2 USA ISO**. The ISO is
required at every game launch, and its files are verified before execution.
Game files are not included in the downloads.

See the [Windows guide](docs/windows.md) for requirements, keyboard controls,
audio diagnostics, and log locations.

## Current status

**In development — complete gameplay is not yet supported.** retail testing
has reached the title screen, menus, and versus level selection. Battle loading
still stops, frame rates are unstable, and audio playback needs further work.
Stable 60 FPS and complete matches have not been verified.

The [development status](docs/status.md) records the current failures and the
scope of automated validation. Release checks cover component behavior and
Windows packaging; they do not establish that the full game is playable.

## Build and contribute

Start with [Building OpenKai2](docs/building.md) for the native runtime, or the
[Windows build guide](docs/windows.md) for Windows-specific setup.

Component checks require Python 3.10+, CMake 3.16+, and a C++17 compiler:

```sh
python -m unittest discover -s tests
python tools/probes/run.py --skip-retail
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow. When
[reporting a bug](https://github.com/7v0biscuit/OpenKai2/issues/new/choose), include
the release tag, reproduction steps, hardware, and relevant startup log.
Do not upload game files.

## Source layout

| Directory | Contents |
| --- | --- |
| `src/` | Application, ISO verification, and runtime integration |
| `src/probes/` | EE instruction, interrupt, and guest-memory checks |
| `tests/` | Python regression tests and synthetic fixtures |
| `tools/native/` | Native builds, runtime overlays, and release packaging |
| `tools/probes/` | Component runner and reference models |
| `docs/` | Build guides, development status, and release notes |
| `packaging/` | Windows launcher and installer configuration |
| `third_party/` | Third-party notices for distributed dependencies |

Generated sources, build output, game executables, and disc assets are excluded
from the source tree.

## License and attribution

OpenKai2 is licensed under [GPL-3.0-only](LICENSE). See [NOTICE](NOTICE) for
third-party attribution.

OpenKai2 is an independent fan project, unaffiliated with the game's publishers
or rights holders. Game content and trademarks belong to their respective owners.
