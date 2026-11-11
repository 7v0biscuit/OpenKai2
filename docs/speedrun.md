# Speedrun mode

Speedrun mode provides category profiles based on the
[speedrun.com rules for Budokai 2](https://www.speedrun.com/dbzb2) and keeps separate records and a checksummed session log. Dragon World timing
and World Tournament timing are automatic. Z difficulties are
local variants, separate from the published categories.

Turn it on in the launcher's **Speedrun** page (Ctrl+3):

| Setting | Choices |
|---|---|
| Speedrun mode | Off, Any%, NG+, Tournament (Novice), Tournament (Adept), Tournament (Advanced), All Dragonballs, Hercule Only, NG+ (Very Hard) |
| Permadeath | Being knocked out in a Dragon World fight, or losing one on time, ends the run. Loading Dragon World, changing maps, story scenes, fights the story ends on its own and having less health than the CPU during a fight never count. **GAME OVER** shows, then the game goes straight back to the main menu without closing, and the timer resets |
| CPU difficulty | Game setting, Z1 to Z6 ([CPU difficulty](cpu-difficulty.md)). Each Z level is its own category with its own splits, shown on the overlay and in the run log (for example "Any% Z3"). It is locked for the run: F6 is off in speedrun mode. The same setting is on the Settings page |

While speedrun mode is on, mods are not loaded.

Each launch starts a fresh attempt. Any% uses an empty temporary memory card.
NG+ uses a separate copy of your existing save's unlocks and equipment, with
unfinished Dragon World progress cleared. Your original memory card is not
changed. Quitting ends the attempt; reopening the game never resumes it.

Manual state saving and loading are disabled throughout speedrun mode, including
before the timer starts. The mode manages its own in-memory checkpoint for
automatic permadeath resets. F8 abandons the attempt and starts fresh.

**Randomizer** (on the Speedrun page and in Settings) works with every
category and with permadeath: your character changes every Dragon World and
World Tournament fight, the CPU does not. Randomizer runs are their own
categories, for example "Any% Randomizer", with their own splits.

Required setup is automatic: NG+ (Very Hard) locks the game's difficulty to
Very Hard, and Z variants lock the selected Z modifier and strongest game/COM
settings. Tournament profiles select their matching bracket. Adept, Advanced
and Hercule Only require the relevant content to be unlocked in your save.

## Timing

- The timer starts when Dragon World starts (its opening text begins).
- It stops on the last hit of Kid Buu: the end of the last battle won before the
  staff credits.
- Starting Dragon World again restarts the timer.
- Real time is the leaderboard time.
- PS2 time counts the console's own video fields (59.94 a second), so slowdown
  on a weak PC does not add to it.

The automatic timer applies to the five Dragon World profiles:

| Category | Requirements |
|---|---|
| Any% | New game file; restarting Dragon World is not allowed |
| NG+ | Existing save; pre-equipped capsules allowed |
| All Dragonballs | Collect all seven Dragon Balls before defeating Kid Buu; pre-equipped capsules allowed |
| Hercule Only | Only Hercule may win battles; every other character must have zero wins. Show the results screen in the recording |
| NG+ (Very Hard) | **Very Hard** is applied and locked automatically; pre-equipped capsules allowed. Z3 is a different modifier |

All finish at the final hit against Kid Buu in Stage 9. The profiles share the
automatic Dragon World start, battle splits and finish detection; they do not
automatically enforce equipment, Dragon Ball collection or character-win counts.
Check those objectives in your recording. Save setup and required difficulty
are applied automatically.

### World Tournament

Choose **Tournament (Novice)**, **Tournament (Adept)** or **Tournament (Advanced)**;
the matching unlocked bracket is selected in game. Timing is **automatic**:

- The timer starts when you confirm **Normal** or **Custom** for your character.
- Each match won is a split, timed at the moment the match is decided
  (**K.O.**, ring-out or **Time-Out!**). The split is confirmed when the next
  bracket appears.
- The run finishes when the champion scene appears, at the moment the final
  match was decided.
- Returning to the tier select or the main menu ends the attempt without a
  finish; a lost match ends it this way.

The overlay keeps the manual controls as an override:

- **F9:** start or restart.
- **F10:** add a split at the end of an intermediate round.
- **F11:** finish now and record the final split.
- **F8:** abandon/reset without recording a completed run.

Tournament times always show milliseconds, which the published rules require for
top-three times. Permadeath is disabled for these profiles.

The published category list and rules are linked above. OpenKai2's native runtime
is not currently a listed speedrun.com platform; a local PB or a passing runtime
check does not establish leaderboard eligibility. Check with the leaderboard's
moderators before submitting a run.

## Overlay

**Customize overlay…** opens the split editor and presentation settings.
The mouse wheel scrolls the split list under the pointer independently of the
launcher page. Choose a **Run category** directly in the editor to enable split
names and images. This is the same category used by Speedrun; choosing **Off**
disables editing. Difficulty, randomizer and permadeath still follow the Run settings.

Choose a split, edit its name and press **Save name** or Enter. **Add split row**
adds another appearance entry. **Use default** restores that row's standard name
and removes its image. These changes leave recorded split times unchanged.
Names and images are kept separately for each category, difficulty, randomizer
and permadeath setting.

Use **Choose image…** to select your own **PNG**, **JPEG** or **GIF**, or drop a
file onto the overlay page. GIFs loop in the preview and beside the split in game. Images keep their aspect ratio and transparency. The launcher copies the
selected file to `%LOCALAPPDATA%\OpenKai2\overlay\images`; removing it from a
split leaves the original file untouched. Files may be up to 16 MB; GIFs may
contain up to 300 frames. Large images are scaled for use as split icons.
There is no bundled split artwork.

**PS2 time** and the **Verified / Flagged / Run over** labels are fixed and
cannot be renamed or hidden through customization. The status reflects the
runtime's checks, not a name or image chosen in the launcher.

The presentation settings control the corner, size (up to Extra large),
background, number of visible splits, text and delta colors, precision,
distance from the edge, square or rounded corners, and an optional heading.
**Reset overlay** restores these settings while keeping custom split names
and images. The preview stays available by scrolling in smaller windows.
The FPS counter is on Settings under **Game**.

In Dragon World, each battle won is a split and the final split is Kid Buu.
In World Tournament, each match won is a split and the final split is the final match. The overlay
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
| PS2 time (fixed) | Time counted from the console's frames |
| Attempts | Runs started, and how many finished |
| Run checks | Category, permadeath, fights lost (without permadeath), and Verified, Flagged or Run over. Always shown |

PS2 time and run checks stay visible while speedrun mode is on. In game,
**F7** hides the optional overlay rows and **F8** resets: the game restarts and the attempt is saved.

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
