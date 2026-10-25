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

Startup logs also include cumulative `[runtime:work]` measurements for the
outer guest executor, DMA submission, pending DMA processing, VU0, and VU1.
These record complete host elapsed times rather than extrapolating sampled
calls. Parent scopes include child work, so the totals are not additive and
do not measure game FPS. Empty pending-transfer polls do not take timestamps.

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

## Impact-effect dispatch

The reviewed effect lifecycle includes all four seven-type impact tables and
six adjacent aura callbacks. The original impact factory now resolves the
constructor at `0x210c00`, along with the corresponding update, draw, and
teardown entries. Original instruction words and table slots are checked
against the supported executable.

Native checks cover 112 original factory cases across all seven impact types,
allocation success and failure, actor state, packed parameters, resource
binding, saved registers, and memory guards. These checks validate dispatch
and initialization; they do not establish complete retail attack sequences.

## Vector execution throughput

The vector interpreter retains decoded instruction pairs by reference during
a synchronous execute/resume call. Cache identity is validated at that
boundary; a cheap generation check between instructions also observes host
callback writes. Reuploads decode only instruction pairs whose words changed.
Sparse integer-register masks are walked directly during hazard checks.
On GNU x86 builds, rounding changes update x87/SSE control bits directly,
avoiding MinGW's repeated CPU-feature probes. Another 128 checks cover host
rounding modes and sticky exception flags.

XGKICK resets transfer metadata and initializes the consumed packet prefix
instead of clearing its entire 64 KiB backing buffer on each launch. Qwords
are copied in one or two contiguous pieces while preserving wraparound and
the cycle at which each qword is read.

Forty upstream vector conformance cases cover pipeline hazards, flags, branch
delays, code invalidation, and transfers. Sixty-four additional original
interpreter references compare registers, flags, guest cycles, memory, and
packet bytes across VU0/VU1, cache reloads, resume, and wrapped transfers. A live-code check also verifies
that a GIF callback can change the next instruction without changing the
already fetched pair. The
optional `openkai2_vector_bench` target reports synthetic host execution costs
and verifies guest cycle counts and output hashes.

Three-run synthetic measurements use the same guest cycles and output hashes
before and after the change. Representative times per vector call are:

| Workload | Native Linux, before → after | Windows under Wine, before → after |
| --- | ---: | ---: |
| Short vector launch | 1.36 → 0.63 µs | 9.71 → 0.74 µs |
| Vector arithmetic | 8.11 → 7.12 µs | 16.91 → 6.82 µs |
| 4 KiB XGKICK packet | 20.73 → 17.40 µs | 30.32 → 17.52 µs |
| Repeated code upload | 27.54 → 5.31 µs | 35.87 → 5.68 µs |

All sixteen workloads retain matching register/memory hashes, packet checksums,
and guest cycle counts across both platforms. CPU-feature probe costs depend
strongly on the execution environment, so the Windows-under-Wine gains are
not estimates of native Windows game FPS. Complete retail performance remains
unverified.

### Active pipeline scheduling

Pipeline queues now track occupied slots with masks. Allocation still chooses
its original lowest free slot, and commits preserve slot and pipeline order.
The interpreter skips repeated scans before the next result is ready and jumps
over idle cycles when PATH1 is inactive. During PATH1 transfers, stores and
qword reads retain their original cycle boundaries. Vector instruction handlers
are compiled together to inline operand and scheduler helpers.

Another 128 original interpreter references compare intermediate registers,
flags, memory, guest cycles, resets, and packet callback cycles across eight
resume budgets on both vector units. The component suite contains 43 cases,
including the forty upstream conformance cases.

Three alternating native Linux benchmark runs against the preceding release
reduced arithmetic workload time from 7.01 to 4.18 microseconds (40%), integer
work from 3.53 to 1.38 microseconds (61%), and a 4 KiB XGKICK workload from 17.22
to 2.89 microseconds (83%). All sixteen workloads retain identical guest cycles
and state/packet hashes. These measurements describe synthetic kernels; stable
retail 60 FPS remains unverified. Windows binary measurements under Wine
reduced the same workloads by 38%, 59%, and 79%, respectively, with matching
cycles and output hashes in all sixteen workloads.

