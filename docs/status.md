# Development status

The runtime reaches the title screen, menus, versus level selection, and fight
intros. Fight progression, character poses, frame rates, and audio fidelity
remain under development.
Complete matches and stable 60 FPS have not been verified.

The original Budokai 2 USA ISO is required at every game launch. Automated
checks cover bounded runtime behavior and Windows packaging; full retail
compatibility requires separate validation.

## Desktop launcher and pacing

The Windows launcher remembers a verified ISO path and provides Play, Saves,
and Settings pages. Save imports support extracted folders, PSU files, and
OpenKai2 ZIP snapshots. Imports/restores validate their source before replacing
the card and retain a recovery backup. Windows game and save operations share
a session lock. Renderer, display mode, FPS cap, VSync, FXAA, texture filtering,
and nearest/bilinear/bicubic display scaling are persistent settings.

The default presentation limit is 60 FPS. Late host vertical-blank deadlines
are rebased after a stall to avoid bursts of catch-up animation. Guest cycle
deadlines remain separate from host presentation. This does not establish
stable fight performance.

The Dragon World callback bank at `0x0037A6C0` now has explicit entries for all
86 methods, including `0x00295F40`. Original bank boundaries and completion
instructions are verified against the supported executable. The compiled
completion callback is checked with direct/cached/uncached addresses, dependency
results, caller frames and stack guards. Full story progression still requires
retail validation.

Display checks exercise all six FXAA/scaler combinations on a real OpenGL
context, including content bounds, image orientation, letterboxing and native
edge smoothing. Save checks cover Unicode settings, argument quoting, lossless
PSU/ZIP import, recoverable replacement, damaged archives, traversal and links.
FSR and frame generation are not currently implemented.

## Native VU1 execution

VU1 microprograms use PCSX2's microVU x86-64 recompiler. The adapter preserves
registers, code-upload invalidation, continuation state, PATH1 packet ordering,
and caller floating-point control. Native executable-memory allocation failure
retains the previous interpreter before game vector execution starts. The EE
executor and original guest clocks remain unchanged.

A local comparison of 512 captured dispatches produced identical vector
registers, stored data, and graphics packets with PCSX2's interpreter and
microVU. Independent tests cover flags, matrix products, branches, continuations,
scalar waits, D/T halts, cycle wraparound, code updates, and callback errors.
The original interpreter snapshots remain in the legacy reference checks.
The native backend reached character selection, where hair distortion remained
visible. Stable game frame rates and complete matches remain unverified.

See [Vector execution](vector-runtime.md) for architecture and diagnostics.

## GPU rendering

The default renderer uses OpenGL 4.3 compute shaders for points, lines,
triangles, and sprites. Ordered tile lists retain draw order and reuse pixel
color and depth across a batch. Direct and indexed textures support point and
bilinear sampling, fixed and perspective coordinates, wrapping, texture
functions, fog, alpha/depth tests, blending, and framebuffer masks. Color and
depth targets support 32-, 24-, and 16-bit layouts.

The backend tracks CPU uploads and GPU writes by VRAM page. Reads, transfers,
palette loads, target transitions, feedback, and presentation synchronize data
before a consumer uses it. Unsupported states retain software rendering until
the next presentation. Bounded submission queues limit work in flight. GPU
submission failures propagate to the caller without abandoning a waiting job.

GPU comparisons require real compute dispatch and compare complete VRAM with
the software renderer: 256 material states, 104 format/addressing states,
65,536 integer pipeline draws, and 576 point/line draws. Additional checks cover
uploads, writebacks, render-to-texture transitions, feedback fallback, replacing
the host VRAM allocation, eight real VIF/PATH1 triangles, and failure propagation.
The Linux headless and Windows-under-Wine comparisons use Mesa; hardware game
performance and stable 60 FPS have not been measured here. Display conversion
and parts of guest execution still run on the CPU.

See [GPU rendering](graphics.md) for selection and diagnostics.

