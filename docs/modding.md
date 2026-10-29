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

The import tool copies data from a PS2 disc, or from files of your own, into a
mod. Reading discs, importing and extracting run in the background, with a
progress bar and percentage.

### Characters

For a Budokai 2 USA disc, the tool reads the disc's own character tables and
lists characters by name. Choosing one selects every file its record names,
so they travel together. Then pick an action:

- **Add as new character** copies the files as new entries and fills an unused
  character ID with them. Choose the **Select cell** the character appears in;
  the screen has 29 cells, so it takes over that cell from the character shown
  there. **Roster slot** shows the ID it will use.
- **Replace a character** writes the files over another character's, file for
  file. Files that have no counterpart are left out and counted in the result.

Switch the list to **All files** to work with raw archive entries instead.
Other discs, including Budokai 3, open in the file view: their character
tables are not mapped yet.

### Files

Steps for the file view:

1. Pick the source. **Game disc** reads your verified Budokai 2 ISO and
   **Choose ISO…** reads any other PS2 ISO, such as Budokai 3.
2. Pick a file on the left. For an archive, its entries are listed on the
   right with their numbers, names and sizes; type in the filter box to search
   them. Ctrl-click or Shift-click to choose several entries.
3. Choose where the data goes:
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
4. Click **Import selected**.

**Import file from disk…** imports a file you made or edited yourself, using
the same targets. **Extract selected…** saves the chosen entries as files, so
you can edit them in other tools and import them back.

The game disc must be verified on the **Play** page before importing, because
the targets come from it.

## Characters and the roster

The USA executable keeps 53 character records at `0x389D40`, 712 bytes each,
indexed by character ID. Each record starts with a 32-byte name. The u32
fields from `0x74` to `0x1E0` hold the character's `USR/DATA_CMN.AFS` entries;
unused fields hold 0 or
-1. The record's own name for each field is unknown; the numbering suggests
models, animations and select-screen art, which needs confirming in game. The
game loads files by `(archive << 16) | entry`, and archive 0 is
`DATA_CMN.AFS`. The select grid is at `0x3A4140`: one ID per cell for 29 cells,
then Random (`0x3F`).

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
select = 5                 ; the grid cell, 0 to 28
```

The changes are made in memory after the executable loads and are listed in
the startup log as `[mods] roster:` lines. Later mods win.

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
- **The select screen has 29 cells.** A new character takes over a cell; the
  character that was there stays in the game but cannot be picked on that
  screen. Adding cells needs the select screen's code changed, which has not
  been done. There are 14 unused IDs to fill.
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

The game reads the disc through `src/disc_image.*` (`IsoIndex`). The
`mod-disc-overlay` patch in `tools/native/patches` routes the runtime's CD
reads through the overlay. Tests are in `tests/mod_overlay_tests.cpp` and `tests/roster_tests.cpp`.
