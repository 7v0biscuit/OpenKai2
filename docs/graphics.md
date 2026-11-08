# Internal resolution and HD textures

Both settings are on the launcher's **Settings** page and apply the next time
the game starts. They need the **GPU (OpenGL)** or **Vulkan** renderer; the CPU
renderer always draws at the PS2's native resolution.

## Internal resolution

| Choice | Picture size |
|---|---|
| Native | 640x448, as on a PS2 |
| 2x | 1280x896 |
| 3x | 1920x1344 |
| 4x | 2560x1792 |

3D scenes, menus and sprites are drawn at the higher resolution. When the
picture is taller than the window, the graphics card averages it down to the
window height before it is copied out, which smooths edges and keeps the copy
small. The result is scaled to the window with the chosen upscaler; when it is
still larger than the window, every upscaler except Nearest averages it down
instead of skipping pixels. The game's own memory is still drawn at
native resolution as well, so effects that read back the picture, videos and
anything the game copies with the CPU stay at native sharpness. Higher choices
need more graphics power and graphics memory; 4x uses about 400 MB.

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
