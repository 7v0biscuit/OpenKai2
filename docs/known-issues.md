# Known issues

OpenKai2 is in active development. The intro, menus and fights run, but full
Dragon World completion, animation correctness and consistent 60 FPS are not
yet verified. A successful component test does not establish that a gameplay
problem is fixed.

## Gameplay and graphics

| Issue | Current status |
| --- | --- |
| Dragon World does not advance after reaching the Goal following the Raditz and Nappa fights | Reported on v0.1.83. Boss victories return to the board, but the Goal, save and next-stage transition still need validation and fixes. |
| Missing Dragon World dialogue and ally Yes/No labels | Reproduced. Boxes and portraits appear while text is missing, making prompts difficult to use. |
| Distorted limbs, moving hair and accessories | Still reported. Earlier vector and animation corrections do not establish correct poses throughout the game. |
| Slow character selection, stage selection and fights; uneven frame pacing | Still reported. Sustained 60 FPS is not established. A higher FPS cap does not guarantee a higher game frame rate. |
| Crashes around melee clashes or special attacks | Earlier reports require retesting on the current release. These paths are not yet considered fully validated. |
| Imported Budokai 3 characters render black or blue | Unresolved. |
| Imported Budokai 3 stages lack particle effects | Unresolved. |

The [Dragon World validation record](dragon-world-validation.md) separates
completed checks from the remaining stage-completion work.

## Audio

Background music remains a known problem:

- Music can sound thin, flat or distorted.
- Tracks may stop after a fight or fail to resume when returning to character
  selection.
- Sound effects generally fare better, but occasional glitches or crackling
  remain reported.

The causes of the quality problems and track dropouts are still under
investigation. Mixer and audio-buffer tests passing does not establish correct
music playback through scene transitions.

As a temporary workaround, set **Music** to **Off** in the launcher. This
silences music tracks while retaining voices, sound effects and video audio.
It does not repair music playback.

## Reporting a problem

Attach `startup.log` from `%LOCALAPPDATA%\OpenKai2\logs` and describe the mode,
the action immediately before the problem, and whether the window closes or
remains open. For music problems, include where playback stopped and whether
voices and sound effects continued. Include `launcher.log` for launcher issues.

Report problems through [GitHub Issues](https://github.com/7v0biscuit/OpenKai2/issues/new/choose).
