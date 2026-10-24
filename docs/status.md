# Development status

The runtime reaches the title screen, menus, versus level selection, and fight
intros. Fight progression, character poses, frame rates, and audio fidelity
remain under development.
Complete matches and stable 60 FPS have not been verified.

The original Budokai 2 USA ISO is required at every game launch. Automated
checks cover bounded runtime behavior and Windows packaging; full retail
compatibility requires separate validation.

## Battle asset loading

Battle startup uses an eleven-stage initialization table. Its entries are
registered explicitly, with the original table and initializer instructions
verified against the supported executable.

The subsequent asset dispatcher calls two additional relocation routines:
BCM animation data at `0x001D21D0` and BSK skeleton data at `0x001D2100`.
The missing BCM entry previously left the main guest thread dormant after
stage selection. Both routines now have explicit translation entries.

Regressions execute the original tag dispatcher, including its nested pointer
updates, variable record sizes, null pointers, empty records, and completion
markers. Direct and uncached aliases retain their object-relative addresses.
Already relocated assets are left unchanged, and caller frames and adjacent
memory guards are checked. These fixtures do not establish complete matches.

## Battle commands

The battle command dispatcher at `0x001C89A0` selects 24 handlers through a
45-slot table at `0x00374FA0`. These entries, including `0x001C8900`, are now
registered explicitly. The original table, reserved entries, both local
switches, and complete handler instructions are checked against the supported
executable.

Regression fixtures execute the original dispatcher across all 45 commands,
three context modes, and four parameter/enable combinations. They check enable
bits, filtered and reserved commands, repeated model callbacks, character
animation state, saved registers, source parameters, and stack guards. Model
and sound dependencies are bounded by test recorders; these checks do not
establish complete battle compatibility.

## Fight intro drawing and skeletal transforms

The battle draw dispatcher at `0x001BE0D0` selects two tables with sixteen and
fourteen slots. All 24 distinct methods have explicit translation entries,
including `0x001BA250`. The original table slots and complete method instructions
are verified against the supported executable. Numeric data following the
secondary table is excluded from its callback entries.

Regressions run the original caller in 224 cases covering both mode-selection
paths, enable masks, reserved entries, callback order, and saved registers.
Sixteen additional cases execute the original intro method through its paused,
empty-command, animated-model, and visibility paths with bounded dependencies.

Main, newly started, restarted, and callback contexts retain VU0's hardwired
`VF0 = (0, 0, 0, 1)`. Previously, newly created contexts could supply zero for
the homogeneous component. Sixty-four cases execute the original quaternion
transform in started and restarted workers, comparing rotation, parent matrices,
translation, and homogeneous coordinates against an independent matrix reference.
Retail character poses and complete fight intros still require validation.

## Battle state updates

Both sixteen-slot battle update tables have explicit translation entries for
all eighteen distinct methods. Seventeen were previously missing, including
`0x001C6E00`, which stopped the main guest thread after stage selection.
The original slots, reserved nulls, complete method instructions, and delay
slots are checked against the supported executable.

Regression fixtures execute the original caller in 545 cases covering both
selection paths, all table modes, paused and absent contexts, character health
and animation state, secondary progress flags, state snapshots, saved registers,
and stack guards. Tail aliases are compared against the shared original method;
model, allocation, and command dependencies are bounded. These checks establish
dispatch behavior, not complete retail battles or stable 60 FPS.

## Aura updates

Both character aura tables have explicit translation entries for all six
methods, including `0x00211FC0`. Their original slots, reserved nulls, complete
instructions, and return delay slots are verified against the supported
executable. This addresses a missing indirect call during battle setup.

The original primary and secondary selectors run in 576 bounded cases covering
both characters, inactive primary and secondary objects, short and full matrix
copies, scale and progress fields, special states, completion, saved registers,
and stack guards. Model submission is recorded by a test dependency; the
fixtures stop at the real callback return before unrelated asset processing.

## Scene setup and rendering

The scene-mode table at `0x00373A60` contains seven callbacks shared by its
sixteen slots. All seven now have explicit translation entries, including
`0x001B3070`. Their complete instructions, repeated table entries, and reserved
null slots are verified against the supported executable. Regression fixtures
execute the original indirect caller in both scene variants, checking record
creation, animation state, saved registers, and the callback return.

