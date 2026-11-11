# Dragon World validation

Dragon World has been beaten through all nine stages and the ending in
playtesting. Full-game completion is now the recorded gameplay status. Music
playback and mod tools remain the active issue areas.

This completion report is separate from the targeted runtime checks below.
Callback registration and component tests verify individual operations; they
are not themselves a full playthrough.

## Targeted in-game checks

The following earlier checks used the supported USA disc, Vulkan and native
internal resolution. They ran with software rendering, so their frame rate
does not measure performance on a hardware Vulkan device.

| Check | Observed result |
| --- | --- |
| Start Dragon World and select an ally | Board opens and keyboard confirmation works. |
| Move to adjacent board points | Characters reach their destinations and control returns. |
| Complete a player turn | Enemy movement completes and another player turn begins. |
| Lose a Saibaman encounter | The game returns to the board and displays the reward overlay. |
| Defeat Raditz | Combat exits, his board health reaches zero and the reward sequence opens. |
| Defeat Nappa | Combat exits, his board health reaches zero and the reward sequence opens. |
| Dialogue and ally confirmation labels | Fixed: story dialogue and the "Is this okay?" prompt show their text. |
| Clear stage 1 and enter stage 2 | Passed on v0.1.84: after both bosses were defeated, Goku moved to the Goal and the snow stage opened. |
| Complete stage 2 setup | Objective, ally selection and keyboard confirmation completed; board control returned. |
| Complete stages 2–9 and the ending | Completed in subsequent playtesting; full Dragon World completion reported. |

Combat transitions were accelerated with reduced opponent health and maintained
player health and ki. Enemy board health was also reduced for the boss checks.
Movement, battle outcome handling and scene transitions still execute the game
code. These checks do not establish that ordinary combat is free of faults.

## Regression checklist

When changing story progression, retain a checkpoint before each stage's final
enemy and verify:

- Required board events and movement paths finish and return control.
- Battles return to the correct board state after both a win and a loss.
- Rewards, dialogue and confirmation prompts remain usable.
- The stage's completion condition opens the next stage, including reaching
  the Goal where required.
- Saving, closing the game and loading restores the expected progress.

Include the ending and return to a usable menu in future progression checks.
Repeat critical transitions without combat acceleration, and check skill-shop
entry and exit, save persistence and the default rendering configuration.

## Graphics regression coverage

Depth testing now honors `TEST.ZTE`: disabling it bypasses the comparison and
suppresses depth writes even when `ZBUF.ZMSK` is clear. Known-output checks cover
512 combinations of framebuffer format, depth format, enable state, comparison,
write mask and primitive type. Both OpenGL and Vulkan comparisons pass.

The missing story text and Yes/No labels came from the VU0 MAX and MINI
instructions. The game projects 2D text with z = 0, so its depth saturates to
the VU's largest value, 0x7FFFFFFF, and is then clamped with MAX and MINI. On a
PC that bit pattern is a NaN, and SSE's max and min returned the minimum
depth, which put the text behind its own box. MAX and MINI now compare the raw
sign-magnitude bits, as the VU does.
