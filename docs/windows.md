# Windows build and startup

OpenKai2 is in development. Title-screen boot and gameplay have not been verified.

## Downloads

Get the Windows development preview from
[Releases](https://github.com/7v0biscuit/OpenKai2/releases).

- `OpenKai2.exe` is a self-contained portable launcher. It temporarily extracts
  its runtime, opens the ISO picker, and removes the temporary files after exit.
- `OpenKai2-windows-x64.zip` can be extracted to a folder of your choice. Keep
  its DLLs beside the included application EXE.
- `OpenKai2-Setup.exe` installs for the current Windows user and adds Start-menu
  and uninstall entries. It does not require administrator access. Uninstalling
  preserves the disc cache, logs, and unrelated files in the installation folder.

The downloads need Windows 10/11 x64, an SSE4.1-capable CPU, and your original
USA ISO. Python and Visual Studio are needed only for building from source.
Development downloads are unsigned. Verify their SHA-256 values against the
release's `SHA256SUMS.txt`.

## Build requirements

- Windows 10/11, x64, with an SSE4.1-capable CPU.
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**.
- Python 3.10+, Git, and CMake 3.21+ on PATH.
- Your original **Budokai 2 USA ISO**. Game data is not distributed with OpenKai2.

Extract a compressed disc archive to a `.iso` file before building or launching.

The first build downloads PS2Recomp and development dependencies. It generates
and compiles game code locally. Use `--jobs 2` initially to limit compiler memory.

## Build

From the repository root in PowerShell:

```powershell
python tools/native/build-windows.py --iso "D:\Games\Budokai 2 USA.iso" --jobs 2
```

The builder verifies the disc before translating its executable. The supported
executable SHA-256 is:

```text
431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898
```

PS2Recomp is pinned to `eb8688b2fd191129f103a39543002c005b33409c`.
Build output and runtime DLLs are staged under
`.local/openkai2/budokai2-native/`. Set `OPENKAI2_WORK_DIR` or pass `--work-dir`
to keep development files elsewhere; set the same environment variable when
using the startup diagnostic.

## Launch

```powershell
& ".\.local\openkai2\budokai2-native\OpenKai2.exe" "D:\Games\Budokai 2 USA.iso"
```

You can also double-click `OpenKai2.exe` and select the ISO in the file picker.
Cancelling the picker exits normally. Errors from an interactive launch appear
in a message box. Startup output is saved to
`%LOCALAPPDATA%\OpenKai2\logs\startup.log`.

The Windows builder also creates `OpenKai2-windows-x64.zip` in its working
directory. Extract the whole archive before launching. It contains the executable,
its required DLLs, license notices, and a SHA-256 manifest. Python and the build
tools are not needed to launch that bundle. The packager checks both direct and
transitive DLL imports and refuses to package an incomplete dependency set.

The ISO is required at every boot. Passing an extracted ELF or removing the ISO
while retaining the cache does not satisfy startup requirements.

Before initializing the runtime, OpenKai2 validates ISO9660 directory records,
checks the USA executable identity, hashes every file, and verifies each cached
copy against the ISO. Missing or altered cache files are refreshed from the
image. Multi-extent files are reconstructed in order. Runtime disc searches
retain the original ISO sector addresses, and raw sector reads use that ISO. Unsupported layouts,
unsafe paths, incomplete discs, and conflicting filenames stop startup.

The extracted disc cache is stored under `%LOCALAPPDATA%\OpenKai2\discs`,
keyed by the full ISO SHA-256. Allow space for the extracted disc contents.
Every launch rereads the ISO and cached files, so verification time depends on
disc size and storage speed.

If verification was forcibly terminated and the next launch reports that the
cache is in use, close all OpenKai2 instances before removing the
`.openkai2-lock` directory inside the affected ISO hash directory.

Executable identity is checked against the known USA SHA-256. The remaining
file hashes establish consistency with your supplied ISO; a separate trusted
hash for the entire retail disc is not currently pinned.

## Startup diagnostics

Runtime integration can be checked without booting the game:

```powershell
& ".\.local\openkai2\budokai2-native\OpenKai2.exe" --self-test
```

This checks IOP import arguments, module-worker startup, sleep and wake
deadlines, SPU2 initialization, read-only wait diagnostics, and an isolated
compiled callback's stores and return. It also checks subsystem method-table
coverage, original leaf returns, file-driver queries and size arithmetic, and
their compiled callers. Scratchpad DMA checks verify received bytes, wraparound,
cached and uncached addresses, completion registers, and the original compiled
copy caller. The original ISO remains required for
game startup.

Normal launches retain EE wait-state and graphics-activity reports in
`startup.log` at 5, 15, 30, and 60 seconds, and on shutdown. If the window remains
blank, leave it open for at least 60 seconds before closing it and retaining the
log. Reports are bounded and do not release waits or change guest state.
Graphics records include frame dimensions, source framebuffer, nonblack pixel
count, RGB hash, drawing state, and recent draw/transfer events. Movie-wait
records include the original player state and display registers, sampled on the
EE executor. A zero nonblack count means a black rendered frame when `frame=1`;
`frame=0` means no host presentation frame is available.
Movie activation and backend-state changes are recorded immediately, with
follow-up samples timed from activation. Those records include the live player,
filename, SDK error, and scene callback, with at most 12 movie reports per run.
The self-test also checks the original MPEG SDK work area, callbacks, native
dispatcher, reset, and reference buffers with the compiled Sofdec no-data callback.

The self-test also checks callback stack separation, nested allocations, and
resumption of an interrupted caller through the EE scheduler using an original
translated callback.

It also verifies the SDK bootstrap-priority transition and runs the game's
translated initialization to create four EE workers and reach their sleep points.
The SJRBF stream checks execute all nine interface methods and the original
cleanup caller, including buffer recycling, wraparound, and error callbacks.
The sound-transfer checks execute the original `sceSdTransToIOP` routine,
verify bytes in IOP RAM, and resume the caller reported in the startup log.
Asset relocation checks execute all seven tagged handlers through the original
dispatcher, including nested assets and protection against repeated relocation.
Fade checks execute the logged update caller and original renderers, inspect
overlay command packets, and verify completion callbacks and saved registers.
Movie-interface checks exercise the logged volume call, original ADX volume and
pan adapters, channel selection, clamping, status queries, and memory bounds.

```powershell
python tools/native/diagnose-startup.py "D:\Games\Budokai 2 USA.iso" --timeout 180
```

The diagnostic writes `startup-status.json` and `startup-verified.log` under
`.local/openkai2/budokai2-native/`. Disc verification, ELF loading, and graphics
initialization are reported separately. A timeout or missing-module failure
stops the run; it does not count as a verified game boot.

The build also runs the ISO parser tests, an EE/IOP original-sector regression,
and four upstream IOP test groups. CI builds these tools and tests without game
data using `--tools-only`; this mode does not produce a translated game binary.

Windows CI builds the development tools and runtime integration tests with
Visual Studio 2022. The translated game executable is cross-built in the Linux
development environment and checked under Wine. Real Windows game startup with
the complete ISO still requires verification.

The fallback translation still reports unresolved instructions. Analyzer and
recompiler logs remain in the native working directory. See
[development status](status.md) for the current evidence and limitations.