The CPU renderer prepares common unmasked CT32 writes once per primitive.
Sprites and triangles can use this path with depth writes enabled and
predecoded blend selectors, including opaque source-over and PABE behavior.
Alpha/destination tests, framebuffer masks, fog, and other framebuffer formats
retain the general pixel pipeline. Color/depth write order remains unchanged when
buffers overlap.

Sixty-four additional fixtures compare complete VRAM and subsequent cached
texture samples against the previous renderer. Existing serial/parallel,
filtering, blending, and texture-feedback comparisons remain enabled. Renderer
microbenchmarks measured 7–16% lower median draw times across twelve indexed
texture fixtures, using three runs per version. They cover large parallel and
small serial sprites/triangles with opaque, translucent, and additive blending;
all resulting VRAM hashes matched. These are individual draw measurements and
do not establish game frame rates.

Hidden pixels can now fail depth testing before texture sampling and color
combination. This path requires an unmasked CT32 destination, immutable texture
pages, and an initial cache that agrees with VRAM. A small coverage-aware probe
keeps apparently visible draws on the usual path. Deferred final samples retain
the cache state of rejected pixels; feedback, stale pages, alpha/destination
tests, masks, fog, and other formats retain their existing behavior.

Forty-eight additional fixtures compare serial and parallel output against the
previous renderer. They cover partially and fully hidden draws, unsigned depth,
indexed textures, perspective coordinates, feedback, invalid region-clamp
bounds, stale pages, and dependent samples. Startup diagnostics report enabled
draws and the number of rejected pixels under `[GS:early-depth]`.

An 18-fixture microbenchmark measured 22–46% lower draw time with approximately
half the pixels rejected, and 79–93% lower time for fully rejected draws. The
largest visible-draw overhead was about 4%. Medians use three runs per version;
all VRAM hashes matched. These are synthetic draw timings, not game frame rates.

The optional `openkai2_depth_bench` CMake target builds
[`tools/benchmarks/depth_raster.cpp`](../tools/benchmarks/depth_raster.cpp). It
prints per-draw timings and complete VRAM checksums for visible, partially
hidden, and fully hidden indexed-texture sprites and triangles.

Indexed texture draws can prepare palette colors once and reuse them across
pixels and filter taps. A small cache compares the actual latched CLUT bytes
and all lookup state, including source and palette formats, CSM, CSA, and TEXA.
Draw closures carry the prepared colors directly to raster workers. Palette
pointers are read once per draw, so pixels and filter taps avoid an extra
thread-local lookup, including on Windows. Small
draws with cold palettes retain scalar lookup to limit preparation overhead.
Startup diagnostics report palette reuse, builds, and fallback counts under
`[GS:palette]`.

Fifty additional fixtures compare fourteen draws each against the previous
renderer in serial and parallel modes. They cover all five indexed formats,
32/24/16-bit and invalid palette formats, both CSM modes, high CSA values,
explicit and skipped CLD loads, transparency changes, reset, reinitialization,
and alternating backend instances. Complete VRAM hashes must agree.
The optional `openkai2_palette_bench` target measures 24 indexed-texture cases,
including repeated, alternating, and frequently reloaded palettes. It accepts
an optional worker count, for example `openkai2_palette_bench 1`.

The Windows binaries measured roughly 14–20% lower median draw times on visible
serial fixtures with reusable palettes against the earlier renderer. These
three-run measurements execute native Windows code under Wine and retain
identical VRAM hashes. Parallel timings remain mixed. Frequently changing palettes can still add overhead on
tiny draws. The benchmark does not establish native Windows game frame rates.

## Interpreter performance

The IOP scheduler now skips periods with no runnable thread or pending event
until the next known wake, timer, DMA, or callback deadline. Operations that can
wake a thread invalidate that deadline. Fractional EE-to-IOP clock conversion
and guest interrupt timing remain unchanged.

Instruction fetches are shared between import detection and execution. Thread
selection combines its wake and priority scans. Diagnostic CPU snapshots and
import text formatting are deferred until needed instead of being copied or
formatted at each instruction or import.

An isolated benchmark using two million single-cycle scheduling charges measured
these median times:

| Workload | Previous | Updated |
| --- | ---: | ---: |
| Sleeping IOP threads | 67.67 ms | 9.62 ms |
| Busy interpreted thread | 97.24 ms | 65.43 ms |

