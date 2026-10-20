# Windows release packaging

Release binaries are development previews until title-screen boot and gameplay
are verified with the original disc. The release workflow always publishes a
prerelease and retains that status in its notes.

The input is a ZIP produced by `tools/native/package-windows.py`. Its manifest
records the native source revision and payload hashes. `build-release.py`
checks every payload file and DLL dependency before building two NSIS programs:
a portable launcher and a per-user installer.

With NSIS 3.11 installed, run:

```sh
python tools/native/build-release.py --bundle /path/to/OpenKai2-windows-x64.zip --output dist --version 0.1.0-preview.1
```

On an isolated Windows runner, validate all three downloads:

```powershell
python tools/native/test-windows-release.py dist
```

This installs to a temporary path containing spaces, tests the installed
application and single-file launcher, checks ISO rejection and startup logs,
and uninstalls while preserving a user-created file. It refuses to run over an
existing OpenKai2 uninstall registration.

`packaging/windows/release-input.json` pins the runtime-input commit, ZIP hash,
native source revision, and release version. Runtime inputs are retained on a
`release-input/<tag>` branch so the main source tree stays free of binary payloads.
Keep the input branch available for reproducible packaging.

Push a matching `v<version>` tag after updating and reviewing the input metadata.
The Windows release workflow verifies the input, builds and tests the packages,
then publishes `OpenKai2.exe`, `OpenKai2-windows-x64.zip`, `OpenKai2-Setup.exe`,
and `SHA256SUMS.txt` with the repository's built-in Actions token. Source-archive
links are supplied by GitHub automatically.
