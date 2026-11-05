# Known issues

OpenKai2 runs the intro, menus and fights. Current work focuses on Dragon World
progression, music playback and performance on the snow stage.
Most gameplay outside the snow stage runs at roughly 60 FPS in current
playtesting.

## Gameplay and graphics

| Issue | Current status |
| --- | --- |
| Dragon World does not advance after reaching the Goal following the Raditz and Nappa fights | Reported on v0.1.83. Boss victories return to the board, but the Goal, save and next-stage transition still need validation and fixes. |
| Missing Dragon World dialogue and ally Yes/No labels | Reproduced. Boxes and portraits appear while text is missing, making prompts difficult to use. |
| Frame-rate drops and uneven frame pacing on the snow stage | The current performance issue is limited to this stage. |
| Imported Budokai 3 characters render black or blue | Unresolved. |
| Imported Budokai 3 stages lack particle effects | Unresolved. |

The [Dragon World validation record](dragon-world-validation.md) separates
completed checks from the remaining stage-completion work.

Previously reported distortion of character limbs, hair and accessories is
resolved and is no longer an active issue.

## Audio

Background music remains a known problem:

- Music can sound thin, flat or distorted.
- Tracks may stop after a fight or fail to resume when returning to character
  selection.
- Sound effects generally fare better, but occasional glitches or crackling
  remain reported.

Music quality and track dropouts are under investigation, including playback
across scene changes.

As a temporary workaround, set **Music** to **Off** in the launcher. This
silences music tracks while retaining voices, sound effects and video audio.

## Reporting a problem

Attach `startup.log` from `%LOCALAPPDATA%\OpenKai2\logs` and describe the mode,
the action immediately before the problem, and whether the window closes or
remains open. For music problems, include where playback stopped and whether
voices and sound effects continued. Include `launcher.log` for launcher issues.

Report problems through [GitHub Issues](https://github.com/7v0biscuit/OpenKai2/issues/new/choose).
