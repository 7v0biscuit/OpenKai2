# Building OpenKai2

## Component tests

Requirements: Python 3.10+, CMake 3.16+, and a C++17 compiler. Run commands from
the repository root:

```sh
python -m unittest discover -s tests -v
python tools/probes/run.py --skip-retail
```

The runner builds all 16 probes in `build/probes/`, compares their output with
independent Python models, and writes results to `reports/probes/`. It replaces
the previous component report directory on each run. The unit suite contains
103 tests covering instruction semantics, interrupt transitions, list traversal,
memory aliases, fault handling, and source integration.

To build the probes separately:

```sh
cmake -S . -B build/probes -DCMAKE_BUILD_TYPE=Release
cmake --build build/probes --config Release --parallel 4
```

On Windows, install Python and Visual Studio's C++ build tools with CMake.
Run the same commands, or open `tools/probes/run.cmd` to choose a local game
file. Windows launchers have not been validated in CI.

## Retail comparisons

Supply your extracted USA executable:

```sh
python tools/probes/run.py --game /path/to/SLUS_207.79
```

The runner verifies the executable's SHA-256 before using its instruction
windows:

```text
431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898
```

An optional `--trace /path/to/observations.jsonl` compares external CP0
observations. The fixture in `tests/fixtures/` is synthetic test data; it is
not a hardware capture.

Collect reports for a bug report with:

```sh
python tools/probes/collect.py
```

This writes `reports/probe-diagnostics.zip`. Review logs before sharing them.

## Windows runtime

See [Windows build and startup](windows.md). The native application requires
the original USA ISO at every boot and verifies the disc files before starting.

## Linux development runtime

The current headless development recipe targets **Debian 13, x86-64**. It needs
Python with `venv`, GCC/G++, Git, APT, `dpkg-deb`, `pkg-config`, and `xdpyinfo`.
Setup downloads dependencies from GitHub, PyPI, and `deb.debian.org`. Debian
packages are verified by APT and extracted into a local sysroot; setup does not
install system packages.

```sh
bash tools/native/setup.sh /path/to/SLUS_207.79
python tools/native/diagnose-startup.py /path/to/Budokai-2-USA.iso
```

Dependencies, generated sources, and the native executable live under
`.local/openkai2/`. To use another working directory, set the same absolute
path for setup and subsequent commands:

```sh
export OPENKAI2_WORK_DIR=/absolute/path/to/openkai2-work
bash tools/native/setup.sh /path/to/SLUS_207.79
python tools/native/diagnose-startup.py /path/to/Budokai-2-USA.iso
```

The extracted ELF above is a compilation input. Runtime startup requires the
complete ISO and prepares its verified disc cache automatically. See the
[Windows guide](windows.md#launch) for the verification rules; Linux uses
`$XDG_CACHE_HOME/openkai2/discs` or `~/.cache/openkai2/discs` for its cache.

Setup pins PS2Recomp to `eb8688b2fd191129f103a39543002c005b33409c`, CMake to
4.4.4, Ninja to 1.13.2, and libarchive-c to 5.3. The validated toolchain uses
GCC 14.2 and Clang 19.1.7. Clang compiles one oversized generated unit; GCC
builds the remaining objects. The refresh command also runs the EE/IOP
original-sector regression and four upstream IOP test groups:

```sh
bash tools/native/refresh-native.sh
```

The launcher uses Xvfb, Mesa software rendering, and a null ALSA output sink.
The startup diagnostic defaults to 180 seconds and bounds retained logs.
It writes `budokai2-native/startup-status.json` and `startup-verified.log` under
the working directory. Exit code **3** means a required disc module is missing;
other unverified startup outcomes return **1**.

See [development status](status.md) before interpreting a successful build as
a playable game.
