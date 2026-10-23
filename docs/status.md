# Development status

retail testing on Preview 33 reaches the title and New Game, writes the save
files, then freezes while waiting for the graphics DMA queue. The log records
successful writes and file closes; afterward, queue zero remains active with
two pending entries, and game-side input reads stop. Movies remain very slow
and menus run at a reported 2–5 FPS on an supported CPU, supported GPU, and
system memory system. No parallel drawing batches ran in that retail session.
Gameplay and stable 60 FPS remain unverified.

The original Budokai 2 USA ISO is required at every launch. A complete retail
ISO is not present on the development host, so automated component tests and
observed retail results are distinguished below.

## Current changes

Maskable interrupt callbacks now respect the guest COP0 IE/EIE enables and
EXL/ERL exception state. Automatic thread preemption also waits until interrupts
are enabled. The original DMA queue submission test reproduces an interrupt
running inside its DI/EI section before this change; it now verifies deferred
delivery, completion order, and released queue ownership. This fixes a
scheduler defect, but does not yet prove the retail post-save freeze is fixed.

Sprite workers now support indexed and 16/24-bit textures, unaligned texture
bases, region wrapping, independent depth buffers, and additional framebuffer
formats. Conservative VRAM bounds exclude feedback and cross-worker color/depth
dependencies. A maximum unsigned depth value with a greater-or-equal test needs
no depth read. Each worker retains a separate sampler cache, and the last
logical row determines the final cached page. Stale sampled pages retain serial
order. Constant-alpha passes that preserve destination RGB can avoid unused
texture sampling even when depth dependencies require serial drawing.

Isolated 512x448 comparisons of the previous and updated renderer measured
roughly 14–17 ms versus 3–5 ms per draw across newly supported cases, with
identical complete VRAM hashes. These timings do not establish retail FPS.
The renderer continues to use the CPU.

Bounded draw-profile records identify formats, depth state, and worker eligibility
for large sprites. Stall records now include copied DMA registers, current tags,
queue entries, owners, and interrupt state. Copies are made on the EE executor;
the window thread prints the copy without reading live guest RAM. Reports do
not release waits or bypass save confirmation.

The earlier empty-DMA completion fix, asynchronous presentation, SSE4.1 bilinear
interpolation, latched keyboard/controller input, original save-menu callbacks,
and persistent save readback checks remain enabled.

## Validation

The runtime self-test covers original translated game callbacks as well as
synthetic disc, MPEG, graphics, scheduler, sound, input, and save fixtures.
The save regression runs the original card checks and menu transitions,
then runs the file-writing callback chain, verifies bytes on disk, restarts the memory-card SDK, and reads
the payload back into guarded EE memory. It uses an isolated temporary card;
it does not access the user's saves or bypass the in-game confirmation.

Parallel rendering is compared with the serial path across 58 full-VRAM
fixtures, including indexed palettes, depth writes, stale caches, dependent draws, blending, alpha-only passes,
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
