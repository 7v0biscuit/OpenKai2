# Windows build and startup

OpenKai2 is in development. The runtime reaches menus, versus fights and opening
story sequences. Progression, performance and complete matches need validation.

## Launcher preferences

The launcher has four pages: **Play**, **Saves**, **Mods** and **Settings**
(Ctrl+1 to Ctrl+4). **Settings** groups display mode, resolution, aspect ratio,
frame rate cap and VSync; renderer, FXAA, texture filtering and upscaling; and
update preferences. Lists support mouse and keyboard selection. Changes save
automatically and apply to the next game session; an already running game keeps
its launch settings. Borderless mode uses the desktop resolution. **Restore
defaults** resets only display and graphics settings, preserving the ISO, saves
and update preferences.

The **Saves** page contains automatic session backups, imports and recovery.
**Unlock all** unlocks characters, stages and all 404 supported capsules in an existing USA progress
save, filling skill quantities to their supported capacity. It checks both save checksums and creates a backup before replacing the
card. Story progress, equipped skills, preferences and other games' files are preserved. Existing quantities above the normal capacity are retained.
Restore the backup from **Backup history** to undo the change. Create a save
in the game first; the tool does not manufacture a new game save.

The **Mods** page lists installed mods in load order and opens the import
tool, which copies characters, movesets and stages from a PS2 ISO or your own
files into a mod. See [Modding](modding.md).

**Discord activity** in the sidebar controls Rich Presence for the launcher and
game, including a running session. It uses the Discord desktop client and the
same application icon. See [Discord activity](discord.md) for art-asset setup.

Update checks run independently of ISO selection and settings. Installation
waits until the game and save operations finish.

**AMD FSR 1 · Spatial** requires OpenGL 4.3 and uses EASU upscaling followed by RCAS sharpening. It scales
the completed native-resolution image to the window, including the game's UI;
it does not reduce guest execution cost or generate extra game frames. Smaller
output sizes use bicubic filtering. FSR 3/4 temporal reconstruction and frame
generation require renderer inputs and backend support that are not available.

## Launcher updates

The **Updates** section of **Settings** checks releases from `7v0biscuit/OpenKai2`
on GitHub. Automatic updates are enabled by default on the **Stable and
prerelease** channel, so published development builds are included. Choose
**Stable** to exclude prereleases, or turn automatic updates off and use
**Check now** / **Install**.
Checks do not require a GitHub account or token. A connection failure leaves the
existing application available.

Downloads use HTTPS and the release's `SHA256SUMS.txt`. Extracted and installed
copies also validate every ZIP payload against its release manifest. The local
update worker waits for the launcher, game and save operations to finish, replaces
application files with recovery copies, and restarts the launcher. The single-file
portable edition replaces its outer executable after its wrapper exits. A
replacement error restores files already replaced. Keep application files
in a writable folder; Windows PowerShell 5.1 is required to apply updates.

Settings, ISO preferences, saves, backups, and cached game files remain under
`%LOCALAPPDATA%\OpenKai2`. Update reports are retained under its `updates`
subfolder in `apply.log`. No game files are downloaded by the updater.

## Downloads

