# Development status

OpenKai2 is in early development. Title-screen boot and gameplay are unverified.
The application requires the original USA ISO at every launch.

## Verified components

- All 16 EE component probes build on Linux and as Windows x64 executables.
- All 123 Python regression tests pass, including translation entry, portable, and release package checks.
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
  exercises EE and IOP file lookup and raw sector reads.
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

The subsequent user log passes that callback and ends in CRI's RPC bind retry
loop (`0x00116D58`, SID `0x80000597`) when the window closes. The sound driver
had entered cold initialization. SPU2 transfer status now clears its completion
latch when ATTR disables DMA, and becomes ready when transfer mode is enabled.
An IOP interpreter regression executes a reset-and-poll sequence after DMA;
both cores and byte, halfword, and word accesses are checked. This fixes a
reproducible register bug; its effect on retail startup still needs verification.
Pending RPC binds retain up to four state samples per SID in the startup log.

The next user log shows cleared sound status but no CRI RPC server at attempt
64. A separate IOP import-pipeline regression reproduces incorrect thread
creation when the caller loads an argument in the JAL delay slot. The import
dispatcher now retires that pending load before invoking either a host service
or an IRX export, and clears it before the service writes its return value.
Synthetic guest modules check thread creation, thread arguments, RPC handler
registration and execution, and imported return values. Thread lifecycle events
are retained in a bounded trace. Retail startup beyond this point is unverified.

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
identified 5,122 function candidates: 4,953 translated functions and 169 SDK
stubs. Some data and VU regions were treated as EE functions, producing 12,755
unhandled-instruction diagnostics despite the recompiler returning zero.
More accurate function boundaries and further runtime integration may be needed.

Physical audio output, controller input, graphics accuracy, full interrupt
timing, title-screen boot, and gameplay require further validation with the
complete disc. Component checks establish only their tested behavior.
