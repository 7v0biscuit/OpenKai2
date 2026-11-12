# Development status

OpenKai2 boots the USA game ISO and runs the intro, menus, versus fights and
Dragon World. Gameplay has been verified through completion in playtesting,
including all nine Dragon World stages and the ending. Current playtesting
reaches roughly 60 FPS through the game.

Music playback and mod tools are the remaining active issue areas. See
[Known issues](known-issues.md) for details and workarounds, and
[Dragon World validation](dragon-world-validation.md) for the completion record
and targeted regression coverage.

## Current priorities

1. Prevent music pops and tracks failing to resume after fights or scene
   changes.
2. Match imported Budokai 3 moveset buttons to the correct moves and extend
   support for moving stage objects, stage-wide effects and capsule imports.

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

A crackle that started a few seconds into playback and then persisted is
fixed: when the game's audio rate and the sound card's clock differed slightly,
the resampler lost its previous sample each time its buffer was tidied and
produced a small click many times a second. A regression test covers clock
differences in both directions. Music may still fail to load after a fight or
when returning to character selection; this has not been reproduced, and
`startup.log` now records when the music stream resumes after silence and
whether the sound chip ran out of stream data.

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

CI runs without a game disc. The full-game completion record comes from
playtesting with an owned ISO; automated checks cover specific runtime and
launcher behavior.

See [Building](building.md), [Windows setup](windows.md) and
[Release packaging](releases.md) for the build and release procedures.
