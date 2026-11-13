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

> **Gameplay verified through completion.** Dragon World has been beaten in
> playtesting, including all nine stages and the ending. Current work focuses
> on music playback and mod tools. Releases remain pre-releases while these
> issues are resolved. See [Known issues](#known-issues).

## What works

- **The game:** intro, menus, versus fights, and Dragon World through all nine
  stages and the ending. Voices and sound effects play; remaining music issues
  are listed below.
- **Graphics:** GPU rendering (OpenGL 4.3 or Vulkan) with a CPU fallback, internal
  resolution up to 4x, HD textures (upscaled from your disc or from texture packs),
  window sizes up to 4K, 16:9, three FXAA levels, and several upscalers including AMD FSR 1.
  See [Internal resolution and HD textures](docs/graphics.md).
- **Online play:** versus over the internet from the launcher, with profiles,
  friends, invites, a room browser and room passwords. Both games run in lockstep
  and check that they stay in sync. [Experimental]
- **Save states:** F5 saves and F9 loads the whole console outside speedrun and online play.
- **Randomizer:** a random character, with its own moves and skills, for every
  Dragon World and World Tournament fight. Works with speedruns and permadeath.
- **Launcher:** a lounge with friends, searchable rooms and selected-room details;
  six dock tabs, Play shortcuts for controls and saves, an action finder,
  thirteen illustrated themes, custom
  colors, local PNG/JPEG/GIF player cards, profile recovery, availability and quiet
  notifications, reduced motion, ISO verification, saves and backups,
  automatic updates and Discord activity. See [Windows](docs/windows.md).
- **Controls:** rebind every PS2 button to a key, an alternate key and a controller
  button through **Play > Controls**. Works with Xbox, PlayStation and most other controllers.
- **Audio:** volume, and an option to turn off only the music while voices and
  sound effects keep playing.
- **CPU difficulty:** Z1 to Z6 levels above Very Hard, set in the launcher or
  with F6 in game. See [docs/cpu-difficulty.md](docs/cpu-difficulty.md).
- **Speedrun mode:** Dragon World and World Tournament category profiles, automatic
  story and Tournament timing, splits, Z1 to Z6 variants, permadeath
  and checksummed session logs. Custom split names and your own PNG, JPEG or
  animated GIF icons are supported; PS2 time and verification labels stay fixed.
- **Mods:** add characters, movesets and stages from other Dragon Ball PS2 games
  without changing your ISO.
- **Saves:** import, back up and restore memory card saves, with an optional
  unlock-all. Select individual saves or backups in the launcher to restore or delete.

## Known issues

- **Music:** a track may fail to load after a fight or when returning to
  character selection. A crackle that built up a few seconds into playback is
  fixed; if you still hear pops, please send `startup.log`.
- **Imported Budokai 3 stages** bring their fight effects but not their moving
  stage objects.
- **Budokai 3 movesets** (experimental): the character animates, but buttons
  may not yet trigger the right moves.

Music playback and mod tools are the remaining active issue areas. See the
[known-issues record](docs/known-issues.md) for details and workarounds, and the
[Dragon World validation record](docs/dragon-world-validation.md) for completion
status and regression coverage.

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
