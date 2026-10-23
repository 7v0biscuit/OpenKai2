# Development status

retail testing of Preview 29 reaches the title screen and New Game after movie
playback. The next reported stop is the confirmation asking whether to create a
save. retail testing shows about 2 FPS in this scene on an supported CPU, supported GPU,
and system memory. Gameplay and stable 60 FPS are not verified.

The original Budokai 2 USA ISO is required at every launch. A complete retail
ISO is not present on the development host, so automated component tests and
observed retail results are distinguished below.

## Current changes

- Host input is sampled on the window thread. Short button presses remain
  pending until the game reads them, even when a press and release occur
  between slow game frames. Held buttons and analog positions remain live;
  controller ports have independent state. Confirm uses Cross (`X` or Space
  on the keyboard); Enter is Start.
- Translated game code now builds with optimization enabled. One oversized
  translation remains separate and unoptimized to limit compiler resources.
- Common opaque CT32 sprite writes avoid repeated pixel-state classification.
  Exact bilinear texel centers avoid redundant reads only when all four taps
  occupy the same texture-cache page. Other modes retain the full pipeline.
- Save and controller snapshots join the bounded startup log reports, including
  the last memory-card command/result, open files, save root, pad reads, and
  button state.

The latest log records roughly 0.33 seconds of MPEG decoding and 0.12 seconds of
output copying during 41 seconds of movie playback. It also records about 127
seconds of textured sprite drawing across the 265-second run. Rendering uses a
CPU software backend; the supported GPU does not rasterize the PS2 drawing commands.

An isolated aligned 512x448 bilinear sprite benchmark improved from about
14.0 to 7.9 milliseconds of CPU time per draw on the development host. This
measures one rendering operation, not retail FPS. A stable 60 FPS result still
requires end-to-end measurement on the supported game.

## Validation

The runtime self-test covers original translated game callbacks as well as
synthetic disc, MPEG, graphics, scheduler, sound, input, and save fixtures.
The new save regression runs the original directory-creation and file-writing
callback chain, verifies bytes on disk, restarts the memory-card SDK, and reads
the payload back into guarded EE memory. It uses an isolated temporary card;
it does not access the user's saves or bypass the in-game confirmation.

Renderer comparisons use complete VRAM snapshots from the previous scalar
backend across 64 textured and solid sprite cases. They cover filtering,
blending, alpha tests, depth, masks, fog, fractional coordinates, cache-page
boundaries, wrapping, and framebuffer/texture feedback. Input checks cover a
press released between guest reads, held input, releases, simultaneous pending
presses, analog state, reset, and independent ports.

The Windows release workflow checks the portable launcher, ZIP, installer,
installed self-test, ISO rejection, and uninstall before publication. These
checks do not establish retail gameplay or frame rate.

## Remaining work

Verify the updated confirmation flow and saving with the original ISO, measure
retail frame times, and continue through gameplay. Physical audio output,
graphics accuracy, controller compatibility, and interrupt timing also need
further validation. The stripped-executable analyzer still identifies some
data and VU regions as EE code, so additional function-boundary corrections
may be required as execution reaches new game paths.

See [Windows build and startup](windows.md) for controls, requirements, and logs,
and [Windows release packaging](releases.md) for package verification.
