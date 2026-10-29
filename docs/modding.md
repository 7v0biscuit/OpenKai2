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

The import tool copies characters from a PS2 disc, or files of your own, into
a mod. Verify your Budokai 2 ISO on the **Play** page first: the tool opens
with it as the source. Use **Choose ISO…** to read another disc and **Game
disc** to go back.

At the top, choose what to do: **Add a character**, **Replace a character** or
**Copy files**. Reading discs and importing run in the background, with a
progress bar and percentage.

### Add or replace a character

1. **Choose a character** from the list on the left. The search box filters it
   by name.
2. On the right, choose where it goes:
   - **Add a character**: a spot on the select screen. The character gets an
     unused character ID. Under **On the select screen**, choose how it
     appears:
     - **Share the spot**: press R2 on that spot to switch characters.
     - **Take over the spot**: it replaces the character shown there.
     - **Add a new spot (experimental)**: it gets a spot of its own, and the
       spot you chose gives the portrait.
   - **Replace a character**: the character whose files it replaces, file for
     file.
3. Under **Save to**, keep **New mod** or pick an existing mod. A new mod is
   named after the character.
4. Read the sentence beside **Import**, which says what will happen, then
   press **Import**.

Characters are read from Budokai 2 USA discs. Other discs, including Budokai
3, can only use **Copy files**, because their character tables are not mapped
yet.

### Copy files

For archives and files of any disc:

1. Pick a file on the left. For an archive, its entries are listed on the
   right with their numbers, names and sizes; type in the filter box to search
   them. Ctrl-click or Shift-click to choose several entries.
2. Choose where the data goes:
   - **Mod**: an existing mod, or **New mod** to create one named after the
     source disc.
   - **Content**: character, moveset, stage or other. This is recorded in the
     mod's `imports.txt` so you can tell what it holds.
   - **Action**:
     - **Add as new entries** appends to the archive.
     - **Replace entries** overwrites entries in order, starting at
       **First entry**.
     - **Replace whole file** swaps the game file.
   - **Game file**: the archive or file on your Budokai 2 disc. When the
     source disc has a file at the same path, it is chosen for you.
3. Press **Import**.

**Import file from disk…** imports a file you made or edited yourself, using
the same targets. **Extract selected…** saves the chosen entries as files, so
you can edit them in other tools and import them back.

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
  game but cannot be picked on the select screen.
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

- **Data from Budokai 3 is copied as is.** Budokai 3 uses formats and tables
  that differ from Budokai 2 in places, so a ported character or stage may not
  load or may look wrong. Test it, and keep the mod disabled if it breaks.
- **There are 14 unused IDs to fill**, so 14 added characters at most.
- **New spots borrow a portrait.** The sprite sheet has one portrait per
  original spot; adding portraits needs the sheet's format mapped.
- **Not every character table is mapped.** Data the record does not name, such
  as voices, story scenes and skill capsules, stays the base character's.
- **Stages and capsules are not listed by name yet.** Their names are images
  and text on the disc, not in the executable. Edit them in the file view.
- Only ISO9660 disc images are read. Files that span several extents are not
  listed.

## Code layout

The mod system lives in `src/mods`:

- `afs.*` reads and lays out AFS archives.
- `disc_overlay.*` builds the virtual disc.
- `mod_library.*` handles mod folders and the load order.
- `importer.*` reads source discs and writes imports into mods.
- `roster.*` reads and applies `roster.ini` and reads a disc's characters.
- `select_alternates.*` switches shared spots with R2.
- `select_screen.*` holds the select-screen layout the patched code reads.

The game reads the disc through `src/disc_image.*` (`IsoIndex`). The
`mod-disc-overlay` patch in `tools/native/patches` routes the runtime's CD
reads through the overlay. Tests are in `tests/mod_overlay_tests.cpp`,
`tests/roster_tests.cpp` and `tests/test_patch_select_screen.py`.
