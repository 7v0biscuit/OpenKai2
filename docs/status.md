# Development status

The runtime reaches the title screen, menus, and versus level selection.
Battle loading, frame rates, and audio fidelity remain under development.
Complete matches and stable 60 FPS have not been verified.

The original Budokai 2 USA ISO is required at every game launch. Automated
checks cover bounded runtime behavior and Windows packaging; full retail
compatibility requires separate validation.

## Battle asset loading

Battle startup uses an eleven-stage initialization table. Its entries are
registered explicitly, with the original table and initializer instructions
verified against the supported executable.

The subsequent asset dispatcher calls two additional relocation routines:
BCM animation data at `0x001D21D0` and BSK skeleton data at `0x001D2100`.
The missing BCM entry previously left the main guest thread dormant after
stage selection. Both routines now have explicit translation entries.

Regressions execute the original tag dispatcher, including its nested pointer
updates, variable record sizes, null pointers, empty records, and completion
markers. Direct and uncached aliases retain their object-relative addresses.
Already relocated assets are left unchanged, and caller frames and adjacent
memory guards are checked. These fixtures do not establish complete matches.

## Interpreter performance

The IOP scheduler now skips periods with no runnable thread or pending event
until the next known wake, timer, DMA, or callback deadline. Operations that can
wake a thread invalidate that deadline. Fractional EE-to-IOP clock conversion
and guest interrupt timing remain unchanged.

Instruction fetches are shared between import detection and execution. Thread
selection combines its wake and priority scans. Diagnostic CPU snapshots and
import text formatting are deferred until needed instead of being copied or
formatted at each instruction or import.

An isolated benchmark using two million single-cycle scheduling charges measured
these median times:

| Workload | Previous | Updated |
| --- | ---: | ---: |
| Sleeping IOP threads | 67.67 ms | 9.62 ms |
| Busy interpreted thread | 97.24 ms | 65.43 ms |

Both versions executed the same guest cycle and instruction counts. These
measurements cover the interpreter; they are not game-FPS results. Graphics
still use the CPU renderer, and sustained 60 FPS requires a frame budget of
about 16.7 ms across the complete runtime.

## Audio

SPU2 AutoDMA PCM reaches a 48 kHz stereo host stream through raylib's miniaudio
backend. Ordinary compressed voice/sample DMA is excluded from the PCM path.
A 2,048-frame prefill buffers delivery jitter. Short ramps soften startup,
starvation, and stream replacement boundaries while retaining sample rate and
channel order. After queue overflow, the callback discards old buffered latency.
It does not repeat or time-stretch missing audio.

Diagnostics report underruns, frames without queued input, producer drops, and
consumer latency trimming. PCM output does not yet provide complete SPU2 voice
synthesis, volume/effect behavior, or retail audio fidelity. Runtime slowdown
can still cause audible gaps; buffering alone cannot restore real-time speed.

## Validation

The Python suite has 164 tests. Native checks include the original BCM/BSK
asset dispatcher, all eleven battle initialization entries, fragmented and bulk
IOP clocks, delayed wakeups, external RPC delivery, both SPU2 DMA cores, planar
PCM order, partial blocks, queue boundaries, and concurrent delivery.

Renderer checks retain scalar blend comparisons, serial/parallel triangle and
sprite snapshots, texture filtering, feedback, and depth behavior. Movie, DMA,
scheduler, input, and persistent-save regressions remain enabled.

The audio-device diagnostic delivered 48,000 stereo frames through a Linux
callback using an ALSA null output. That validates callback delivery, not
physical playback fidelity. Windows release checks cover the portable launcher,
ZIP, installed application's full self-test, ISO rejection, and uninstall.

See [Windows setup and controls](windows.md), [building](building.md), and
[release packaging](releases.md).
