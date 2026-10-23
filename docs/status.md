# Development status

retail testing reaches versus character loading. Preview 35's log loads and
closes the existing 2,736-byte save, drains both graphics queues, then stops
at an unregistered character-animation callback at `0x002AEAC0`, called from
`0x001483F0`. The main guest thread becomes dormant while the window remains
open. Full gameplay and stable 60 FPS remain unverified.

The original Budokai 2 USA ISO is required at every launch. A complete retail
ISO is not present on the development host. Automated component checks and
observed retail results are distinguished below.

## Current changes

The original character-animation callback is now an explicit translation entry.
ELF validation checks its entire instruction sequence, including the frame
store in the tail jump's delay slot. The runtime regression executes the
original scene dispatch for both character slots. It checks frame advance,
clamping, repeated wrapping, saved caller state, memory guards, and the tail
call into the original model renderer. The regression observes that call before
retail geometry is needed; it does not establish a complete versus match.

The renderer now skips destination reads and blending for exactly opaque GS
source-over pixels when destination tests and masks do not need the old value.
General blending uses SSE4.1 and preserves the original fixed-point rounding
and saturation. Zero-factor sprite passes can skip texture sampling when RGB
is unchanged and the output alpha is known; alpha/depth tests and writes still
run, and final sampler residency is retained.

Large textured triangles use up to four workers when conservative VRAM bounds
prove texture, color, and depth accesses independent. Feedback, stale sampled
pages, cross-row color/depth dependencies, small draws, and unsafe layouts retain
serial execution. Each worker has its own sampler cache and floating-point mode.
The last worker that actually samples a covered pixel supplies the final cached
page, including when later partitions contain no covered pixels. Immutable
triangle textures also avoid repeated page copies in the serial path.

Isolated 512x448 comparisons against the previous renderer, using the median
of three runs of 48 draws each on the development host, measured:

| Draw | Previous | Updated |
| --- | ---: | ---: |
| Nearest textured triangle | 5.13 ms | 2.03 ms |
| Filtered textured triangle | 8.86 ms | 3.80 ms |
| Filtered indexed triangle | 9.65 ms | 3.74 ms |
| Zero-alpha modulated sprite | 4.97 ms | 1.09 ms |

All eight benchmark fixtures retained identical complete VRAM hashes. These
component timings do not establish retail FPS. The renderer continues to use
the CPU. Triangle timing records now complement sprite/upload timings so the
next retail run can identify remaining graphics work.

The earlier interrupt masking, empty-DMA completion, asynchronous presentation,
interlaced display, latched keyboard/controller input, original save callbacks,
and persistent save readback checks remain enabled.

## Validation

The original translated callback is checked through its logged indirect caller
for both character slots across seven animation cases each. Missing generated
entries and changed original instructions are rejected before packaging.

Blending is compared with an independent scalar equation across 12,288 cases:
all selector combinations, eight alpha boundaries, PABE, masks, FBA, and
16/24/32-bit framebuffers. Triangle checks compare 48 complete VRAM snapshots
from the preceding renderer in both serial and parallel modes. They cover
filtered, indexed, and 16-bit textures, interpolation, depth, feedback, stale
pages, clipping, empty partitions, reversed winding, dependent draws, and the
final sampler cache. Sprite checks retain 64 serial/parallel VRAM comparisons,
64 scalar-reference snapshots, and 8,192 scalar bilinear comparisons.

The runtime self-test also covers original translated game callbacks and
synthetic disc, MPEG, scheduler, sound, input, and save fixtures. Save checks
run the original card checks and menu transitions, verify bytes on disk,
restart the memory-card SDK, and read the payload back into guarded EE memory.
They use an isolated temporary card and retain the in-game confirmation.

The Windows release workflow checks the portable launcher, ZIP, installer,
installed self-test, ISO rejection, and uninstall before publication. These
checks do not establish retail gameplay or frame rate.

## Remaining work

Verify progress beyond character loading and a complete versus match with the
original ISO. Measure total game-frame time across movies, menus, and matches
on the reported supported CPU, supported GPU, and system memory system; stable 60 FPS
requires a sustained frame budget of about 16.7 ms. Physical audio output,
graphics accuracy, controller compatibility, and interrupt timing need further
validation. Additional function-boundary corrections may be required as new
game paths execute.

See [Windows build and startup](windows.md) for controls, requirements, and logs,
and [Windows release packaging](releases.md) for package verification.
