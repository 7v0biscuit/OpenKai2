# Development status

retail testing reaches level selection. Preview 36's retail log then stops at
an unregistered battle initializer, `0x001B2560`, called from `0x001D5778`.
Its startup table has eleven stages, ten of which lacked explicit translation
entries. The window remains open after the main guest thread becomes dormant.
Complete matches and stable 60 FPS remain unverified.

The original Budokai 2 USA ISO is required at every game launch. A complete
retail ISO is not present on the development host. Automated component checks
and observed retail progress are distinguished below.

## Current changes

All eleven battle startup stages are explicit translation entries. Validation
checks their original table, the first initializer's complete instructions,
and exact generated resume points. The regression executes the original
state copy/clear and logged indirect caller, checking asset argument order,
return guards, and caller frames. Asset operations are bounded in the fixture;
a complete retail match still needs confirmation.

SPU2 AutoDMA PCM writes now reach a 48 kHz stereo host stream through raylib's
miniaudio backend. The Windows build uses the system's playback APIs; no kernel
driver or additional driver installer is included. Ordinary voice/sample DMA
is excluded because it can contain compressed data. Partial planar blocks,
both cores, resets, stream stops, and invalid source ranges are handled.
The device callback mixes bounded queues without allocating or reading guest
memory. Full queues discard new frames, and stopping a stream retains samples
submitted after the stop boundary.

PCM AutoDMA completion now uses 768 IOP cycles per stereo frame, corresponding
to 36.864 MHz and 48 kHz. A 1,024-byte transfer takes 196,608 cycles instead of
the 512-cycle ordinary-transfer delay. This avoids repeatedly waking the audio
driver much faster than its playback rate. Ordinary voice DMA retains its
previous transfer timing. Guest EE, timer, and VBlank rates are unchanged.

IOP import decoding caches validated table results. RAM page generations
invalidate cached entries after code/table edits, module replacement, and reset;
stack writes to unrelated pages retain cache hits. An isolated benchmark of
100,000 repeated imports measured a median 38.59 ms before and 1.43 ms after,
with identical results. This is a lookup benchmark, not a game-FPS measurement.

Triangle rasterization tests four pixel positions at once with SSE, retaining
scalar interpolation order and shading covered pixels in original order.
Fog interpolation is skipped when fog is disabled. Forty-eight prior-renderer
VRAM fixtures still match in serial and parallel, including feedback, stale
pages, clipping, empty partitions, depth, and dependent texture samples.
The small-triangle benchmark improved from about 29.64 to 27.86 microseconds
per draw on the development host. Renderer, import-cache, and audio timing
improvements do not establish constant 60 FPS in the game.

## Validation

The Python suite has 161 tests. Linux runtime checks include original game
callbacks, both SPU2 DMA cores, signed PCM channel order, partial blocks,
48 kHz completion timing, stop/reset boundaries, queue overflow, 200,000-frame
concurrent delivery, and import-cache invalidation. Existing checks retain
12,288 scalar blend comparisons, 48 serial/parallel triangle snapshots,
64 sprite comparisons, 64 scalar-reference sprite snapshots, and 8,192 scalar
bilinear samples. Movie, DMA, scheduler, input, and persistent-save checks
remain enabled and use isolated fixtures.

The audio-device diagnostic delivered 48,000 stereo frames through the Linux
callback using an ALSA null output. Wine has no usable playback endpoint on
the development host. Audible Windows playback, core volume/effect accuracy,
voice synthesis, and complete retail audio remain unverified. The synthetic
PCM checks do not establish that a particular retail stream is producing data.

The Windows release workflow checks the portable launcher, ZIP, installer,
installed application's complete self-test, ISO rejection, and uninstall
before publication. The self-test does not require an audio endpoint.

## Remaining work

Verify progress beyond level selection and a complete match with the original
ISO. Measure frame times across movies, menus, and matches on the reported
supported CPU, supported GPU, and system memory system. A sustained 60 FPS requires a
frame budget of about 16.7 ms. The renderer still uses the CPU. Further runtime
optimization, graphics accuracy, audio fidelity, controller compatibility,
and newly reached indirect callbacks need validation.

See [Windows build and startup](windows.md) for controls, audio diagnostics,
requirements, and logs, and [Windows release packaging](releases.md) for
package verification.
