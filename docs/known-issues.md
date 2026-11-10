# Known issues

OpenKai2 runs the intro, menus, fights and Dragon World. Current work focuses on
music playback. Gameplay runs at roughly 60 FPS in current playtesting.

## Gameplay and graphics

| Issue | Current status |
| --- | --- |
| 4x internal resolution below 60 FPS on busy stages | At 4x the graphics card draws sixteen times the pixels of native resolution. On slower cards use 2x or 3x to keep 60 FPS. |
| Budokai 3 movesets (experimental) | A forced character with its Budokai 3 moves animates, but buttons may not yet trigger the right moves. |
| Imported Budokai 3 stages lack animated objects and stage-wide effects | Fight effects such as dust and impacts are imported; moving stage objects are not yet. |

The [Dragon World validation record](dragon-world-validation.md) separates
completed checks from the remaining stage-completion work.

Previously reported distortion of character limbs, hair and accessories is
resolved and is no longer an active issue.
The stage 1 Goal transition has also passed testing through entry into stage 2;
the earlier progression failure has not reproduced on the tested route.

## Audio

Background music remains a known problem:

- Tracks may stop after a fight or fail to resume when returning to character
  selection.
- Sound effects generally fare better, but occasional glitches or crackling
  remain reported.
- After a long hitch (more than about a tenth of a second) the music fades out
  briefly and comes back where it left off.
- When the game runs below about 94% speed, audio is time-stretched to match,
  which can blur the music. Keeping 60 FPS (for example a lower internal
  resolution) avoids this.

Fixed in v0.1.101: when the game ran slightly below full speed (around 57 to
59 FPS), music switched to time stretching and parts of it played a second
time with pops at the joins. Small slowdowns are now absorbed by nudging the
playback rate by up to 5%, the switch back from stretching continues from the
exact matching point, and a dropout no longer throws away the buffered music.

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
