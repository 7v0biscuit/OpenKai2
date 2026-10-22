# Development status

OpenKai2 is in early development. Title-screen boot and gameplay are unverified.
The application requires the original USA ISO at every launch.

## Verified components

- All 16 EE component probes build on Linux and as Windows x64 executables.
- All 152 Python regression tests pass, including translation entry, portable, and release package checks.
- Synthetic and USA-executable component comparisons pass.
- ISO verification passes 33 native Linux checks and 32 Windows-executable
  checks under Wine. Wine cannot execute the cache-symlink check.
- The native Windows x64 game executable links with its runtime DLLs and passes
  missing-ISO and extracted-ELF rejection checks under Wine.
- An extracted portable Windows bundle passes the same checks. Its ISO picker
  opens and cancels normally under Wine; payload hashes and DLL imports verify.
- Windows CI passes component tests and MSVC runtime integration tests without
  game data. The game translation itself is built separately.
- The original-ISO sector integration test and all four upstream IOP test groups
  pass on Linux and as Windows executables under Wine. The integration test
  exercises EE and IOP file lookup, raw sector reads, and the CD/DVD search RPC
  through EE SIF.
- SPU2 initialization checks pass on Linux and as a Windows executable under
  Wine; the packaged game executable also passes them through `--self-test`.
- The disc loader validates directory records, reads and hashes every file,
  repairs damaged cache files, and rejects missing or unsupported disc input.

## Runtime integration

The earlier ELF-based Linux startup loaded the game, began execution at
`0x00100008`, initialized Mesa graphics and null audio, and reached IOP module
loading. It stopped because the disc modules were absent. That startup path
has been replaced with an ISO-required application entry.

Runtime file searches preserve the original ISO sector addresses. Raw EE and
IOP reads use the supplied image rather than a synthetic extracted-file layout.

A user-provided Windows startup log confirms that ISO verification completed,
the native entry executed, and SIO2MAN, LIBSD, and CRI_ADXI loaded. That run
stopped at the unregistered indirect entry `0x00167188`. The translation now
exposes the callback and its exact resume label; an isolated runtime regression
checks its argument stores, delay slot, and return.

Later logs stop in the RPC bind retry loop at `0x00116D58`, requesting
`0x80000597`. This SID belongs to the CD/DVD file-search service (CDVDFSV).
Earlier previews incorrectly described it as a CRI sound-server wait. The latest
thread snapshot shows the CRI main thread waiting for VBlank and its RPC worker
sleeping after registering the separate sound SID `0x90000200`.

The runtime now supplies the missing CD/DVD file-search service from the original
ISO. It supports the 292-, 296-, and 300-byte request formats, returning the real
sector, size, filename, recording date, and attributes to the EE destination.
A missing file, invalid packet, unsupported layer, or missing ISO returns failure.
An actual guest-registered server takes precedence over this host service.

The regression reproduces the unavailable bind on the previous runtime, then
binds and completes a search through EE SIF with the updated runtime. It checks
all three packet layouts, aliased guest addresses, output bounds, malformed
requests, missing files, and ISO removal/reset. These checks use a synthetic ISO;
retail startup beyond this bind still requires verification.

Earlier component fixes remain: SPU2 DMA status clears on transfer disable, and
IOP import dispatch retires a pending load before invoking a service or IRX
export. Guest regressions check both SPU2 cores and imported thread/RPC calls.
Bounded pending-bind diagnostics retain thread states, wait objects, nearby
instructions, and recent imports; scheduler exceptions are reported.

The latest user log confirms that CD/DVD search now finds `CACHE.LST` and the
three AFS archives, CRI registers sound SID `0x90000200`, and the remaining sound
modules load through `AMSOUND.IRX`. The game then binds SID `0x11` once and enters
its sound-bind error loop at `0x00146838` because the server is absent. The
snapshot shows the newly started worker still at its entry; it creates another
worker only after the EE has already rejected the bind.

