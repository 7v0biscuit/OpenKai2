# Development status

OpenKai2 boots the USA game ISO and runs the intro, menus, versus fights and
Dragon World. Current playtesting reaches roughly 60 FPS through most of the
game. The snow stage still has frame-rate drops and uneven pacing.

The main gameplay blocker is Dragon World progression: reaching the Goal after
defeating Raditz and Nappa can fail to advance the stage. Boss victories,
rewards and board movement work in the tested route; the Goal, save and
next-stage sequence are under investigation. Full story completion is not yet
verified.

See [Known issues](known-issues.md) for the current problem list and temporary
workarounds, and [Dragon World validation](dragon-world-validation.md) for the
progression checks completed so far. Previously reported character animation
distortion is resolved.

## Current priorities

1. Fix the Dragon World Goal transition and test progression through all nine
   stages, including saving, loading and the ending.
2. Restore missing dialogue and confirmation text.
3. Improve music quality and prevent tracks stopping after fights or scene
   changes.
4. Profile and improve rendering on the snow stage.

## Runtime

The game runs its recompiled EE code through PS2Recomp. Thread scheduling,
interrupts, DMA and the IOP preserve the console's execution model. Guest
instruction and callback checks use the supported executable as their reference.

VU1 microprograms use PCSX2's microVU backend. Reference comparisons cover
registers, flags, memory, timing, continuation state and graphics packets. See
[Vector execution](vector-runtime.md) for the implementation and diagnostics.

OpenGL and Vulkan render GS primitives on the GPU, with a CPU fallback. Tests
compare video memory across drawing, transfers, palettes, depth tests and
presentation. Depth-enable coverage includes 512 known-output cases across
primitive types, framebuffer and depth formats, comparisons and write masks.
See [Graphics](graphics.md) for renderer settings and supported upscalers.

## Audio

PCSX2's SPU2 core mirrors the console's register writes and transfers and mixes
voices, sound effects and music at 48 kHz. Host output includes volume control
and time stretching for slow frames.

Music still needs work: tracks can sound thin or distorted and may stop after
a fight or when returning to character selection. Occasional sound-effect
glitches are also reported. Offline mixer and buffer checks pass, but scene
transitions still need in-game testing.

## Validation and releases

Routine checks cover Python tooling, compiled guest callbacks, rendering,
movies, input, audio and persistent saves. Quick local checks are:

```sh
python -m unittest discover -s tests
python tools/probes/run.py --skip-retail
```

Packaged runtime checks exercise the executable's self-test, audio, GPU and
display paths. The release workflow checks portable launch, update recovery,
installation and uninstall on Windows before publication. Payload checksums
and embedded source revisions identify each build.

CI runs without a game disc. Complete story progression and stage-specific
performance therefore require separate gameplay testing with an owned ISO.

See [Building](building.md), [Windows setup](windows.md) and
[Release packaging](releases.md) for the build and release procedures.