## GPU submission and readbacks

Texture source-page footprints are cached by storage format, base, buffer width,
and coordinate bounds. The cache contains addresses, so texture data uploads
and flushes retain their existing visibility rules. Page-aligned buffers use
logical page ranges instead of repeated swizzle lookups. Unaligned bases retain
the previous walk, including wrapped VRAM addresses.

Pixel reads and updates, partial texture uploads, and palette loads read back
only the dirty pages they consume. Dirty data in other GPU targets remains
resident. Presentation, full snapshots, resets, software fallbacks, and local
transfers retain complete synchronization.

Another 8,320 address states compare page masks with the previous walk across
all thirteen formats, aligned/unaligned and wrapped bases, varied widths, page
boundaries, and empty rectangles. A 128-draw sequence changes texture format,
base, buffer width, dimensions, and wrap state. Four targeted readback checks
require one-page transfers while preserving other dirty targets, followed by a
complete VRAM comparison. Existing GPU material, pipeline, and integration
comparisons remain enabled.

Three alternating Windows-binary runs under Wine with Mesa measured 42–48%
lower costs in four small textured draw batches, including GPU completion.
Complete VRAM matched the CPU renderer and all before/after output hashes
matched. These synthetic submission costs are not estimates of hardware game
FPS or sustained 60 FPS.

## Vector arithmetic and constant registers

Common bounded FMAC instructions combine operand validation, SIMD arithmetic,
and flag production. Multiply and add retain separate rounding. Inactive
lanes retain their normalization rules, and uncertain cancellation or boundary
values use the original extended calculation. NOPs avoid arithmetic setup.
The existing exhaustive arithmetic, operand, pipeline and callback checks
remain enabled.

VF00 stays read-only throughout a dual-issued instruction pair. Discarded
upper destinations still produce arithmetic flags, and the lower instruction
reads the constant `(0, 0, 0, 1)`. Eighteen cases cover stores, moves and divisions
paired with arithmetic, maximum and integer-conversion writes on both units.
Another 64 original matrix snapshots cover dense, identity, rotated and
cancelling transforms, both units and resume modes. Every case also checks
vertex positions against an independent matrix product. The vector component
suite contains 51 cases.

## Vector code uploads and packed vertices

Decoded vector instructions are validated against the fetched 64-bit pair.
Repeated microcode uploads no longer scan every decoded entry before resuming
a short program. Identity changes still initialize the cache, and code edits
from callbacks and between resume slices remain visible. Another 64 original
state, cycle and data snapshots cover unchanged uploads, executed and unused
edits, both vector units, and resume slices. Shutdown records both units' code
generation counters to help assess upload activity.

Packed GIF texture perspective values now latch on RGBA writes and survive tag
boundaries. Packed XYZ2/XYZ3 vertices retain exact 32-bit depths. Independent
checks cover both packet parsers, register ordering and reset. These corrections
address packet interpretation; character pose correctness still requires retail
validation.

## Display pacing and vector flags

The window defaults to a 60 FPS limit. `--fps-limit=0` removes host frame
pacing; values from 1 through 240 choose a cap. `OPENKAI2_FPS_LIMIT` also applies
to portable and installed launches, with command-line settings taking priority.
Guest timing and simulation retain their original rates. Repeated window
requests coalesce by guest field, avoiding duplicate conversions and extra
guest execution. There is no frame interpolation; higher window update rates
do not establish higher game FPS.

Selected product lanes use a SIMD exponent/sign classification for sticky
flags when every active lane is proven to avoid underflow and overflow.
Boundary cases retain the original scalar calculation. MAC bit planes and
STATUS use packed lane flags with the original destination mask and visibility
timeline. The 73,728 original arithmetic cases, 128 selected-operand snapshots,
32 product snapshots, and execution/scheduling references remain enabled.