Resident module loading now hands control to ready IOP threads before completing
the synchronous load. The scheduler runs those threads until they block or the
existing execution budget is reached. It keeps priority scheduling, sleep state,
and future wake deadlines intact, and only actual RPC registration makes a
server bindable. A busy worker produces a bounded scheduling warning instead of
holding the load indefinitely.

A synthetic guest module reproduces the previous first-bind failure. Its parent
starts a nested RPC worker, which now registers and serves a real interpreted
request before loading returns. Checks also cover sleeping workers, delayed
registration after a real wake deadline, reset, missing SIDs, and a busy worker's
budget. The packaged application's `--self-test` includes this regression.
Retail startup beyond this sound-bind failure is still unverified.

The following Windows log gets past the sound bind and loads `MCMAN.IRX`,
`MCSERV.IRX`, and `PADMAN.IRX`. It then stops at the unregistered indirect method
`0x001908E0`, called from the startup table dispatcher at `0x0019DF98`.
The stripped-ELF analyzer had folded this method into an earlier function.

The translation now includes all 98 methods in the dispatcher's seven active
subsystem tables. The previous build omitted 86 of those entries. Entry metadata
is checked against the supported executable's table roots, table contents, and
leaf-return instructions before configuring translation. Generated-source
verification requires every entry to start at its own instruction.

The executable self-test checks registration of all reviewed entries, executes
39 original leaf methods, checks their return delay slots and preserved registers
and stack, and runs the compiled dispatcher across two isolated method tables.
These checks use the translated instructions. The other methods still require
valid game objects and disc data for end-to-end validation.

The next user log passes the subsystem dispatcher and stops at file-driver
callback `0x00167AF8`, called from `0x00164940` with a null object and query code
100. Its interface bank at `0x0035FB38` contains 25 reviewed slots, including 14
non-null callback entries. Ten entries were absent from native dispatch.

The translation now includes that callback bank and verifies its original null
slots and method addresses against the supported executable. The self-test
executes the original query callback with null objects, unsupported commands,
file-size queries, and signed size boundaries. It checks object memory, saved
registers, adjacent stack memory, and return-frame restoration. The compiled
caller is tested with the query callback, the original 2,048-byte sector-size
callback, and an absent callback slot. Actual file reads and subsequent retail
startup still require validation with the complete ISO.

The next Windows log passes the file-driver callback and stops at the
unimplemented `sceDmaRecvN` SDK function, called from `0x00186328`. It requests
1,024 quadwords (16 KiB) from scratchpad DMA channel 8 into `0x003C4200`.

Normal scratchpad receives now program the existing DMA controller, preserving
the scratchpad source address and unrelated channel flags. The controller copies
the actual bytes, advances MADR/SADR, clears QWC/STR, and raises completion status.
Reading scratchpad channel status no longer clears STR prematurely. Other receive
devices remain explicitly unsupported rather than returning invented data.

The regression reproduces the previous SDK exception with the retail arguments.
It checks full-size and wrapping copies, cached and uncached destinations, zero
length, SDK send/receive round trips, destination guards, completion polling,
invalid requests, and preservation of an active transfer. The executable self-test
also runs the original compiled scratchpad-copy caller and checks its result,
saved registers, return frame, and copied bytes. Startup beyond this transfer
still requires validation with the original ISO.

The following log completes both scratchpad DMA directions without an exception.
retail testing shows a blank window for over a minute, then closes it. The old log
contains no EE wait-state snapshot, so it does not establish where startup stalls.

Startup now records synchronized EE thread snapshots at 5, 15, 30, and 60 seconds,
plus shutdown. Reports include thread PCs, return addresses, wait reasons and
objects, semaphore counts, event flags, and atomic DMA/GIF/GS/VIF counters. Each
report limits object lists to 16 entries and reports omitted counts. Up to four
zero-PC call returns retain their source, target, continuation, and dispatch
history; a zero PC alone is not classified as an execution error.

