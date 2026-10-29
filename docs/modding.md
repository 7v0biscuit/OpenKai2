# Modding

Mods change the game's files without touching your ISO. When the game starts,
OpenKai2 reads the enabled mods and builds a virtual disc on top of the ISO:
the game sees the modded files at their usual paths, and the ISO itself stays
unchanged. Turn a mod off and the game is back to the original disc.

Characters, movesets and stages are stored as numbered entries in the disc's
AFS archives. A mod can replace entries, add new ones or replace whole files.
It can add any number of entries, and any number of mods can be enabled.

## The Mods page

The launcher's **Mods** page (Ctrl+3) lists every mod in load order:

- **Enable / disable** switches the selected mod. Double-clicking it does the
  same.
- **Move up** and **Move down** change the load order. Mods load top to bottom
  and later ones win where two change the same thing.
- **Create mod** makes an empty mod with the name you type.
- **Open mods folder** opens `%LOCALAPPDATA%\OpenKai2\mods`.
- **Import tool…** opens the import tool.

Changes apply the next time the game starts. While the game runs, mods are
read-only. The startup log lists the mods it applied, in `[mods]` lines.

## Import tool

The import tool adds content from Dragon Ball Z games to Budokai 2. It reads
Budokai, Budokai 2, Budokai 3 and Infinite World discs and refuses other discs.
Imports only ever add: every character already in Budokai 2 stays on the select
screen.

Verify your Budokai 2 ISO on the **Play** page first: the tool opens with it as
the source. Use **Choose ISO…** to read another disc and **Game disc** to go
back. The tool names the game it found beside the disc. Reading discs and
importing run in the background, with a progress bar and percentage.

At the top, choose what to add: **Characters**, **Stages**, **Movesets** or
**Capsules**.

### Characters

1. **Choose a character** from the list on the left. The search box filters it
   by name. Every file the character's record names is imported together.
2. On the right, choose a spot on the select screen. Under **On the select
   screen**, choose how the character appears:
   - **Share a spot**: press R2 (Right Shift) on that spot to switch between
     the characters it holds.
   - **Add a new spot (experimental)**: it gets a spot of its own before
     Random, showing the portrait of the spot you chose.
3. Under **Save to**, keep **New mod** or pick an existing mod. A new mod is
   named after the character.
4. Read the sentence beside **Import**, which says what will happen, then
   press **Import**. The character fills an unused character ID.

### What can be imported so far

| Content    | Budokai 2 | Budokai | Budokai 3 | Infinite World |
|------------|-----------|---------|-----------|----------------|
| Characters | Yes       | Not yet | Not yet   | Not yet        |
| Stages     | Not yet   | Not yet | Not yet   | Not yet        |
| Movesets   | Not yet   | Not yet | Not yet   | Not yet        |
| Capsules   | Not yet   | Not yet | Not yet   | Not yet        |

Each game keeps its characters, stages, movesets and capsules in tables in its
executable, and those tables have to be mapped before the tool can read them.
Budokai 2's character table is mapped. For the rest, **Save disc report…**
writes `%LOCALAPPDATA%\OpenKai2\mods\reports\<serial>.txt`. The report lists
the disc's files and archives, the character tables found in its executable
with the fields that look like file numbers, and the first 16 bytes of archive
entries, so the file formats can be compared with Budokai 2's. It holds no game
data beyond names, numbers and those first bytes. Send it to have that game
mapped.

Data from another game is not converted. Budokai 3 and Infinite World use a
newer version of the engine, and their files may need converting before
Budokai 2 can load them; the report's file signatures show whether they do.

## Characters and the roster

The USA executable keeps 53 character records at `0x389D40`, 712 bytes each,
indexed by character ID. Each record starts with a 32-byte name. The u32
fields from `0x74` to `0x1E0` hold the character's `USR/DATA_CMN.AFS` entries;
unused fields hold 0 or -1. The record's own name for each field is unknown; the numbering suggests
models, animations and select-screen art, which needs confirming in game. The
game loads files by `(archive << 16) | entry`, and archive 0 is
`DATA_CMN.AFS`.

The select screen is a wheel of spots. The character in each spot is at
`0x3A4140`: 29 spots, then Random (`0x3F`). The spot for each character ID is
at `0x3A4100`; the screen unlocks spots through it and opens with the cursor
on the spot of the last character chosen. Each spot shows sprite 36 + its
number from one sprite sheet, `DATA_CMN` entry 901.

