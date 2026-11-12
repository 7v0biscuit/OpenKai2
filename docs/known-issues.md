# Known issues

The game has been verified through completion in playtesting. Dragon World has
been beaten through all nine stages and the ending. Music playback and mod
tools are the remaining active issue areas.

## Music playback

Music is mixed at 48 kHz stereo from the game's sound chip output. The remaining
problems concern playback consistency:

- A track may fail to load after a fight or fail to resume when returning to
  character selection.
- A long frame hitch can briefly interrupt the music. At substantially reduced
  game speed, time stretching can affect its clarity.

A crackle that started a few seconds into playback and then persisted was
fixed after v0.2.7. It came from the step that matches the game's audio rate to
the sound card's clock, not from the sound card running short. If you still
hear pops, please report them with `startup.log`.

Missing tracks remain under investigation. `startup.log` ends with an
`[audio:device]` line showing whether the output buffer ran short, records
`[audio:music] resumed after_silence_s=...` each time the music stream restarts
after two or more seconds of silence, and adds `starved=` to the `[audio:adma]`
lines, counting the times the sound chip ran out of stream data. Include the
log when reporting a problem.

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