The diagnostic regression inspects a genuinely blocked EE semaphore wait, checks
the sample and record limits, and verifies that reporting changes neither guest
memory nor scheduler state. The compiled scratchpad regression now executes both
bank-switch routines, including their send, receive, synchronization, saved
registers, and outer return frame. At that stage the blank-screen cause remained
unidentified; those diagnostic checks did not establish a boot fix.

The next log shows the only EE thread dormant at PC/RA zero, with no semaphore
or event waiters. The runtime's first asynchronous callback stack started at
the top of RDRAM, overlapping the bootstrap thread's live return frames. Saving
the callback's zero return sentinel could therefore destroy the interrupted
caller's return address.

Callback allocation now leaves the top 64 KiB for the bootstrap stack and grows
downward above the existing guest heap limit. The regression reproduces both
frame corruption and loss of the interrupted continuation on the previous
runtime. It checks disjoint nested stacks, reuse by nesting depth, alignment,
arena exhaustion, and successful continuation through the real EE scheduler.
The packaged self-test repeats the scheduler interruption with the original
translated file-query callback. The next retail log preserves the return address
and reaches `SleepThread` at `0x00109A08`, with no EE workers present.

The SDK `InitThread` replacement omitted the original routine's transition from
bootstrap priority zero to user priority one. The game's translated setup reads
that priority through `ReferThreadStatus` and passes it to `CreateThread`. With
zero, all four worker creations return illegal-priority errors. The replacement
now performs the transition while preserving an already configured user priority.
The regression runs the translated SDK entry and game initialization, checks all
four worker IDs and entries, and verifies that two execute to their sleep points.
It also checks descriptor fields, invalid-priority rejection, and saved registers.
The next retail log confirms that all four workers start. The main thread then
stops at the unregistered SJRBF stream cleanup method `0x0016D310`, called through
slot `0x0C` of the interface at `0x00367B68`.

Translation hints now cover all nine methods in that interface, preserving its
three reserved null slots. The packaged regression executes the original cleanup
caller at `0x00156F90`, checks complete object release and adjacent pool bounds,
and verifies construction, interface identity, reset, chunk acquisition, return,
recycling, length queries, error callbacks, and ring wraparound. All 121 reviewed
indirect entries must have valid instruction-level dispatch coverage. Further
retail startup still requires validation with the original ISO.

The next retail log confirms that stream cleanup completes and reaches
`sceSdTransToIOP` from `0x00146B10`. The named runtime stub was unimplemented.
This SDK routine is now translated from the original executable: it submits a
SIF DMA descriptor and waits for completion when requested. Existing SIF support
copies the payload into actual IOP RAM. The packaged regression checks the
logged 2 KiB transfer, synchronous and asynchronous modes, address aliases,
rejected transfers, neighboring memory, preserved registers, and resumption of
the original caller. Translation verification covers 122 reviewed entries,
including this SDK entry. Retail progress beyond this transfer remains unverified.

The subsequent retail log confirms sound transfer completion and reaches the
asset relocation dispatcher at `0x0012A680`. Its call from `0x0012A75C` to the
`AMM` handler at `0x0012AC30` lacked a translated entry, leaving the main thread
dormant. Entry hints now cover all seven handlers in the original table at
`0x0033D680`: AMG, AMT, AMM, AMO, AMC, AML, and AMP. Input metadata verification
checks their addresses, tags, and terminating record. The executable self-test
uses the original dispatcher and logged object address, verifies populated and
empty records, nested AMO/AMM dispatch, relative pointer relocation, adjacent
memory, saved registers, and the already-relocated guard. There are now 129
reviewed guest entries. Further retail progress remains unverified.

