# OpenKai2

[![Component tests](https://github.com/7v0biscuit/OpenKai2/actions/workflows/probes.yml/badge.svg)](https://github.com/7v0biscuit/OpenKai2/actions/workflows/probes.yml)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)

A native PC recompilation project for **Dragon Ball Z: Budokai 2**, built with
[PS2Recomp](https://github.com/ran-j/PS2Recomp).

**Early development.** The native executable reaches IOP module loading.
Title-screen boot and gameplay are not yet verified. See the
[development status](docs/status.md) for current limitations.

## Getting started

The component tests require Python 3.10+, CMake 3.16+, and a C++17 compiler.

```sh
python -m unittest discover -s tests -v
python tools/probes/run.py --skip-retail
```

To compare the probes against your own USA game executable:

```sh
python tools/probes/run.py --game /path/to/SLUS_207.79
```

For native runtime setup, supported platforms, and diagnostics, see
[Building OpenKai2](docs/building.md).

## Project layout

```text
src/probes/          Native EE, interrupt, and guest-memory probes
tests/              Python regression tests and synthetic fixtures
tools/probes/       Component runner, diagnostics, and reference models
tools/native/       PS2Recomp build and runtime tools
docs/               Build instructions and development status
```

Build output, reports, dependencies, and game files stay local. No game
executables, disc images, assets, or generated game translations are included.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow.
[Report a bug](https://github.com/7v0biscuit/OpenKai2/issues) with reproduction
steps, your platform, and the relevant diagnostic output.

## License

OpenKai2 is licensed under [GPL-3.0-only](LICENSE). Third-party attribution is
listed in [NOTICE](NOTICE).

OpenKai2 is an independent fan project and is not affiliated with or endorsed
by the game's publishers or rights holders. Game content and trademarks belong
to their respective owners.
