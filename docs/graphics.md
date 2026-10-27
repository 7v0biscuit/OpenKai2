# GPU rendering

OpenKai2 uses an OpenGL 4.3 compute renderer when a compatible driver is
available. It draws game geometry into emulated VRAM on the GPU. Install a
current graphics driver before launching. The CPU renderer remains available
for compatibility and comparison.

## Select a renderer

Choose the renderer on the launcher **Settings** page. The default is `auto`: use GPU rendering when initialization succeeds, otherwise
use the CPU. From PowerShell, with the extracted ZIP version:

```powershell
.\OpenKai2.exe --renderer=gpu "D:\Games\Budokai 2 USA.iso"
.\OpenKai2.exe --renderer=cpu "D:\Games\Budokai 2 USA.iso"
```

`gpu` requires successful GPU initialization and reports an error if it fails.
Both modes retain software handling for unsupported draw states. For a
self-contained launcher or installed shortcut, set `OPENKAI2_RENDERER` to `gpu`,
`cpu`, or `auto` before launching. This environment setting also works with the
extracted application. The game ISO is required in every renderer mode.

## Display settings

The launcher saves resolution, windowed/borderless display mode, VSync, FXAA,
texture filtering and nearest/bilinear/bicubic presentation scaling. The default
aspect ratio is 4:3; 16:9, stretch and native pixel aspect are also available.
FXAA and scaling operate on the completed game image. AMD FSR 1 uses the
original full-precision EASU and RCAS shaders for spatial
upscaling and sharpening. It runs after optional FXAA and clamps all reads to
the active image, excluding framebuffer padding and letterboxing. A smaller
output uses bicubic filtering. FSR processes the completed image, including UI;
it does not reduce guest CPU work or generate frames.

FSR 3/4 are not available. Temporal reconstruction requires depth, motion
vectors, jitter and history management, plus a supported backend. The current
OpenGL presentation path exposes a completed color image only. Frame generation
remains a separate future integration.

The default presentation cap is 60 FPS. Higher caps through 240 FPS and unlimited
presentation do not change the original guest vertical blank cadence. When a
host stall misses a field deadline, the pacing anchor is rebased instead of
running overdue fields in a burst. More window refreshes do not imply more
completed game frames.

Filtered output is reused while the completed game frame is unchanged. FXAA,
bicubic and FSR passes run again after a new completed frame, crop, filter or
output-size change. Nearest and bilinear presentation without FXAA retain the
direct path. The guest clock and input polling remain independent of this cache.
On shutdown, `[display:filters]` reports processed and reused filtered images;
these counts are separate from completed game frames and window refreshes.

## Checks and diagnostics

The extracted application's GPU comparisons run without game files:

```powershell
.\OpenKai2.exe --gpu-self-test
.\OpenKai2.exe --display-self-test
```

Results are written to `%LOCALAPPDATA%\OpenKai2\logs\gpu-test.log`. This
command requires GPU initialization and actual compute dispatch; it fails if
GPU output differs from the software reference. Normal startup records the
selected renderer and device. On shutdown, `[renderer:gpu]` reports GPU and
software draw counts, submitted batches, tile counts, and upload/readback bytes.
`culled_draws` counts repeated-vertex and axis-flat triangles omitted before
GPU binning. Display checks compare all FXAA/scaler combinations using actual OpenGL
output, including orientation, cropping, letterboxing, unchanged-frame reuse
and cache invalidation after new frames or resizing.
These are diagnostic counts, not a game FPS measurement.

## Compatibility and performance

The renderer supports direct and indexed textures, point and bilinear sampling,
32-, 24-, and 16-bit color/depth targets, texture functions, fog, blending,
alpha/depth tests, and framebuffer masks. Buffer feedback, overlapping color
and depth targets, scan masking, and other unsupported states use the CPU
compatibility path. Presentation reads only the color pages required by the enabled display
circuits, their origins and any candidate presentation sources. Depth and
unrelated targets remain resident until an explicit CPU consumer needs them.
A controlled color/depth/offscreen fixture reads 16 KiB for presentation,
compared with 256 KiB before this change. Game FPS still requires measurement. Texture page footprints are reused while addressing state
is unchanged. Aligned buffers use direct page ranges, and pixel updates,
texture uploads, and palette loads read back only their affected pages. Other
dirty GPU pages remain resident until a consumer needs them.

Triangle depth uses differences from one vertex, retaining exact integer depth
for flat surfaces. Large common depths no longer inherit rounding errors from
three separately weighted values. Constant-depth and overlapping-plane checks
use independent expected depths on both rendering paths.

Packed GIF packets keep temporary STQ.Q separate from the latched RGBAQ.Q
attached to vertices. Only packed RGBA copies the temporary Q; a new tag resets
the temporary without changing the vertex value. Direct and REGLIST RGBAQ
writes retain their explicit Q. Packed XYZ2 and XYZ3 depths retain all 32 bits
before interpolation. Independent packet checks exercise both packet parsers,
register ordering, chained tags, reset, and depths beyond float32 precision.

Guest execution and vector instruction processing still consume CPU time.

Complete matches and sustained 60 FPS remain unverified. The automated GPU
comparisons passed with Mesa on Linux and Windows under Wine. Software OpenGL
timings do not predict hardware game performance. Host audio remains disabled.

## Implementation reference

PCSX2's [v2.1.27 graphics source](https://github.com/PCSX2/pcsx2/tree/v2.1.27/pcsx2/GS)
was reviewed for buffer hazards and texture-cache invalidation behavior.
OpenKai2 implements the compute backend in `src/render/`. VU1 execution uses
[PCSX2's microVU recompiler](vector-runtime.md). Dependency attribution is
recorded in [NOTICE](../NOTICE).

Developers can run the required headless comparisons with
`python tools/native/test-gpu-linux.py --jobs 2` on Linux with Mesa/EGL and the
native build dependencies installed. The display-filter checks also require
Xvfb and xauth on Linux. The optional `openkai2_gpu_bench` CMake
target compares ordered batches with the CPU renderer and checks output hashes.
Its `--footprints` mode measures CPU page preparation; `--submission` measures
small textured draw batches, waits for GPU completion, and compares complete
VRAM with the CPU reference. These synthetic timings are not game FPS.

## Widescreen and image clarity

The 16:9 mode applies a horizontal camera correction for the USA executable.
It uses the same 3/4 projection factor as the maintained
[PCSX2 widescreen reference](https://github.com/PCSX2/pcsx2_patches/blob/main/patches/SLUS-20779_1FB1F8E2.pnach)
by Catarax, applied at the original camera scale load. Other aspect modes keep the original camera.
The 640×448 movie surface retains its original TV proportions. **Stretch to
window** remains available as a separate display choice.

The launcher’s **Clear image preset** selects bicubic scaling, original texture
filtering and no FXAA. It preserves resolution, aspect ratio, frame limit,
VSync and save/update preferences. Spatial upscaling cannot recover detail
absent from the original image.

Completed-frame timing is reported separately from window redraw counts. The
`performance:frames` line records measured presentation rate, frame-interval
percentile upper bounds, the longest interval and intervals exceeding 25 ms.
These measurements help distinguish pacing spikes from an average frame rate.