The next retail log confirms asset relocation completes and reaches screen-fade
rendering. The indirect call at `0x00249B84` targets `0x002492D0`, which was
merged into a larger function without a callable entry. The fade table at
`0x003786E0` has 20 slots shared by four renderers; all four now have reviewed
translation hints, preserving repeated entries and the reserved custom-callback
slot. There are 133 reviewed guest entries in total.

The packaged regression executes the original fade update and renderer,
inspects black and white overlay packets, opacity, viewport coordinates, queue
bounds, drawing-state restoration, and saved registers. It also exercises
custom rendering, completion callbacks, the completed state, transparent modes,
and the textured renderer's initial delay. It uses the original batched command
queue and checks active radial-fade queue bounds; host rendering, textured output,
and title-screen boot remain unverified.

The subsequent retail log confirms fade dispatch completes and startup continues
through memory-card queries and repeated frame submissions. It next stops at
`0x00186E88`, the Sofdec movie player's output-volume method, called indirectly
from `0x002A4EF8`. Translation hints now cover all 14 methods in the movie
interface at `0x00370AE0`, including merged methods that lacked a callable entry.
The build verifies the original reserved slots, methods, and trailing null word.
There are now 147 reviewed guest entries.

The packaged regression runs the original volume and pan methods through their
movie, ADX, and sound-state adapters. It checks signed values, channel selection,
clamping, no-audio behavior, status queries, saved registers, and neighboring
memory. The logged caller executes its original table loads and JALR and returns
to `0x002A4F00`, before opening the movie file. Full movie decoding, graphics
output, and title-screen boot remain unverified.

Callback regression fixtures allocate their runtime devices on the heap, avoiding
overflow of MSVC's default 1 MiB host stack. Windows source CI passes this check.

The next retail run displays images/logos before turning black, and Start does
not advance it. Its changing nonblack frames confirm early graphics output;
there is no execution exception. Investigation found that the generic MPEG SDK
replacement builds a different work-area layout from this game's native SDK.
It omits the native handle link and callback records, writes unrelated fixed
globals, and returns an allocator pointer instead of SDK success.

The original create, reset, reference-clear, callback-registration, and IPU-sync
routines are now translated, bringing reviewed entry coverage to 152. The
regression executes the original SDK routines through return and checks aligned
and unaligned work areas, callback/data/GP records, the original dispatcher and
compiled no-data callback, reference buffers, reset, memory bounds, saved
registers, and unrelated guest memory. These checks establish SDK setup;
complete retail decoding and the transition to the title screen still require
validation.

Movie diagnostics follow the live game handle and read the actual backend
status word. Activation/state changes are captured immediately, with later
snapshots timed from activation. The common frame-yield path also runs during
logos; the previous fixed deadlines could finish before the movie existed.

An earlier log contains no missing movie callback or execution exception. DMA
and GIF submissions continue for 281 seconds until the window closes. Its zero
GS count was misleading: the counter omitted normal 32- and 64-bit privileged
register writes. These writes now increment the counter. Bounded graphics
reports include presented pixel statistics and recent drawing/transfer events;
four executor-side movie-wait snapshots also record player and display state.

The original solid-fade packets now pass an end-to-end regression through DMA,
GIF decoding, CPU rasterization, and latched host presentation. An opaque white
viewport is followed by a black viewport, checking actual RGB output and frame
replacement. This verifies that isolated path; that retail log did not
identify why visible startup remains unresolved. Full movie decoding and retail
graphics still require local-ISO validation.

Overlay generation also isolates Git discovery from a parent source checkout.
Without that isolation, Git can silently skip a `diff --git` patch when the
default working directory is inside the repository. Regressions cover nested
and external working directories and reject a successful patch command that
changes no files.

The next retail run opens `/USR/MOVIE_US.SFD` and changes its backend status
from 1 to 2 without an SDK error, but stays in preparation for over 100 seconds.
It also shows a horizontal split, stretching, and flashing. The log configures
interlaced FRAME mode (`SMODE2=3`) with a 512x448 display and a 512x224 memory
buffer at page 0, immediately followed by the render target at page 56.

