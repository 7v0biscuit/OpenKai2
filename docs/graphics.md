# Internal resolution and HD textures

Both settings are on the launcher's **Settings** page and apply the next time
the game starts. They need the **GPU (OpenGL)** or **Vulkan** renderer; the CPU
renderer always draws at the PS2's native resolution.

## Internal resolution

| Choice | Picture size |
|---|---|
| Auto (match display) | Picked from your display, see below. This is the default |
| Native | 640x448, as on a PS2 |
| 2x | 1280x896 |
| 3x | 1920x1344 |
| 4x | 2560x1792 |

**Auto** picks the smallest choice that covers the height the game is shown
at: the window height, or the screen height in borderless mode. A 720p window
gets 2x, 1080p gets 3x, and 1440p or 4K gets 4x. With HD textures on it never
picks less than 2x. `startup.log` records the choice as
`[display] internal_scale=auto display_height=1080 chosen=3x`. If you updated
from a version before Auto existed and had never changed this setting from
Native, it is switched to Auto once; choose Native again to keep the PS2 picture.

3D scenes, menus and sprites are drawn at the higher resolution. Pictures the
game draws and then reuses as a texture, such as full-screen copies and
effects, keep the higher resolution too. When the
picture is taller than the window, the graphics card averages it down to the
window height before it is copied out, which smooths edges and keeps the copy
small. The result is scaled to the window with the chosen upscaler; when it is
still larger than the window, every upscaler except Nearest averages it down
instead of skipping pixels. The game's own memory is still drawn at
native resolution as well, so effects that read back the picture, videos and
anything the game copies with the CPU stay at native sharpness. Higher choices
need more graphics power and graphics memory; 4x uses about 400 MB. If the
graphics card cannot hold the chosen size at all, the game uses the largest
size that fits and notes it in `startup.log`
(`internal_resolution=4x does not fit this GPU; using 3x`).

If you choose Native, 2x, 3x or 4x yourself, the game keeps exactly that
resolution for the whole session, even when it runs below 60 FPS. With **Auto**
the chosen resolution is a maximum: when the graphics card cannot keep up and
the game starts falling behind 60 FPS (the card is busy more than 85% of the
time and more than a tenth of frames are late, for two seconds in a row), the
game lowers the internal resolution by one step, down to Native if needed.
When the card has had plenty of headroom for 15 seconds, it goes back up one
step, but not sooner than 20 seconds after a step down; if a step up has to be
undone within 30 seconds, it waits twice as long before trying again, up to
five minutes. Each change is written to `startup.log` as
`[renderer] internal_resolution lowered to 2x to keep 60 FPS` or
`[renderer] internal_resolution raised to 3x`. Frame drops caused by the CPU
rather than the graphics card do not change the resolution, since lowering it
would not help.

`startup.log` reports `[renderer] internal_resolution=2x enabled` at start, and
every 600 frames a `[renderer:hires]` line counts frames shown at the higher
resolution (`frames_scaled`) and frames that fell back to native
(`frames_native`, with the reason).

## HD textures

Needs an internal resolution of 2x or higher. Choosing an HD texture mode while
internal resolution is Native sets it to 2x; choosing Native turns HD textures
off.

Textures are recognised by a hash of their memory pages, so a texture the game
uploads again unchanged is matched without reading it again. At most 262,144
new texels are read per frame; textures beyond that keep their original look
for a frame and are picked up on the next. **Texture packs** with no PNG files
in the textures folder turns itself off.

| Choice | What it does |
|---|---|
| Off | The game's textures as they are |
| Upscaled from your disc | Each texture is upscaled while you play (2x, or 4x at 3x and 4x internal resolution). Nothing is downloaded |
| Texture packs | PNG files in the textures folder replace the game's textures |
| Texture packs, upscale the rest | Packs where a file exists, upscaling for everything else |
| Dump textures for pack makers | Saves every texture the game uses once, as a PNG |

**Open texture folder** opens `%LOCALAPPDATA%\OpenKai2\textures`.

In game, **F4** switches HD textures off and on, so you can compare them with
the original textures. A message shows how many textures have been replaced
from packs or upscaled. Textures the game draws itself, such as reflections and
some effects, keep their original look.

## Anti-aliasing

| Choice | What it does |
|---|---|
| Off | No edge smoothing |
| FXAA | Fast smoothing of the sharpest edges |
| FXAA High | Follows each edge a few pixels further, so long edges are smoother |
| FXAA Ultra | Follows edges furthest and also smooths small details; the softest picture |

Anti-aliasing runs before the picture is scaled to the window. Internal
resolution 2x or higher also smooths edges, because the picture is averaged
down to the window.

## Upscaling

The upscaler scales the game picture to the window when the picture is smaller
than the window, for example at Native in a large window. When the picture is
larger than the window, as with Auto internal resolution, it is averaged down
instead; with **AMD FSR 1** chosen, FSR's sharpening pass then restores detail
lost in the averaging, so the choice still matters at high internal
resolutions.

## Image effect

| Choice | What it does |
|---|---|
| None | The picture as drawn |
| Sharpen | Strengthens fine detail and outlines without halos: each pixel is pushed away from its neighbours but never past their brightest or darkest value |
| Vibrant colors | Raises the saturation of dull colors more than already strong ones, with a slight contrast lift |
| CRT scanlines | Dark gaps between the game's 448 lines and a gentle vignette, like a CRT television |

The effect is the last step, after anti-aliasing and scaling, and works with
every renderer. It is in **Settings > Graphics** and applies at the next launch.

### Making a texture pack

1. Choose **Dump textures for pack makers** and play the parts of the game you
   want to change. Each texture is saved once to `textures\dump`, named by a
   16-digit hash of its pixels and palette, for example `3f9a0c12d4e5b678.png`.
2. Copy a file out of `dump`, edit it at any size (2x, 4x or 8x the original
   keeps texels square), and keep the file name.
3. Put the file anywhere in the textures folder except `dump`. Subfolders are
   fine, so a pack can be its own folder.
4. Choose **Texture packs** or **Texture packs, upscale the rest**.

Packs only contain your own edits. Do not share textures taken from the game.

Textures the game draws into itself (render targets) and textures that use the
PS2's region-repeat addressing are not replaced.