IDs 1, 13, 14, 19 and 43 to 52 have names but no data, and the game never
shows them. A mod fills one with `roster.ini`:

```
[character]
slot = 13                  ; the ID to fill
base = 0                   ; copy this character's record first (Goku)
name = SSJ4 GOKU           ; up to 31 characters
file.74 = add:ssj4_body.bin ; a DATA_CMN entry this mod adds
file.1dc = 412             ; or an entry number
f32.20 = 9.0               ; also u8., u16. and u32., at a hex record offset
select = 5                 ; take over spot 5 (0 to 28)
alternate = 5              ; or share spot 5; R2 switches between them
new_cell = yes             ; or add a spot of its own before Random
icon = 5                   ; the spot whose portrait a new spot shows
```

The changes are made in memory after the executable loads and are listed in
the startup log as `[mods] roster:` and `[mods] select:` lines. Later mods win.

A character goes on the select screen in one of three ways:

- **`alternate`** shares a spot. Press R2 (Right Shift on the keyboard) on the
  spot to switch between the characters it holds. Nothing in the game's code
  changes. The spot keeps its original portrait.
- **`select`** takes over a spot. The character that was there stays in the
  game but cannot be picked on the select screen. The import tool never does
  this; it is for mods made by hand.
- **`new_cell`** adds a spot to the wheel, before Random, for up to 34 extra
  spots. New spots are always unlocked and show the portrait of the spot
  named by `icon`, or of their base character's spot. This changes the select
  screen's code (`tools/native/patch-select-screen.py`). It is experimental:
  report anything odd on the select screen, with `startup.log`.

The 3D preview's tables cover IDs below 37. Filled IDs 37 and up (43 to 52)
show their base character in the preview, so the new character's own model
appears in battle. IDs 1, 13, 14 and 19 preview as themselves; the import
tool uses them first.

## Mod folder layout

A mod is a folder in the mods folder. You can build one by hand:

```
mods/
  load-order.txt               +Enabled_mod / -Disabled_mod, one per line
  My_mod/
    mod.ini                    name=, author=, version=, description=
    files/USR/DATA.BIN         replaces USR/DATA.BIN
    afs/USR/DATA_US.AFS/
      0412_goku.bin            replaces entry 412; text after the number is free
      add/new_stage.bin        appended as a new entry, in name order
    imports.txt                written by the import tool
    roster.ini                 character slots, see above
```

- Disc paths match without regard to case.
- An entry number past the end of the archive stops the game with an error
  that names the mod file. Use `add/` to grow an archive.
- When an entry still fits in the original space, it is written in place.
  Otherwise the archive is rebuilt past the end of the disc image, and the
  directory records that point at it are updated.
- A whole-file replacement under `files/` replaces the archive. Entry edits
  from mods loaded before it are dropped; entry edits from mods loaded after
  it apply on top.

## Limits

- **There are 14 unused IDs to fill**, so 14 added characters at most.
- **New spots borrow a portrait.** The sprite sheet has one portrait per
  original spot; adding portraits needs the sheet's format mapped.
- **Not every character table is mapped.** Data the record does not name, such
  as voices, story scenes and skill capsules, stays the base character's.
- **Stages, movesets and capsules are not mapped yet**, in any game. Stage and
  capsule names are images and text on the disc, not in the executable.
- Only ISO9660 disc images are read. Files that span several extents are not
  listed.

## Code layout

The mod system lives in `src/mods`:

- `afs.*` reads and lays out AFS archives.
- `disc_overlay.*` builds the virtual disc.
- `mod_library.*` handles mod folders and the load order.
- `importer.*` reads source discs and writes imports into mods.
- `roster.*` reads and applies `roster.ini` and reads a disc's characters.
- `disc_identity.*` tells which Dragon Ball game a disc holds and writes disc
  reports.
- `select_alternates.*` switches shared spots with R2.
- `select_screen.*` holds the select-screen layout the patched code reads.

The game reads the disc through `src/disc_image.*` (`IsoIndex`). The
`mod-disc-overlay` patch in `tools/native/patches` routes the runtime's CD
reads through the overlay. Tests are in `tests/mod_overlay_tests.cpp`,
`tests/roster_tests.cpp` and `tests/test_patch_select_screen.py`.