Presentation now reads 224 memory rows and duplicates each into two output
rows before merging the read circuits. Previously it read 448 memory rows and
exposed the neighboring render target in the bottom half. The correction also
honors the display buffer during black fades instead of selecting a stale
context or preferred texture. A patterned regression checks all pixels,
consecutive fields, both circuits, framebuffer origins, black output,
progressive output, and full-height field parity. The installed executable
self-test and the Windows runtime CI both require this regression.

Preparation diagnostics now include the exact parameters and driver status
words tested by the native readiness loop, callback addresses, and video input
buffer state. These snapshots are bounded, validate guest pointers, and do not
execute callbacks or change memory. The preparation stall remains unresolved;
this presentation correction does not establish movie playback or title boot.

A later run still finds the movie but leaves every preparation driver
unready, with an empty native video input buffer. A bounded isolated execution
of the original player service clears its pending flag, advances its service
counter, and invokes the original stream preparation callback. This verifies
that isolated control path, rather than establishing the live file handoff.
New read-only reports include the selected stream queues, their SJ object
headers, the service counter, and the exact global/player entry guards.
Aliased, invalid, duplicate, and out-of-range stream references are covered.

The CPU sprite renderer previously truncated 12.4 vertex positions, XYOFFSET,
and UV coordinates separately. With the fractional logo rectangles in the
retail log, this shifts texture sampling and can drop the last row or column.
Sprite edges now use the GS integral pixel centers with exclusive bottom/right
coverage, and interpolation retains the original fractions. A patterned
regression reproduces the error before the change and checks fractional edges,
fractional UVs under scaling, ordinary integer rectangles, and boundary pixels.
The remaining slight movement and retail movie playback require local testing.

The latest retail log has 1 MiB of readable movie data in the file stream, but
its service counter stops at one and the native decoder input remains empty.
The EE ResumeThread and WakeupThread replacements returned zero on success.
The original CRI resume helper compares the result against the worker's ID
before waking a worker that was both sleeping and suspended. Returning zero
therefore leaves that worker asleep. A second original helper also compares
the WakeupThread result against the target ID before clearing its wake flag.

Those syscalls now return the target ID on success and retain negative errors;
the scheduler's internal return contract is unchanged. A regression executes
both original compiled CRI helpers. It reproduces the stranded worker before
the correction, then checks readiness, released sleep state, wake acknowledgment,
and preserved caller registers. Runtime component checks cover interrupt-safe
wakes, queued wakes, invalid IDs, self-wakes, and dormant-thread errors. These
isolated checks do not establish retail movie decoding.

The remaining presentation shake is reproduced with a stationary ramp sampled
at alternating even and odd scanlines. Duplicating half-height rows moves that
image by one output line between fields. Presentation now keeps the active
field's scanlines at their own positions and interpolates missing lines from
neighboring rows, clamping the image edges. It needs no previous frame and
preserves black fades. Regressions cover four alternating fields, both display
circuits, framebuffer origins, odd heights, adjacent buffers, and progressive
output. The installed executable must pass both new regressions before release.
Retail movie playback and the remaining visible movement need local testing.

The Preview 23 retail log stops drawing after four fields, before the logos
or movie player activate. The main thread is stuck at priority 1 and sleeping;
the service worker is sleeping and suspended. ChangeThreadPriority still
returned zero on success. The original CRI critical-section code saves that
result as its prior priority and later tries to restore it. Zero is illegal,
so the main thread remains at its temporary priority and the frame handoff
cannot reliably continue with the corrected wake acknowledgments.

