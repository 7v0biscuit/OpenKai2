# Dragon World validation

Full completion is not yet verified. Callback registration and component tests
cover individual operations; they do not replace a playthrough through all nine
stages and the ending.

## Current in-game checks

The following checks use the supported USA disc, Vulkan and native internal
resolution. The test environment uses software rendering, so its frame rate
does not measure performance on a hardware Vulkan device.

| Check | Observed result |
| --- | --- |
| Start Dragon World and select an ally | Board opens and keyboard confirmation works. |
| Move to adjacent board points | Characters reach their destinations and control returns. |
| Complete a player turn | Enemy movement completes and another player turn begins. |
| Lose a Saibaman encounter | The game returns to the board and displays the reward overlay. |
| Defeat Raditz | Combat exits, his board health reaches zero and the reward sequence opens. |
| Defeat Nappa | Combat exits, his board health reaches zero and the reward sequence opens. |
| Dialogue and ally confirmation labels | Text is missing; unresolved. |
| Clear stage 1 and enter stage 2 | The Goal transition is under investigation; not yet verified. |
| Complete stages 2–9 and the ending | Not yet verified. |

Combat transitions were accelerated with reduced opponent health and maintained
player health and ki. Enemy board health was also reduced for the boss checks.
Movement, battle outcome handling and scene transitions still execute the game
code. These checks do not establish that ordinary combat is free of faults.

## Completion checklist

For each stage, retain a checkpoint before its final enemy and verify:

- Required board events and movement paths finish and return control.
- Battles return to the correct board state after both a win and a loss.
- Rewards, dialogue and confirmation prompts remain usable.
- Defeating the final enemy completes the stage and opens the next one.
- Saving, closing the game and loading restores the expected progress.

After stage 9, verify the ending and the return to a usable menu, then repeat
the critical transitions without combat acceleration. Check skill-shop entry
and exit, save persistence and the default rendering configuration separately.

## Graphics regression coverage

Depth testing now honors `TEST.ZTE`: disabling it bypasses the comparison and
suppresses depth writes even when `ZBUF.ZMSK` is clear. Known-output checks cover
512 combinations of framebuffer format, depth format, enable state, comparison,
write mask and primitive type. Both OpenGL and Vulkan comparisons pass.

This correction does not resolve the missing story text. Captured dialogue
textures contain readable glyphs, but the submitted draws fail their depth
comparison. The source of the conflicting depth state remains under investigation.