Get the Windows development build from
[Releases](https://github.com/7v0biscuit/OpenKai2/releases).

- `OpenKai2.exe` is a self-contained portable launcher. It temporarily extracts
  its runtime, opens the desktop launcher, and removes temporary files after the launcher and game exit.
- `OpenKai2-windows-x64.zip` can be extracted to a folder of your choice. Keep
  its DLLs beside the included application EXE.
- `OpenKai2-Setup.exe` installs for the current Windows user and adds Start-menu
  and uninstall entries. It does not require administrator access. Uninstalling
  preserves the disc cache, logs, and unrelated files in the installation folder.

The downloads need Windows 10/11 x64, an SSE4.1-capable CPU, and your original
USA ISO. GPU rendering requires an OpenGL 4.3 driver. Python and Visual Studio are needed only for building from source.
Development downloads are unsigned. Verify their SHA-256 values against the
release's `SHA256SUMS.txt`.

## Loading times

Disc reads follow the timing of a PS2 DVD drive: 30 ms short seeks, 100 ms full
seeks and a constant-angular-velocity transfer rate from about 1.65x DVD speed at
the hub to 4x at the edge of the layer. The time is measured in emulated IOP
cycles, so loads take as many game frames as on a console regardless of host
disk speed, which keeps load-inclusive runs comparable. The first read logs
`[cdvd:timing] mode=ps2`. Developers can set `OPENKAI2_DISC_TIMING=instant` to
restore immediate reads.

## Controls and saving

Use arrows or WASD to move, **X or Space for Cross/confirm**, C for Circle/cancel,
and Enter for Start. A compatible gamepad uses its lower face button for Cross.
Enter does not replace Cross in confirmation dialogs. Short taps are retained
until the game polls input, including when game frames are slow. Keyboard input
remains available when a controller is connected.

Memory-card files are stored in the disc cache's `mc0` directory. Preserve this
directory when troubleshooting; the runtime self-test uses a separate temporary
card. The `[startup:save]` log record shows the active save root and last command.
Cumulative `[runtime:work]` log records separate guest execution, DMA, and
vector-unit work. `path1-submit` measures graphics submission inside vector
execution. These are inclusive host timings, not a game-FPS counter; nested
stages must not be added together.
OpenKai2 selects the OpenGL 4.3 compute renderer automatically when available.
It rasterizes game primitives on the GPU, with a software compatibility path
for unsupported or overlapping buffer states. Stable 60 game FPS remains
unverified. See [GPU rendering](graphics.md) for renderer selection, requirements,
and the scope of validation.

## Desktop launcher

Double-click any Windows package to open the launcher. **Choose ISO** opens the
Windows file picker. Verification runs in the background; **Play** becomes
available after the supported executable and every disc file have been checked.
The verified ISO path is remembered for the next session. The ISO remains
required, including when a verified cache already exists.

The launcher uses owner-drawn Windows controls, Segoe UI and a dark navy
palette with orange and gold accents.

**Settings** saves the renderer, frame-rate limit, window resolution,
windowed/borderless mode, aspect ratio, VSync, FXAA, texture filtering, and spatial upscaling.
Texture filtering can retain the game's choice or force nearest/bilinear
sampling. Display scaling supports nearest, bilinear, and bicubic sampling;
FXAA runs before enlargement. Changes apply to the next game launch.
The default 4:3 aspect restores the original television proportions; 16:9,
stretch, and native-pixel modes are also available. These spatial filters do not
create additional game frames. FSR and frame
generation are not currently included.

Preferences are stored in `%LOCALAPPDATA%\OpenKai2\settings.ini`. They are
shared by the installed and portable packages. Cancelling ISO selection leaves
the current selection unchanged. Verification errors appear in the launcher;
launcher activity is recorded separately in `logs\launcher.log`.

## Save import and backups

The **Saves** page manages slot 1 for the verified ISO. Import an extracted
memory-card folder, a single game-save folder containing `icon.sys`, a PSU
save, or a ZIP backup created by OpenKai2. Raw `.ps2` memory-card images and
compressed ZIPs must first be exported or extracted to a supported format.

Imports merge save files and preserve other games' folders. Restoring a backup
replaces the active card with that snapshot. Both operations validate the
entire source and back up the current card before replacement. Damaged
archives, unsafe paths, conflicting names, and links are rejected. Save tools
are locked while a game is running.

Use the list selector to switch between **Backups** and **Game saves**. Click a
backup and choose **Restore backup**, or double-click it. Confirming restores
that snapshot and first retains a recovery copy of the current card. Load the
restored save from the game's title menu to use its progress.

**Select all** selects the visible list; click it again to clear the selection.
Ctrl-click or Shift-click selects several rows. **Delete** shows the selection
count and asks for confirmation. Game-save deletion first backs up the card;
backup deletion permanently removes only the selected archives.

**Back up now** creates a ZIP snapshot and verifies the written archive. Automatic session backups are enabled
by default and run before launch and after the game closes. Imports and
restores always retain a recovery backup, regardless of this preference.
Backups live under `%LOCALAPPDATA%\OpenKai2\backups\<ISO hash>`; use
**Open folder** in Backups view to copy or archive them elsewhere. Existing backups are not
automatically deleted. Uninstalling preserves saves, backups, and preferences.

## Display frame limit

The default limit is **60 FPS**. Choose another limit in Settings, including
240 or unlimited, or use the direct game command:

```powershell
.\OpenKai2.exe --fps-limit=60 "D:\Games\Budokai 2 USA.iso"
.\OpenKai2.exe --fps-limit=0 "D:\Games\Budokai 2 USA.iso"
```

A value of `0` removes the host display cap; integers from 1 through 240 select
a limit. For direct game launches, command-line values override
`OPENKAI2_FPS_LIMIT`, followed by saved preferences. The desktop launcher passes
its saved selection explicitly. `[display]` records the effective setting.

Original game timing is independent of the window's refresh cap. After a long
host stall, pacing resumes from the current time instead of replaying a backlog
of vertical blanks in a burst. Guest cycle deadlines and interrupt order are
preserved. Repeated window updates are not new game frames; higher caps do not
establish higher simulation FPS or resolve slow fights.

## Vector execution

VU1 microprograms use PCSX2's microVU native recompiler on x64. Startup records
`[vector:engine] backend=pcsx2-microvu`. If executable memory is unavailable,
the runtime selects its previous interpreter before vector execution starts.
This changes CPU execution cost while retaining the original game clocks.
Character distortion and stable 60 FPS remain under investigation.
See [Vector execution](vector-runtime.md) for validation and developer settings.

## Audio output

Game audio is synthesized by PCSX2's SPU2 core from the same register writes
and transfers the console's sound chip receives: music, voices, sound effects
and reverb. When the game runs below full speed, audio is time-stretched
with SoundTouch's stereo, pitch-preserving processing. At full speed the PCM
passes through unchanged, apart from volume, a short start/stop fade and the
mixer's existing peak limiter. SoundTouch is built from the version pinned by
PCSX2 v2.1.27. Settings → Display → Game audio turns it off or sets
the volume (100, 75, 50 or 25%). The startup log reports `[audio:output]
enabled=1` with the stream format, or the reason output is off, and
`[audio:output] stopped` with underrun counts. Per-core `[audio:core]` records
include submitted frames, stop requests, queued frames and playback tempo to
help distinguish game-side interruptions from device starvation. `--audio-test` checks the mixer
without opening a playback device.

## Build requirements

- Windows 10/11, x64, with an SSE4.1-capable CPU.
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**.
- Python 3.10+, Git, and CMake 3.21+ on PATH.
- Your original **Budokai 2 USA ISO**. Game data is not distributed with OpenKai2.

Extract a compressed disc archive to a `.iso` file before building or launching.

The first build downloads PS2Recomp and development dependencies. It generates
and compiles game code locally. Use `--jobs 2` initially to limit compiler memory.

## Build

From the repository root in PowerShell:

```powershell
python tools/native/build-windows.py --iso "D:\Games\Budokai 2 USA.iso" --jobs 2
```

The builder verifies the disc before translating its executable. The supported
executable SHA-256 is:

```text
431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898
```

PS2Recomp is pinned to `eb8688b2fd191129f103a39543002c005b33409c`.
Build output and runtime DLLs are staged under
`.local/openkai2/budokai2-native/`. Set `OPENKAI2_WORK_DIR` or pass `--work-dir`
to keep development files elsewhere; set the same environment variable when
using the startup diagnostic.

## Launch

```powershell
& ".\.local\openkai2\budokai2-native\OpenKai2.exe" "D:\Games\Budokai 2 USA.iso"
```

Double-click `OpenKai2.exe` to use the desktop launcher. Choose and verify the
ISO, then press **Play**. The launcher returns when the game closes. Game output
is saved to `%LOCALAPPDATA%\OpenKai2\logs\startup.log`.

The Windows builder also creates `OpenKai2-windows-x64.zip` in its working
directory. Extract the whole archive before launching. It contains the executable,
its required DLLs, license notices, and a SHA-256 manifest. Python and the build
tools are not needed to launch that bundle. The packager checks both direct and
transitive DLL imports and refuses to package an incomplete dependency set.

The ISO is required at every boot. Passing an extracted ELF or removing the ISO
while retaining the cache does not satisfy startup requirements.

Before initializing the runtime, OpenKai2 validates ISO9660 directory records,
checks the USA executable identity, hashes every file, and verifies each cached
copy against the ISO. Missing or altered cache files are refreshed from the
image. Multi-extent files are reconstructed in order. Runtime disc searches
retain the original ISO sector addresses, and raw sector reads use that ISO. Unsupported layouts,
unsafe paths, incomplete discs, and conflicting filenames stop startup.

The extracted disc cache is stored under `%LOCALAPPDATA%\OpenKai2\discs`,
keyed by the full ISO SHA-256. Allow space for the extracted disc contents.
Every launch rereads the ISO and cached files, so verification time depends on
disc size and storage speed.

If verification was forcibly terminated and the next launch reports that the
cache is in use, close all OpenKai2 instances before removing the
`.openkai2-lock` directory inside the affected ISO hash directory.

Executable identity is checked against the known USA SHA-256. The remaining
file hashes establish consistency with your supplied ISO; a separate trusted
hash for the entire retail disc is not currently pinned.

## Startup diagnostics

Runtime integration can be checked without booting the game:

```powershell
& ".\.local\openkai2\budokai2-native\OpenKai2.exe" --self-test
```

This checks IOP import arguments, module-worker startup, sleep and wake
deadlines, SPU2 initialization, read-only wait diagnostics, and an isolated
compiled callback's stores and return. It also checks subsystem method-table
coverage, original leaf returns, file-driver queries and size arithmetic, and
their compiled callers. Scratchpad DMA checks verify received bytes, wraparound,
cached and uncached addresses, completion registers, and the original compiled
copy caller. The original ISO remains required for
game startup.

Normal launches retain EE wait-state and graphics-activity reports in
`startup.log` at 5, 15, 30, and 60 seconds, and on shutdown. If the window remains
blank, leave it open for at least 60 seconds before closing it and retaining the
log. Reports are bounded and do not release waits or change guest state. The log
starts with the source revision. Save-wait reports retain observed/delivered
buttons and the original card command state, including samples after the movie.
Adjacent `[startup:frame-wait]` records include the original frame flags,
countdown, DMA queue counts, active commands, and channel ownership. Empty
graphics transfers are checked through the original game's queue and VBlank
callback as well as direct device tests; invalid empty chains must stay busy.
After five seconds without a game-side controller read, `[startup:stall]` and
`[startup:save-menu]` report the captured guest PC, stack, menu callback, fade
state, and queues. `snapshot_age_ms` distinguishes a stopped executor from a
continuing guest wait. `parallel_draws` records completed parallel sprite and triangle batches.
`[GS:timing]` records aggregate triangle, sprite, and upload work; triangle
`bounds_pixels` counts the bounding boxes tested, including uncovered pixels.
These reports also appear before shutdown joins the game thread, so a blocked
shutdown can retain its last known continuation. Leave a frozen screen open
for at least ten seconds before closing the window and retaining `startup.log`.
At shutdown, presentation metrics distinguish window frames, completed renders,
and guest fields; window refresh rate alone is not game FPS.
Graphics records include frame dimensions, source framebuffer, nonblack pixel
count, RGB hash, drawing state, and recent draw/transfer events. Movie-wait
records include the original player state and display registers, sampled on the
EE executor. A zero nonblack count means a black rendered frame when `frame=1`;
`frame=0` means no host presentation frame is available.
Movie activation and backend-state changes are recorded immediately, with
follow-up samples timed from activation. Those records include the live player,
filename, SDK error, and scene callback, with at most 12 movie reports per run.
The self-test also checks the original MPEG SDK work area, callbacks, native
dispatcher, reset, and reference buffers with the compiled Sofdec no-data callback.

The self-test also checks callback stack separation, nested allocations, and
resumption of an interrupted caller through the EE scheduler using an original
translated callback.

It also verifies the SDK bootstrap-priority transition and runs the game's
translated initialization to create four EE workers and reach their sleep points.
The SJRBF stream checks execute all nine interface methods and the original
cleanup caller, including buffer recycling, wraparound, and error callbacks.
The sound-transfer checks execute the original `sceSdTransToIOP` routine,
verify bytes in IOP RAM, and resume the caller reported in the startup log.
Asset relocation checks execute all seven tagged handlers through the original
dispatcher, including nested assets and protection against repeated relocation.
Fade checks execute the logged update caller and original renderers, inspect
overlay command packets, and verify completion callbacks and saved registers.
Movie-interface checks exercise the logged volume call, original ADX volume and
pan adapters, channel selection, clamping, status queries, and memory bounds.

```powershell
python tools/native/diagnose-startup.py "D:\Games\Budokai 2 USA.iso" --timeout 180
```

The diagnostic writes `startup-status.json` and `startup-verified.log` under
`.local/openkai2/budokai2-native/`. Disc verification, ELF loading, and graphics
initialization are reported separately. A timeout or missing-module failure
stops the run; it does not count as a verified game boot.

The build also runs the ISO parser tests, an EE/IOP original-sector regression,
and four upstream IOP test groups. CI builds these tools and tests without game
data using `--tools-only`; this mode does not produce a translated game binary.

Windows CI builds the development tools and runtime integration tests with
Visual Studio 2022. The translated game executable is cross-built in the Linux
development environment and checked under Wine. Real Windows game startup with
the complete ISO still requires verification.

The fallback translation still reports unresolved instructions. Analyzer and
recompiler logs remain in the native working directory. See
[development status](status.md) for the current evidence and limitations.

`[startup:dma-queue]` and `[startup:dma-channel]` retain the executor's DMA
registers, tag words, queue entries, and interrupt enables at a stall.
`[GS:sprite]` identifies large-draw formats, depth state, and worker eligibility.
The latest retail log shows an existing save loaded successfully, followed by
a missing draw-setup callback. Preserve the existing `mc0` files.