The inclusive `path1-submit` work total separately records graphics work
inside vector execution, allowing its cost to be distinguished from the
interpreter. Nested totals must not be added together.

### Runtime header consistency

Version 0.1.11 mixed the patched vector header with an unpatched version pulled
in by quoted includes inside the enclosing runtime header. Absolute upstream
precompiled-header paths also bypassed include-directory precedence. The two
vector layouts differed by 32 bytes; embedded state could be overwritten, and
VIF1 programs could execute with VU0 semantics and omit PATH1 geometry.

The complete public header tree is now mirrored before applying overlays.
Runtime consumers prefer this tree, and precompiled headers use its paths.
A header fingerprint invalidates cached objects, including objects whose old
dependency files would otherwise miss newly shadowing headers. Startup compares
compiled runtime and interpreter layouts before constructing the runtime.

A production-runtime regression fails on the inconsistent build and passes with
the consistent headers. It executes real MSCAL/MSCNT callbacks, renders eight
PATH1 triangles across resets and code reuploads, checks pixels outside the
triangles, and verifies that intervening VU0 microprograms preserve VU1 state.
This covers the integration missing from the standalone vector references;
complete retail scenes and sustained 60 FPS remain unverified.

## Vector arithmetic and geometry uploads

Ordinary vector FMAC results use a bounded-input classification path instead
of repeating extended-precision arithmetic. Zero results and inputs outside
the proven magnitude range retain the original calculation. Numeric results,
guest flags, pipeline readiness, instruction boundaries, and host rounding
controls retain their existing behavior.

Contiguous, unmasked VIF1 UNPACK transfers use direct copies or SIMD component
expansion. Vector-memory wrapping, signed and unsigned expansion, and untouched
lanes are preserved. Transfers with masks, fill/skip cycles, row addition,
truncation, or overlapping source memory use the original scalar path.

The arithmetic regression compares 73,728 cases with original-runtime snapshots,
including cancellation, extreme values, exceptional inputs, destination masks,
and both supported extended-precision and binary64 host arithmetic. The VIF
regression compares 1,664 complete memory/register snapshots across all valid
formats and fallback conditions. The production-runtime scene test uploads its
eight triangles through VIF1 before MSCAL/MSCNT execution and pixel checks.
The vector component suite contains 45 cases.

Three-run Windows binary medians under Wine measured 27–29% lower costs in six
arithmetic-heavy vector workloads and 90–97% lower costs in eight contiguous
upload workloads. All 16 execution states and eight upload snapshots matched
the previous runtime. These synthetic timings do not establish retail frame
rates or sustained 60 FPS.

## Vector transfers and lane scheduling

Known PATH1 payloads are copied in bulk during vector waits and pipeline
flushes, stopping before pending commits, instruction issues, tag parsing,
and completion callbacks. The final qword follows the original cycle path.
Register readiness uses fast full-lane updates and selected-lane iteration;
write retirement compares complete 64-bit sequence numbers and preserves
unselected or superseded lanes. Fully assigned write slots avoid redundant
payload clearing.

The 47-case vector suite includes 128 new snapshots from the unmodified
interpreter, checking full packets, callback cycles, wrapped memory, chained
tags, packets up to 64 KiB, pending stores, callback writes, and resume budgets.
Existing arithmetic, scheduling, and real-runtime rendering checks remain.

Three alternating Windows-binary medians under Wine showed about 80% lower
cost for the 4 KiB transfer control, 16% for the shorter packet control, and
6–10% for eight product-heavy workloads. Original state, cycle, memory, and
packet hashes matched. These synthetic costs do not establish retail game
frame rates or sustained 60 FPS.

## Actor command callbacks and vector product flags

The actor-command continuation at `0x0014C978` selects callbacks through the
bank at `0x00375550`. All forty entries now have verified native dispatch
coverage, including `0x001FB010`, whose absence could stop the main guest
thread. The original bank, method instructions, and indirect caller are checked
against the supported executable. Native fixtures cover 48 orientation callbacks
and 120 original caller cases with argument, address-alias, saved-register,
stream-position, and stack checks.

