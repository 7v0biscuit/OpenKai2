# Known issues

The game has been verified through completion in playtesting. Dragon World has
been beaten through all nine stages and the ending. Music playback and mod
tools are the remaining active issue areas.

## Music playback

Music is mixed at 48 kHz stereo from the game's sound chip output. The remaining
problems concern playback consistency:

- Music can pop intermittently, starting a few seconds after the main menu
  loads.
- A track may fail to load after a fight or fail to resume when returning to
  character selection.
- A long frame hitch can briefly interrupt the music. At substantially reduced
  game speed, time stretching can affect its clarity.

The pops and missing tracks remain under investigation. Since v0.2.0,
`startup.log` ends with an `[audio:device]` line showing whether the output
buffer ran short; include it when reporting a problem.

As a temporary workaround, set **Music** to **Off** in the launcher. This
silences music tracks while retaining voices, sound effects and video audio.

## Mod tools

| Issue | Current status |
| --- | --- |
| Budokai 3 movesets | Experimental. Imported characters animate, but buttons may not trigger the correct moves. |
| Imported Budokai 3 stages | Fight effects such as dust and impacts are imported. Moving stage objects and some stage-wide effects are not yet supported. |

See [Modding](modding.md) for the supported import workflow and current limits.

## Gameplay verification

Full Dragon World completion has been reported in playtesting. Earlier
progression, dialogue and animation reports are no longer listed as active
issues. The [validation record](dragon-world-validation.md) documents this
completion status alongside the targeted checks and automated regressions.

For graphics configuration, including internal resolution and automatic
resolution scaling, see [Graphics](graphics.md).

## Reporting a problem

Attach `startup.log` from `%LOCALAPPDATA%\OpenKai2\logs` and describe the mode,
the action immediately before the problem, and whether the window closes or
remains open. For music problems, include where playback stopped and whether
voices and sound effects continued. Include `launcher.log` for launcher or
import-tool issues.

Report problems through [GitHub Issues](https://github.com/7v0biscuit/OpenKai2/issues/new/choose).
