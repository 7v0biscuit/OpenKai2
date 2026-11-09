# Save states

Save states store the whole console: the EE, IOP, GS and video memory, VU0 and
VU1, sound, and timers. Loading one puts the game back exactly where it was.

| Key | Action |
|---|---|
| F5 | Save to slot 1 |
| F9 | Load slot 1 |

A short message shows when the state is saved or loaded.

**Load state** on the launcher's Play page starts the game and loads slot 1 as
soon as the game is running. If the state cannot load yet (for example while
the opening movie plays), it tries again every second for up to 30 seconds.

States are taken between frames, when every game thread is waiting. When you
press a key, the state is saved or loaded within a frame or two.

States are saved to `%LOCALAPPDATA%\OpenKai2\states\`. A state only loads in the
same OpenKai2 build with the same disc. A state from another build or another
disc is refused, and the game keeps running.

If a state cannot be loaded, the game goes back to where it was before the load.

Save states are not available while a movie plays.

Save states are off in speedrun mode.