Vector product sticky flags use normalized operand signs, zero bits, and a
proven exponent range. Boundary products retain extended-precision arithmetic
and its underflow/overflow behavior. Bounded simple arithmetic can classify
exact zero results directly; product-sum cancellation retains its original
calculation. The 73,728 original arithmetic cases remain enabled, alongside
32 new original product-program snapshots and the existing pipeline and real
runtime rendering checks. The vector suite contains 46 cases.

Three alternating Windows binary measurements under Wine showed 12–17% lower
costs in eight multiply-add/subtract workloads, with matching original state,
flag, and timing hashes. These synthetic costs do not establish game frame
rates or sustained 60 FPS.

## Attack-effect dispatch

The reviewed translation includes the 19-slot attack-effect constructor table
and the update callbacks those constructors install. Sixteen methods are
registered and their complete instruction words are checked against the
supported executable. Native regression checks exercise the original indirect
call with successful and failed allocations and all three resource types,
covering 114 constructor cases. Node links, installed callbacks, input packets,
and saved registers are checked.

The prepared CT32 pixel writer also handles alpha-test failure masks,
destination-alpha tests, framebuffer masks, and FBA. CT32 and Z32 use inline swizzled address lookups. Other framebuffer formats
and fog retain the general pixel pipeline. Guest clocks and texture sampling
order are unchanged.

The optional `openkai2_tested_pixels_bench` target covers 32 textured sprite
and triangle cases with alpha-test outcomes, destination-alpha checks,
framebuffer masks, FBA, and existing fast-path controls. Three-run Windows
binary medians under Wine measured about 8–23% lower serial draw times for the
newly prepared states and 2–28% lower times with four workers. All VRAM
checksums matched the previous renderer and agreed across worker counts.
These are synthetic draw costs, not retail game frame rates.

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

## Raster shading throughput

Texture addressing and the CPU renderer compile together so common texture
formats can inline swizzled addressing and sampler-cache resolution. Triangle
color and texture coordinates use SIMD lanes with the original arithmetic
order. Modulation and highlight texture functions operate on packed channels,
retaining saturation, texture alpha selection, and draw ordering.

A new regression compares 128 original material fixtures with one and four
workers, including color gradients, all four texture functions, both alpha
sources, fixed and perspective coordinates, filtering, and direct and indexed
textures. Complete VRAM is compared after each shaded draw and a dependent
sample, for 512 snapshots. Existing coverage, feedback, depth, and serial/parallel
renderer references remain enabled.

## Audio

Host playback is disabled during performance development. Startup opens no
playback device, PCM submission is a no-op, and decoded-sound playback uses a
null backend. No audio mixing callback or playback queue runs. The
`--audio-test` command reports this state without opening a device.

Guest sound DMA, RPC responses, and interrupt timing remain enabled. Their
component regressions still cover planar PCM, transfer ordering, queue and fade
logic. Retaining these checks does not enable host playback. Complete SPU2
synthesis and retail audio fidelity remain incomplete.

## Validation

The executable embeds its source revision in a contiguous log marker. Native
builds refresh this stamp after a commit; portable and installer packaging
reject binaries whose marker disagrees with the checkout or bundle manifest.

The Python suite has 193 tests. Native checks include the original BCM/BSK
asset dispatcher, all eleven battle initialization entries, fragmented and bulk
IOP clocks, delayed wakeups, external RPC delivery, both SPU2 DMA cores, planar
PCM order, partial blocks, queue boundaries, and concurrent delivery.

Renderer checks retain scalar blend comparisons, serial/parallel triangle and
sprite snapshots, texture filtering, feedback, and depth behavior. Movie, DMA,
scheduler, input, and persistent-save regressions remain enabled.

Windows release checks cover the portable launcher, ZIP, installed application's
full self-test, ISO rejection, and uninstall. Native checks also compare
147,456 tested sprite and triangle writes with the unchanged scalar point
pipeline, including all alpha comparisons and failure outcomes, destination
alpha tests, framebuffer masks, FBA, depth, aliases, and neighboring VRAM.

See [Windows setup and controls](windows.md), [building](building.md), and
[release packaging](releases.md).
