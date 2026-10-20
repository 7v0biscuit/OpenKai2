# Third-party notices

`licenses/` retains upstream license and attribution text used by Windows
development bundles. `licenses/sources.json` records the upstream locations.
The FFmpeg dependency revisions come from the `n7.1-241205` shared LGPL build
recipe used by the pinned PS2Recomp runtime.

OpenKai2's source license is [GPL-3.0-only](../LICENSE). The individual licenses
in this directory continue to apply to their respective dependencies.

The packaging tool follows the executable's DLL imports and includes only its
required dependency chain. It preserves the original development executable
when stripping debug information from the packaged copy.
