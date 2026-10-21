# Development status

OpenKai2 is in early development. Title-screen boot and gameplay are unverified.
The application requires the original USA ISO at every launch.

## Verified components

- All 16 EE component probes build on Linux and as Windows x64 executables.
- All 138 Python regression tests pass, including translation entry, portable, and release package checks.
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

Callback regression fixtures allocate their runtime devices on the heap, avoiding
overflow of MSVC's default 1 MiB host stack. Windows source CI passes this check.

Overlay generation also isolates Git discovery from a parent source checkout.
Without that isolation, Git can silently skip a `diff --git` patch when the
default working directory is inside the repository. Regressions cover nested
and external working directories and reject a successful patch command that
changes no files.

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
identified 5,122 function candidates. After translating the sound-transfer SDK
routine, 4,954 functions are translated and 168 remain SDK stubs. Some data and
VU regions were treated as EE functions, producing 12,755
unhandled-instruction diagnostics despite the recompiler returning zero.
More accurate function boundaries and further runtime integration may be needed.

Physical audio output, controller input, graphics accuracy, full interrupt
timing, title-screen boot, and gameplay require further validation with the
complete disc. Component checks establish only their tested behavior.