Both normal and interrupt-safe priority changes now return the actual previous
priority on success, preserving negative errors and the internal scheduler
contract. Component checks exercise save/restore, self IDs, worker priorities,
and invalid calls. Original compiled nested critical sections must restore
priority 24, depth counters, and caller registers. The compiled worker test now
runs sixteen synchronous-service and sleep cycles with all four original CRI
workers and the real VBlank callback. Native movie preparation callbacks run
through that scheduling path and must keep advancing their service counter.
This test reproduces the priority restoration failure before the correction.
It uses a single preparation driver and no disc assets, so it does not verify
movie decoding, visible logos, title-screen boot, or gameplay.

The Preview 24 log confirms restored main/worker priorities and changing
nonblack frames through startup. The movie parser now consumes its file input,
then calls an unregistered packet handler at `0x001915F0` from `0x00191154`.
That failure ends worker 5, leaving the player busy with no decoder input.

The translation now covers all four packet-type handlers in the original table
at `0x00387180`, including the missing private-stream handler. The table is
verified against the supported ELF, and generated dispatch must enter each
handler at its own instruction. The complete reviewed-entry set has 156 entries.

A regression reproduces the missing handler on the previous translation. It
executes the original packet caller, its native packet-info query, the handler,
and the original SJRBF stream methods with the logged 2,030-byte body. It checks
all four packet types, contiguous and wrapped writes, insufficient destination
space, an incomplete source body, empty packets, source/configuration preservation,
output flags, consumed bytes, saved registers, and neighboring memory. Audio
and video handlers are checked with their streams disabled; this is a packet
transport regression, not a movie-decoding test. The installed Windows self-test
must pass it before publication. Visible movie playback, title boot, and gameplay
remain unverified.

The Preview 25 log advances past that packet callback: the video stream contains
about 234 KiB and the audio stream about 22 KiB. The user confirms that screen
shaking is fixed, but no movie appears. The movie worker then becomes dormant at
the unregistered time-code handler `0x0019D4F0`, called indirectly from
`0x0019D42C` with the original 29.97-rate denominator `0x7512`.

Translation now explicitly includes all seven handlers in the timing table at
`0x00387448`. Its nine rates, reserved null pair, repeated slots, and both modes
are verified against the supported ELF; generated-source validation requires
every handler to start at its own instruction. The complete reviewed-entry set
has 163 entries. The existing field-presentation fix is unchanged.

The previous translation reproduces the exact missing target through the
original time-code dispatcher. The updated regression runs that dispatcher for
all nine rates and both modes, checking zero times, seconds, minute and ten-minute
boundaries, hours, long-duration 32-bit wrapping, original numerator/denominator
outputs, saved registers, caller frames, input preservation, and memory guards.
The installed Windows application's self-test must pass this regression before
publication. These are synthetic time-code tests; full movie decoding, visible
playback, title boot, and gameplay remain unverified.

A complete retail ISO is not present in the development environment. Further
end-to-end checks must run the updated application with the user's local ISO.
The launcher obtains `MODULES`, `USR`, and the executable from that image;
separately supplied extracted files are not accepted as boot input.

The Windows build guide and bounded startup diagnostic are in
[Windows build and startup](windows.md). Graphics initialization, native entry
execution, title-screen boot, and gameplay are separate milestones.

The Windows launcher offers an ISO file picker when started without arguments.
It retains a startup log, and portable bundles include the checked DLL dependency
chain. These launch and packaging improvements do not establish game boot.
The release workflow tests the portable EXE, installer, installed application,
and uninstall on Windows before publishing a development prerelease.

## Remaining work

The fallback analyzer reads a stripped ELF containing both code and data. It
identified 5,122 function candidates. Some SDK routines still use host
replacements, and native integration remains incomplete. Some data and
VU regions were treated as EE functions, producing 12,755
unhandled-instruction diagnostics despite the recompiler returning zero.
More accurate function boundaries and further runtime integration may be needed.

Physical audio output, controller input, graphics accuracy, full interrupt
timing, title-screen boot, and gameplay require further validation with the
complete disc. Component checks establish only their tested behavior.