Both versions executed the same guest cycle and instruction counts. These
measurements cover the interpreter; they are not game-FPS results. Graphics
still use the CPU renderer, and sustained 60 FPS requires a frame budget of
about 16.7 ms across the complete runtime.

Startup logs include sampled inclusive guest-call timings under `[guest:timing]`
and `[guest:hotspot]`, with separate dispatch, IOP, and VU0 entries. These are
host execution samples, not game frame-rate counters; nested timings of the same
kind are suppressed. Only the active EE executor records samples.

During gameplay, graphics diagnostics retain one in 64 draw-history entries.
This avoids copying graphics state and taking a backend snapshot lock for every
primitive. Register and transfer history remain at full detail. Component checks
compare complete VRAM from full and sampled histories for sprites, triangles,
and blending. The optional `openkai2_submission_bench` target compares the two
diagnostic modes; its measurements cover synthetic submission work.

Three-run Windows binary measurements under Wine showed roughly 3–22% lower
median times for the smallest sprite and triangle submissions, with matching
VRAM checksums. Larger-draw timings were mixed, including small regressions.
These measurements do not establish retail game frame rates.

## Fight rules and result transitions

The reviewed translation includes all 20 distinct methods from the adjacent
fight initialization, update, and result-rule tables. This covers the indirect
rule call reached after the fight introduction, including the previously
unregistered target at `0x1d34f0`.

Native checks execute the original call site across 896 cases. They cover all
14 valid rule slots, timers, knockouts, ties, state commands, and saved
registers. This validates the translated rule dispatch; uninterrupted retail
matches still require gameplay testing.

## Texture sampling throughput

Sprite and triangle rasterization now passes each worker's texture cache
directly to the sampler. This removes repeated thread-local cache lookups from
texture taps while retaining palette handling, filtering, wrapping, feedback,
and cache visibility.

The optional `openkai2_texture_cache_bench` target covers 40 sprite and triangle
cases with direct and indexed textures, point and bilinear filtering, small
draws, perspective, and feedback. It accepts a worker count, for example
`openkai2_texture_cache_bench 1`.

Three-run Windows binary measurements under Wine showed about 29–36% lower
serial bilinear draw times and 3–24% lower point-filtered draw times. All 40
VRAM checksums matched the previous renderer with both one and four workers.
These are synthetic draw timings, not retail game frame rates. The CPU renderer
still needs further optimization to establish sustained 60 FPS.

## Audio

SPU2 AutoDMA PCM reaches a 48 kHz stereo host stream through raylib's miniaudio
backend. Ordinary compressed voice/sample DMA is excluded from the PCM path.
A 2,048-frame prefill buffers delivery jitter. Short ramps soften startup,
starvation, and stream replacement boundaries while retaining sample rate and
channel order. After queue overflow, the callback discards old buffered latency.
It does not repeat or time-stretch missing audio.

Interrupted fades now restart from the last emitted stereo sample. Repeated
stops, replacements during a fade, and later stops after silence cannot restart
an older, louder tail. These checks address boundary clicks; they do not restore
missing source audio or complete SPU2 synthesis.

Diagnostics report underruns, frames without queued input, producer drops, and
consumer latency trimming. PCM output does not yet provide complete SPU2 voice
synthesis, volume/effect behavior, or retail audio fidelity. Runtime slowdown
can still cause audible gaps; buffering alone cannot restore real-time speed.

## Validation

The executable embeds its source revision in a contiguous log marker. Native
builds refresh this stamp after a commit; portable and installer packaging
reject binaries whose marker disagrees with the checkout or bundle manifest.

The Python suite has 179 tests. Native checks include the original BCM/BSK
asset dispatcher, all eleven battle initialization entries, fragmented and bulk
IOP clocks, delayed wakeups, external RPC delivery, both SPU2 DMA cores, planar
PCM order, partial blocks, queue boundaries, and concurrent delivery.

Renderer checks retain scalar blend comparisons, serial/parallel triangle and
sprite snapshots, texture filtering, feedback, and depth behavior. Movie, DMA,
scheduler, input, and persistent-save regressions remain enabled.

The audio-device diagnostic delivered 48,000 stereo frames through a Linux
callback using an ALSA null output. That validates callback delivery, not
physical playback fidelity. Windows release checks cover the portable launcher,
ZIP, installed application's full self-test, ISO rejection, and uninstall.

See [Windows setup and controls](windows.md), [building](building.md), and
[release packaging](releases.md).
