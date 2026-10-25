# GPU rendering

OpenKai2 uses an OpenGL 4.3 compute renderer when a compatible driver is
available. It draws game geometry into emulated VRAM on the GPU. Install a
current graphics driver before launching. The CPU renderer remains available
for compatibility and comparison.

## Select a renderer

The default is `auto`: use GPU rendering when initialization succeeds, otherwise
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

## Checks and diagnostics

The extracted application's GPU comparisons run without game files:

```powershell
.\OpenKai2.exe --gpu-self-test
```

Results are written to `%LOCALAPPDATA%\OpenKai2\logs\gpu-test.log`. This
command requires GPU initialization and actual compute dispatch; it fails if
GPU output differs from the software reference. Normal startup records the
selected renderer and device. On shutdown, `[renderer:gpu]` reports GPU and
software draw counts, submitted batches, tile counts, and upload/readback bytes.
These are diagnostic counts, not a game FPS measurement.

## Compatibility and performance

The renderer supports direct and indexed textures, point and bilinear sampling,
32-, 24-, and 16-bit color/depth targets, texture functions, fog, blending,
alpha/depth tests, and framebuffer masks. Buffer feedback, overlapping color
and depth targets, scan masking, and other unsupported states use the CPU
compatibility path. Presentation reads completed VRAM back for the existing
display conversion. Texture page footprints are reused while addressing state
is unchanged. Aligned buffers use direct page ranges, and pixel updates,
texture uploads, and palette loads read back only their affected pages. Other
dirty GPU pages remain resident until a consumer needs them.

Guest execution and vector instruction processing still
consume CPU time.

Complete matches and sustained 60 FPS remain unverified. The automated GPU
comparisons passed with Mesa on Linux and Windows under Wine. Software OpenGL
timings do not predict hardware game performance. Host audio remains disabled.

## Implementation reference

PCSX2's [v2.1.27 graphics source](https://github.com/PCSX2/pcsx2/tree/v2.1.27/pcsx2/GS)
was reviewed for buffer hazards and texture-cache invalidation behavior.
OpenKai2 implements its own compute backend in `src/render/`; no PCSX2 runtime
or binaries are included. Existing dependency attribution is recorded in
[NOTICE](../NOTICE).

Developers can run the required headless comparisons with
`python tools/native/test-gpu-linux.py --jobs 2` on Linux with Mesa/EGL and the
native build dependencies installed. The optional `openkai2_gpu_bench` CMake
target compares ordered batches with the CPU renderer and checks output hashes.
Its `--footprints` mode measures CPU page preparation; `--submission` measures
small textured draw batches, waits for GPU completion, and compares complete
VRAM with the CPU reference. These synthetic timings are not game FPS.
