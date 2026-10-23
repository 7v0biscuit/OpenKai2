# Development status

retail testing reaches the title screen and New Game after movie playback, but
Preview 30 still hangs at the create-save confirmation. The window stays open
and stops responding. The latest log records about 13 movie FPS, no save write
or exception, and a normal window-close request. Gameplay and stable 60 FPS
remain unverified on the reported supported CPU, supported GPU, and system memory system.

The original Budokai 2 USA ISO is required at every launch. A complete retail
ISO is not present on the development host, so automated component tests and
observed retail results are distinguished below.

## Current changes

Presentation runs on a worker so the window thread can poll keyboard and
controller input while rendering is busy. Requests coalesce to the latest field,
and shutdown joins the worker. This changes host presentation scheduling;
it does not accelerate the guest clock or establish 60 game FPS.

Bilinear RGBA interpolation uses SSE4.1 with scalar-compatible rounding. When a
CT32 draw cannot write its texture pages, sampling avoids repeated page copies
while preserving initial stale bytes, eviction order, and the final cache page.
Feedback and unsupported modes retain the ordinary path. In paired CPU tests,
a 512x448 fractional bilinear draw takes 11.04 ms instead of 15.85 ms, and the
aligned case takes 5.69 ms instead of 7.68 ms. These are isolated drawing costs,
not retail frame rates. PS2 drawing still uses a CPU software backend.

Input samples remain on the window thread and short taps survive until a game
read. Keyboard input remains active when a controller is connected. Bounded logs
track observed and delivered buttons, original save states, presentation work,
and the source revision. Window frames, presentation completions, and guest
fields are recorded separately.

## Validation

The runtime self-test covers original translated game callbacks as well as
synthetic disc, MPEG, graphics, scheduler, sound, input, and save fixtures.
The save regression starts with the original 21-sample card check and missing
directory search, then runs directory-creation and file-writing
callback chain, verifies bytes on disk, restarts the memory-card SDK, and reads
the payload back into guarded EE memory. It uses an isolated temporary card;
it does not access the user's saves or bypass the in-game confirmation.

Renderer comparisons use complete VRAM snapshots from the previous scalar
backend across 64 textured and solid sprite cases. They cover filtering,
blending, alpha tests, depth, masks, fog, fractional coordinates, cache-page
boundaries, wrapping, and framebuffer/texture feedback. Input checks include a released Cross tap through the compiled pad-read entry,
its guest packet and memory bounds. Additional checks cover a
press released between guest reads, held input, releases, simultaneous pending
presses, analog state, reset, and independent ports.

The presentation regression deliberately blocks rendering while a thousand
requests continue to arrive, checks coalescing to the latest field, and verifies
shutdown and error reporting. Bilinear checks compare 8,192 samples against scalar
rounding and retain complete VRAM comparisons against the preceding renderer.

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
