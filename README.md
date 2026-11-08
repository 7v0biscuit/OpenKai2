# OpenKai2

A native Windows port of **Dragon Ball Z: Budokai 2** (PS2, USA), made by
recompiling the game's own code with [PS2Recomp](https://github.com/ran-j/PS2Recomp).
You need your own copy of the game: OpenKai2 reads your ISO and includes no game
files.

[![Build](https://github.com/7v0biscuit/OpenKai2/actions/workflows/probes.yml/badge.svg)](https://github.com/7v0biscuit/OpenKai2/actions/workflows/probes.yml)
[![Windows downloads](https://img.shields.io/badge/Windows-downloads-0078D4)](https://github.com/7v0biscuit/OpenKai2/releases)
[![GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**[Download](https://github.com/7v0biscuit/OpenKai2/releases)** ·
[Setup and controls](docs/windows.md) ·
[Online play](docs/online.md) ·
[Speedrun mode](docs/speedrun.md) ·
[Graphics](docs/graphics.md) ·
[Save states](docs/save-states.md) ·
[Modding](docs/modding.md) ·
[Report an issue](https://github.com/7v0biscuit/OpenKai2/issues/new/choose)

> **Early development.** Releases are pre-releases. The game runs, but it is not
> yet a finished, bug-free port. See [Known issues](#known-issues).

## What works

- **The game:** intro, menus, versus fights, and Dragon World with its board and
  fights. Music, voices and sound effects play.
- **Graphics:** GPU rendering (OpenGL 4.3 or Vulkan) with a CPU fallback, internal
  resolution up to 4x, HD textures (upscaled from your disc or from texture packs),
  window sizes up to 4K, 16:9, three FXAA levels, and several upscalers including AMD FSR 1.
  See [Internal resolution and HD textures](docs/graphics.md).
- **Online play:** versus over the internet from the launcher, with profiles,
  friends, invites, a room browser and room passwords. Both games run in lockstep
  and check that they stay in sync. [Experimental]
- **Save states:** F5 saves and F9 loads the whole console outside speedrun and online play.
- **Audio:** volume, and an option to turn off only the music while voices and
  sound effects keep playing.
- **CPU difficulty:** Z1 to Z6 levels above Very Hard, set in the launcher or
  with F6 in game. See [docs/cpu-difficulty.md](docs/cpu-difficulty.md).
- **Speedrun mode:** Dragon World and World Tournament category profiles, automatic
  story and Tournament timing, splits, Z1 to Z6 variants, permadeath
  and checksummed session logs.
- **Mods:** add characters, movesets and stages from other Dragon Ball PS2 games
  without changing your ISO.
- **Saves:** import, back up and restore memory card saves, with an optional
  unlock-all. Select individual saves or backups in the launcher to restore or delete.
- **Launcher:** automatic updates, Discord activity, and the settings above.

## Known issues

- **Dragon World dialogue:** the speech text and the ally Yes/No text do not
  appear, making some prompts difficult to use.
- **Music:** background music can sound thin or distorted and may cut out
  after a fight or when returning to character selection.
- **Snow stage:** frame-rate drops and uneven frame pacing remain an issue.
  Most gameplay outside this stage runs at roughly 60 FPS.
- **Imported Budokai 3 characters** look black or blue.
- **Imported Budokai 3 stages** load without their particle effects.

See the [known-issues record](docs/known-issues.md) for current status,
workarounds and issues awaiting retesting.

## Getting started

1. Choose a download from the [current release](https://github.com/7v0biscuit/OpenKai2/releases):
   **OpenKai2-Setup.exe** to install, **OpenKai2.exe** for the portable app, or
   **OpenKai2-windows-x64.zip** to extract manually.
2. Open it, choose your **Budokai 2 USA ISO**, and press **Play**.

The ISO's files are checked before every launch. Logs are in
`%LOCALAPPDATA%\OpenKai2\logs`. Please attach `startup.log` when you
[report a bug](https://github.com/7v0biscuit/OpenKai2/issues/new/choose).

## Building

See [Building OpenKai2](docs/building.md). Quick checks need Python 3.10+, CMake
3.16+ and a C++20 compiler:

```sh
python -m unittest discover -s tests
python tools/probes/run.py --skip-retail
```

The [development status](docs/status.md) has technical notes on each fix.

## License

OpenKai2 is licensed under [GPL-3.0-only](LICENSE). See [NOTICE](NOTICE) for
third-party attribution.

OpenKai2 is an independent fan project, not affiliated with the game's publishers
or rights holders. Game content and trademarks belong to their owners.
