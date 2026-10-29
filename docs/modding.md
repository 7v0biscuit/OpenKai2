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
mod.

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
- **Added entries are not new roster slots yet.** The game finds characters
  and stages through internal tables that refer to entry numbers. An added
  entry is on the disc, but the game uses it only when something points at it.
  New select-screen slots need those tables mapped, which has not been done.
  Replacing an existing character's or stage's entries works now.
- Only ISO9660 disc images are read. Files that span several extents are not
  listed.

## Code layout

The mod system lives in `src/mods`:

- `afs.*` reads and lays out AFS archives.
- `disc_overlay.*` builds the virtual disc.
- `mod_library.*` handles mod folders and the load order.
- `importer.*` reads source discs and writes imports into mods.

The game reads the disc through `src/disc_image.*` (`IsoIndex`). The
`mod-disc-overlay` patch in `tools/native/patches` routes the runtime's CD
reads through the overlay. Tests are in `tests/mod_overlay_tests.cpp`.
