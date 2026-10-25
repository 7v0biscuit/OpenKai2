# Windows release packaging

Tags and display titles use only the semantic version, such as `v0.1.11`.
Set `version` without the leading `v` in `packaging/windows/release-input.json`
and add the corresponding notes under `docs/release-notes/<version>.md`.

Releases retain GitHub's prerelease status while gameplay and performance remain
under development. A descriptive release name does not establish full gameplay.

The input is a ZIP produced by `tools/native/package-windows.py`. Its manifest
records the native source revision and payload hashes. `build-release.py`
checks every payload file and DLL dependency before building two NSIS programs:
a portable launcher and a per-user installer.

With NSIS 3.11 installed, run:

```sh
python tools/native/build-release.py --bundle /path/to/OpenKai2-windows-x64.zip --output dist --version 0.1.11
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

After all packages pass validation and publication, superseded development
releases and their tags are removed. Newer publications and stable releases
are retained. Historical change notes remain available. Keep the current runtime-input
branch for reproducible packaging; remove superseded input branches after
verifying the replacement release.

To retry packaging with workflow fixes while preserving an existing release tag,
push the corrected packaging commit to `release/v<version>`. The branch name must
match the pinned version, and the matching release tag must already exist.
The workflow accepts an existing NSIS 3.11 installation or retries installation
up to three times before failing with the package-manager log.
