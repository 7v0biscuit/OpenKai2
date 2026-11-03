# Speedrun mode

Speedrun mode times Dragon World runs under the
[speedrun.com rules for Budokai 2](https://www.speedrun.com/dbzb2) and keeps a
log that moderators can check. The game itself runs unchanged: same frame rate,
same disc read times as a PS2. Only the picture is upscaled, and only the CPU
changes when a Z difficulty is chosen.

Turn it on in the launcher's **Speedrun** page (Ctrl+3):

| Setting | Choices |
|---|---|
| Speedrun mode | Off, Any%, NG+ |
| Permadeath | Being knocked out in a Dragon World fight, or losing one on time, ends the run. Loading Dragon World, changing maps, story scenes, fights the story ends on its own and having less health than the CPU during a fight never count. **GAME OVER** shows, then the game resets to the main menu and the timer resets |
| FPS counter | Frame counter in the top corner, also outside speedrun mode |
| CPU difficulty | Game setting, Z1, Z2, Z3 ([CPU difficulty](cpu-difficulty.md)). Each Z level is its own category with its own splits, shown on the overlay and in the run log (for example "Any% Z3"). It is locked for the run: F6 is off in speedrun mode. The same setting is on the Settings page |

While speedrun mode is on, mods are not loaded.

## Timing

- The timer starts when Dragon World starts (its opening text begins).
- It stops on the last hit of Kid Buu: the end of the last battle won before the
  staff credits.
- Starting Dragon World again restarts the timer.
- Real time is the leaderboard time.
- PS2 time counts the console's own video fields (59.94 a second), so slowdown
  on a weak PC does not add to it.

Any% starts with default equipment. NG+ allows skills set up before Dragon World.

## Overlay

**Customize overlay…** on the Speedrun page opens the overlay page. Pick the
items to show, the corner, the size, the background and how many splits are
listed, the colors of the text, of times ahead and behind and of best segments,
and whether times show hundredths, tenths or whole seconds. The preview updates
as you change them when the window is tall enough to show it.

Each battle won is a split, and the final split is Kid Buu. The overlay
compares the run with your best run and shows:

| Item | Shows |
|---|---|
| Run timer | Green when ahead of your best run, red when behind, gold for a new personal best |
| Splits | Each battle won, the difference to your best run (gold for a best ever segment) and the time |
| Segment timer | Time since the last battle won |
| Previous segment | The last segment against the same segment of your best run |
| Possible time save | Your best run's current segment against your best ever segment |
| Best possible time | Where the run ends if every segment left is your best |
| Sum of best | Your best segments added together |
| Personal best | Your fastest finished run |
| PS2 time | Time counted from the console's frames |
| Attempts | Runs started, and how many finished |
| Run checks | Category, permadeath, fights lost (without permadeath), and Verified, Flagged or Run over. Always shown |

Run checks can't be turned off while speedrun mode is on, so every recording
shows whether the run is verified. In game, **F7** hides the rest of the
overlay and **F8** resets: the game restarts and the attempt is saved.

## Splits

After every run (finished, reset, game over or game closed), the splits are
saved to `%LOCALAPPDATA%\OpenKai2\splits\` in one file per category, with and
without permadeath:

- your best run's split times
- your best segment for each split
- a line for every attempt

The **Splits** panel on the Speedrun page shows the best run of the category,
difficulty and permadeath setting that are selected, in columns: split, time,
segment and best segment. Segments that match the best segment are gold. Below
the list are the best run, the sum of best segments and the possible time
save; the title shows attempts and finished runs.

Pick a split to change its time (type it like `12:34.56` or `1:02:03.5`), add a
split at the end, remove one, or reset the best run. Attempt history is kept.
**All rules…** shows the full timing rules.

## LiveSplit

When LiveSplit's server is running (port 16834), OpenKai2:

- starts the LiveSplit timer when the run starts;
- sends PS2 time as game time;
- splits at the end;
- resets on a restart.

## Run logs for moderators

Each session writes `%LOCALAPPDATA%\OpenKai2\logs\speedruns\run-<date>.log`.
The log is separate from the splits files. It records:

- the OpenKai2 version and source revision, and the SHA-256 of the OpenKai2
  executable, the ISO and the game executable;
- whether the game data matches the US retail disc;
- the category, permadeath and display settings;
- every run start, battle won (real and PS2 time), fight lost (map health
  lost), game over, reset and finish.

Each line ends with a checksum chained to the line before it, so edited or
removed lines show up. It is a checksum, not a signature.

A run is **Flagged** if any of these happens during the session:

- the game data differs from the US retail disc;
- the game runs faster than a PS2;
- the system clock and the game clock run at different speeds (speed hacks);
- a debugger attaches;
- a memory editor or speed tool is running (Cheat Engine, ArtMoney, x64dbg and
  similar).

The reason is written to the log.
