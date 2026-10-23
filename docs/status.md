# Development status

Preview 34's retail log gets past the former graphics DMA wait. It records
9,351 completed parallel draws, empty graphics queues, and a successful read
and close of the existing 2,736-byte save. The next stop is an unregistered
render-setup callback at `0x001A9380`, called from `0x0013A20C`. The main guest
thread becomes dormant while the window remains open. Full gameplay and
stable 60 FPS remain unverified.

The movie decoder returned 240 pictures in about 13.74 seconds in this run.
This is about 17.5 returned pictures per second, not a measurement of displayed
frames. Rendering and overall game speed still need improvement on the
reported supported CPU, supported GPU, and system memory system.

The original Budokai 2 USA ISO is required at every launch. A complete retail
ISO is not present on the development host, so automated component tests and
observed retail results are distinguished below.

## Current changes

The original depth initializer at `0x001A9380` is now an explicit translation
entry, along with seven related render setup callbacks. ELF validation checks
their original instructions, including return delay slots. Runtime comparisons
exercise their exact packet/global writes, both direct RAM and the logged
uncached RAM alias, surrounding guards, saved registers, and return paths.
The original selector and packet builder's indirect call are tested together.
The callback performs its original depth write; it is not replaced by a no-op.
Retail progress beyond this callback still needs confirmation.

Maskable interrupt callbacks now respect the guest COP0 IE/EIE enables and
EXL/ERL exception state. Automatic thread preemption also waits until interrupts
are enabled. The original DMA queue submission test reproduces an interrupt
running inside its DI/EI section before this change; it now verifies deferred
delivery, completion order, and released queue ownership. The following retail
log has empty DMA queues and reaches the later draw
setup callback; further scene initialization remains unverified.

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
