# Contributing

OpenKai2 is working toward complete Windows gameplay. Current work includes
battle loading, frame times, audio fidelity, and runtime accuracy. Check
[development status](docs/status.md) before starting runtime work.

## Development workflow

1. Follow [the build instructions](docs/building.md).
2. Keep changes focused on a specific behavior or reproducible failure.
3. Run the Python suite and native component comparisons before opening a pull
   request. For runtime changes, include the relevant startup diagnostic.
4. Describe the issue, the change, and the checks you ran in the pull request.

Use descriptive names for components and tests. Add regression coverage when a
behavior change needs it. Keep generated sources, build output, game files, and
local configuration out of commits.

For bug reports, include your operating system, toolchain versions, reproduction
steps, and the smallest useful log excerpt. Do not attach game files.

Contributions are provided under the project's GPL-3.0-only license.
