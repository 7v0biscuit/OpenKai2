# Development status

retail testing reaches the title screen and New Game after movie playback, but
Preview 32 still freezes at the create-save confirmation and runs too slowly.
The latest log shows empty DMA queues at the recorded card checks and ends
during the second stability check, without a stopped-state or shutdown report.
The user closed the window normally. This log does not identify the blocked
callback. Gameplay and stable 60 FPS remain unverified on the reported
supported CPU, supported GPU, and system memory system.

The original Budokai 2 USA ISO is required at every launch. A complete retail
ISO is not present on the development host, so automated component tests and
observed retail results are distinguished below.

## Current changes

Large eligible sprites draw across up to four persistent CPU workers. The
renderer proves that the texture is immutable and framebuffer pixels are
independent, uses a separate sampler cache per worker, and preserves the final
logical row's cache. Feedback, depth reads/writes, stale sampled pages, and
unsupported modes retain serial order. Worker exceptions join the complete
batch before reaching the caller.

Draws that preserve destination RGB and have constant alpha can avoid unused
texture sampling while retaining alpha writes, tests, masks, and final sampler
state. This includes some fully faded textured passes. Other blend and texture
modes retain their complete pixel calculations.

In paired 512x448 wall-time tests, filtered opaque draws average 3.96 ms instead
of 13.30 ms, blended draws 5.31 ms instead of 17.47 ms, and eligible alpha-only
passes 1.25 ms instead of 17.96 ms. These are isolated draw timings with one and
four workers, not retail FPS measurements. The renderer still uses the CPU.

A stall watchdog reports five seconds without game-side controller reads. The
EE executor captures the current PC, caller stack, save-menu callback and fade
state, card command, and DMA queues. The window thread prints this copied state,
its age, and the number of parallel draws without reading live guest RAM.
Shutdown reports are written before waiting for the game thread to finish.
These diagnostics do not release guest waits or bypass the save confirmation.

The save fixture now exercises the original menu callbacks from a completed
card check to directory search and creation. It covers repeated 21-sample card
checks through both the blocking completion loop and original frame poller.
Another fixture injects 21 completion interrupts through the EE scheduler and
checks that the original stability callback finishes with its caller frame intact.
Those component transitions pass; the retail freeze still needs diagnosis.

The earlier empty-DMA completion fix, asynchronous presentation, SSE4.1
bilinear interpolation, and latched keyboard/controller input remain enabled.
Window frames and guest frames are counted separately.

## Validation

The runtime self-test covers original translated game callbacks as well as
synthetic disc, MPEG, graphics, scheduler, sound, input, and save fixtures.
The save regression runs the original card checks and menu transitions,
then runs the file-writing callback chain, verifies bytes on disk, restarts the memory-card SDK, and reads
the payload back into guarded EE memory. It uses an isolated temporary card;
it does not access the user's saves or bypass the in-game confirmation.

Parallel rendering is compared with the serial path across 24 full-VRAM
fixtures, including stale caches, dependent draws, blending, alpha-only passes,
and guarded fallbacks. The worker checks cover varying partition counts,
exception propagation, reuse, and shutdown.

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

Locate the remaining retail save-confirmation stall using the captured guest
continuation, verify saving with the original ISO, measure the renderer changes
in retail runs, and continue through gameplay. Physical audio output,
graphics accuracy, controller compatibility, and interrupt timing also need
further validation. The stripped-executable analyzer still identifies some
data and VU regions as EE code, so additional function-boundary corrections
may be required as execution reaches new game paths.

See [Windows build and startup](windows.md) for controls, requirements, and logs,
and [Windows release packaging](releases.md) for package verification.