Three alternating Windows-binary measurements under Wine showed 7–17% lower
costs in eight product programs and about 33% lower median cost across 32
selected-operand programs. All original state and cycle hashes matched. The
general control group had about 0.8% higher median cost. These synthetic costs
do not establish game frame rates.

## Vector operands and depth precision

FMAC flag classification now checks only inputs read by the current instruction
when the existing common path cannot prove all registers bounded. Destination
masks, broadcast selectors, scalar Q/I forms, accumulator use, and cross-product
lane permutations determine the selected inputs. Unused extreme values no
longer force extended arithmetic. Arithmetic results, guest cycles, flags,
exception handling, and cancellation behavior retain their original paths.
Another 128 full-state references cover 32 programs with unused extreme and
denormal register data, partial masks, scalar and broadcast forms, accumulator
destinations, and cross products. The vector suite contains 51 cases.

Three alternating Windows-binary runs under Wine measured about 32% lower
median CPU cost in these targeted programs, ranging from 5% to 50% lower.
Two clean control groups had median costs about 0.2% and 2.4% higher. Original
state and cycle hashes matched in every case. These synthetic measurements
do not establish retail frame rates.

Triangle depth interpolation now uses depth differences from one vertex.
Constant-depth triangles retain the exact original integer, including the full
32-bit maximum. This prevents large common depths from acquiring spurious
variations through float barycentric weights. Both the CPU and GPU paths are
checked against ten exact constant-depth cases and eight independently defined
depth planes with nearby overlapping surfaces and both windings. Existing
material, sampler, feedback, and full-VRAM checks remain enabled. Character
pose correctness and retail frame rates still require game validation.

## Pause menu callbacks

The pause menu's 21 descriptors contain six callback slots each. Nine distinct
methods and three installed continuations have explicit native dispatch entries,
including the previously missing open callback at `0x001C0BD0`. Original table
words, full method instructions, and the input caller are verified against the
supported executable.

The native regression executes the original open callback and timer continuation
in 72 cases, and the original input caller in 441 cases. It checks all descriptor
rows, input priority, null callbacks, address aliases, dependency arguments,
installed continuations, saved registers, and stack guards. These bounded checks
do not establish complete retail pause-menu behavior.

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
The vector component suite contains 51 cases.

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

The vector suite includes 128 new snapshots from the unmodified
interpreter, checking full packets, callback cycles, wrapped memory, chained
tags, packets up to 64 KiB, pending stores, callback writes, and resume budgets.
Existing arithmetic, scheduling, and real-runtime rendering checks remain.

Three alternating Windows-binary medians under Wine showed about 80% lower
cost for the 4 KiB transfer control, 18% for the shorter packet control, and
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

The neighboring timing bank at `0x00375520` also has explicit dispatch entries:
ten methods and two installed continuations. This includes `0x001FC1E0`, which
copies the current frame delta and could otherwise leave the main guest thread
waiting at an unresolved indirect call. Original table words, complete method
instructions, and the bank catalog are verified. Native fixtures add 24 raw
delta-copy cases, 120 timer boundary cases, and 30 original caller cases across
three address aliases, bringing actor-command coverage to 342 cases.

The actor state bank at `0x003758C0` has thirteen registered methods, including
the state query at `0x001F9290`. All 23 bank-catalog pointers and the complete
method instructions are verified. Another 72 state-query cases cover actor
selection, raw values, side inversion, and aliases; 39 cases run the original
indirect caller. Total actor-command coverage is 453 cases. Complex transition
dependencies are not covered by a complete retail match here.

Vector product sticky flags use normalized operand signs, zero bits, and a
proven exponent range. Boundary products retain extended-precision arithmetic
and its underflow/overflow behavior. Bounded simple arithmetic can classify
exact zero results directly; product-sum cancellation retains its original
calculation. The 73,728 original arithmetic cases remain enabled, alongside
32 new original product-program snapshots and the existing pipeline and real
runtime rendering checks. The vector suite contains 51 cases.

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

The Python suite has 199 tests. Native checks include the original BCM/BSK
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
