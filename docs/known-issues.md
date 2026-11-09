# Known issues

OpenKai2 runs the intro, menus, fights and Dragon World. Current work focuses on
music playback and performance on the snow stage.
Most gameplay outside the snow stage runs at roughly 60 FPS in current
playtesting.

## Gameplay and graphics

| Issue | Current status |
| --- | --- |
| Frame-rate drops and uneven frame pacing on the snow stage | The current performance issue is limited to this stage. |
| 4x internal resolution below 60 FPS on busy stages | At 4x the graphics card draws sixteen times the pixels of native resolution. On an RTX 4060 the snow stage runs at about 45 to 50 FPS at 4x; 2x or 3x keeps 60 FPS. |
| Imported Budokai 3 characters render black or blue | Unresolved. |
| Imported Budokai 3 stages lack particle effects | Unresolved. |

The [Dragon World validation record](dragon-world-validation.md) separates
completed checks from the remaining stage-completion work.

Previously reported distortion of character limbs, hair and accessories is
resolved and is no longer an active issue.
The stage 1 Goal transition has also passed testing through entry into stage 2;
the earlier progression failure has not reproduced on the tested route.

## Audio

Background music remains a known problem:

- Music can sound thin, flat or distorted.
- Tracks may stop after a fight or fail to resume when returning to character
  selection.
- Sound effects generally fare better, but occasional glitches or crackling
  remain reported.
- When the game runs below full speed, audio is stretched to match and can
  drop out briefly as it adjusts. Keeping 60 FPS (for example a lower internal
  resolution) avoids this.

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
