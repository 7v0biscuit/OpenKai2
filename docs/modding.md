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

### Movesets

With Budokai 2 as the source (**Game disc**), the **Movesets** tab adds a new
character with one character's moves and another's body:

1. On the left, **choose whose moves** the character uses. It becomes the new
   character's base, so it brings that character's record, animations, move
   effects and voice.
2. On the right, **choose whose body** it wears: the character in that spot
   lends its costume models. Nothing is copied; the roster entry names the
   body's own `DATA_CMN.AFS` entries.
3. Choose **Share a spot** or **Add a new spot**, then press **Import**.

When the two characters' skeletons differ, the sentence beside **Import** says
so: the moves may move the body oddly. Budokai 3 and Infinite World movesets
are a newer file version than Budokai 2 reads and are not converted.

### Stages and capsules

The **Stages** tab lists the source disc's stages, Budokai 2's or Budokai 3's,
with the `DATA_CMN.AFS` entries each uses. They cannot be imported yet (see
[Stages](#stages)). Capsules are not mapped in any game yet.

### Budokai 3 models

With a Budokai 3 USA disc (SLUS-20998) as the source, the list on the left
holds Budokai 3's character models, one row per form or costume, such as
"Goku, model 3". Budokai 2 draws Budokai 3 meshes all black, so the tool
brings over a model's textures and puts them on a Budokai 2 character's own
body:

1. **Choose a model** on the left.
2. On the right, **choose who wears it**. The new character is a copy of that
   Budokai 2 character, with its body, moves, animations and voice, wearing
   the Budokai 3 textures in every costume.
3. Choose **Share a spot** or **Add a new spot**, and where to save, as for
   characters, then press **Import**.

The textures are matched to the wearer's body: for each of the wearer's
textures, in order, the tool takes the next Budokai 3 texture with the same
format and size, skipping the rest, and builds a texture pack in the wearer's
layout. A model fits when every one of the wearer's textures finds one. Each model in the list says who it fits ("fits Krillin") or "no
Budokai 2 match", and choosing a model marks the spots that fit and moves to
the first. When the chosen character doesn't fit, the sentence beside
**Import** says why and Import stays off. Usually the same character in both
games fits. Budokai 3 marks its textures version 2; they are marked version 1,
the version Budokai 2 uses, and their layout matches Budokai 2's.

The disc report lists each model's part count and a check value of how its
parts link, for the work on converting Budokai 3 meshes.

#### Forcing a new character

**Force a new character** under **On the select screen** imports the whole
Budokai 3 model, mesh and textures, as a new character with a spot of its
own, whether or not it fits. On the right, choose whose moves and animations
it uses; the moves may not suit the body.

A Budokai 3 mesh block matches Budokai 2's except in its material records:
16 bytes at a 16-byte boundary holding flags, `0x29BD` and two texture
numbers. Budokai 2 sets `0x2000` in the flags (`0x21B5`); Budokai 3 leaves it
clear (`0x01B5`), and Budokai 2 drew those meshes black. Forced imports set
the flag in every Budokai 3 material record. The texture numbers follow the
model's own texture pack, which a forced import keeps. The disc report lists
Krillin's and Supreme Kai's material records in both games.

#### Comparing versions

**Compare 4 versions (test)** under **On the select screen** adds four
characters sharing the chosen spot, in this order after the character already
there, to see in game which part Budokai 2 draws wrongly:

- **D**: the wearer's own Budokai 2 mesh with the Budokai 3 textures matched
  to it, as a normal import does;
- **H**: D, keeping the wearer's own shading textures;
- **J**: the whole Budokai 3 model, its shading textures replaced by the
  wearer's;
- **A**: the whole Budokai 3 model, as a forced import uses it.

Press R2 on the spot to go through them. The message after importing, and
the `[mods] select:` lines in `startup.log`, give each version's character ID.

Up to v0.1.55 the versions were A (the whole converted model, as a forced
import uses), B (unchanged), C (the Budokai 3 mesh with the wearer's
textures) and D. Only D looked right. Setting Budokai 2's material flag
(v0.1.52) did not fix the others. With a Budokai 3 source and your Budokai 2
disc verified, **Save disc report…** also compares Supreme Kai's and
Krillin's meshes in both games word by word, listing every difference that is
not vertex data. Supreme Kai's differ only in the material flag, texture
numbers and bit `0x20` in some draw records of the mesh's first block
(`9000022C` where Budokai 3 has `9000020C`): 27 of 55 in Budokai 2. Krillin's
Budokai 2 mesh sets it in none of its 50, so that bit is a per-model setting;
v0.1.55 set it in every draw record, and v0.1.56 no longer does.

In v0.1.56 every repacked version was black, D included, while a plain copy
of a Budokai 2 character drew right. Repacking cut each texture's pixels and
palette 32 bytes short: the header gives their size without the 32 bytes in
front that send them to video memory (three zero words, a VIF `DIRECT` code
and half a GIF tag), so the last 32 bytes of every pixel block and palette
were lost, half of a 16-colour palette. v0.1.57 carries them over.

Shading textures are the 256-colour textures marked by the top bit of the
header's second word, and textures whose palette is black with only the
transparency changing. Each game shares its own across all its characters,
and the two games' differ.

### What can be imported so far

| Content    | Budokai 2 | Budokai | Budokai 3 | Infinite World |
|------------|-----------|---------|-----------|----------------|
| Characters | Yes       | Not yet | Textures  | Not yet        |
| Stages     | Listed    | Not yet | Listed    | Not yet        |
| Movesets   | Yes       | Not yet | Not yet   | Not yet        |
| Capsules   | Not yet   | Not yet | Not yet   | Not yet        |

Each game keeps its characters, stages, movesets and capsules in tables in its
executable, and those tables have to be mapped before the tool can read them.
Budokai 2's character table is mapped. For the rest, **Save disc report…**
writes `%LOCALAPPDATA%\OpenKai2\mods\reports\<serial>.txt`. The report lists
the disc's files and archives, the character tables found in its executable
with the fields that look like file numbers, every character name with its
address, and the first 32 bytes of every archive entry (24 entries for sound
archives), so the file formats can be compared with Budokai 2's. It holds no
game data beyond names, numbers and those first bytes. Send it to have that
game mapped.

The report also lists the files inside each `#AMB` container with their
first bytes, every string in the character name lists, and, for tables that
point at names, every record and the data it points at.

What the reports have shown so far about Budokai 3:

- Its archives are the same AFS format as Budokai 2's (`DATA_CMN.AFS`,
  `DATA_USA.AFS`, `ADX_USA.AFS`), and its sound is CRI ADX like Budokai 2's.
- Both games pack their data in the same containers (`#AMB`, `#AMT`, `#AMK`,
  `#SPX`, `#AMA`, `#AMM`), at the same container version. Budokai 3 also has
  `#BPL`, `#LST` and `#AMO` files. An `#AMB` starts with a 32-byte header whose
  word at 16 counts its files and whose word at 20 is where their 16-byte
  records start.
- In both games each character has one `#AMB` model per costume, about 800 KB,
  and three more files. Budokai 2's are a 200 KB `#AMB` of four files, a 5 KB
  file and a 1.4 MB `#AMB` of two; Budokai 3's are a 40 to 80 KB `#AMB` of four,
  a 5 KB file and a 1.5 to 2.4 MB `#AMB` of four.
- Budokai 3 keeps its character names in a list of 32-byte slots at
  `0x4314B0`. Its character records start at `0x447440` and are 96 bytes;
  each holds a pointer to its name, four flag bytes, two pointers to lists of
  entries and the record's number, which is also the name's slot. The first
  list holds the character's models, one per form or costume, as 12-byte
  items starting with a `DATA_CMN.AFS` entry number (Goku: 264 to 268).
- A costume model is an `#AMB` of a mesh (`#AMO`) and its textures (`#AMT`).
  The mesh has the same version in both games; Budokai 3's textures are
  version 2, which Budokai 2 never uses (it uses 0 and 1). Animations (`#AMM`)
  match; skeletons (`#BSK`) are version 3 in Budokai 2 and 4 in Budokai 3.
- An `#AMT` starts with a 32-byte header (version at 12, texture count at 16,
  offset table at 20), then one offset per texture and a 48-byte header for
  each texture.

The report includes up to three samples of each file format and version so
the differences can be worked out.

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

A filled ID also takes its base character's entries in the other tables the
game indexes by character ID, where the unused IDs hold nothing:

| Table | Entry | Covers | What it holds |
|-------|-------|--------|---------------|
| `0x376F30` | 12 bytes | IDs 0 to 40 | per form, the index of the move effects its attacks use (read at `0x20B340`); unused IDs hold -1, so their special moves draw nothing and hit nothing |
| `0x3788D0`, `0x378960` | 4 bytes | IDs 0 to 35 | voice files in `ADX_US.AFS`, as `(2 << 16) \| entry` |
| `0x377120` | 4 bytes | IDs 0 to 52 | the `DATA_US.AFS` entry of the name plate |
| `0x3A1D60` | 20 bytes | IDs 0 to 40 | hair physics per costume (read at `0x200AC0`) |

IDs 43 to 52 lie past the move-effects table, so the special moves of a
character filled there may not work; the import tool uses IDs 1, 13, 14 and
19 first and says so when it has to use a higher one.

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

The 3D preview's tables cover IDs below 37. The pose table at `0x37B3D0`
holds a 44-byte row per ID, after 16 bytes, with the preview camera; the rows
of IDs 1, 13, 14 and 19 hold a placeholder whose camera sits far too close.
Every filled ID uses its base character's row, in each of the three preview
functions that read it (the translated code at `0x2AEBC0`, `0x2AF000` and
`0x2AF4E0`), and its base's byte in the table at `0x37C0B0`. IDs 1, 13, 14 and 19 show
their own model in the preview; filled IDs 37 and up (43 to 52) show their
base character's model there, and their own in battle. The import tool uses
IDs 1, 13, 14 and 19 first.

## Stages

The USA executable keeps 13 stage records of 156 bytes at `0x373270`. A record
starts with a 32-byte name. From `0x40` it holds eight `DATA_CMN.AFS` entries:
the map (`#MDD`) at `0x40`, the `#MAD` and `#MAS` at `0x44` and `0x48`, the
`#AMB` pack at `0x4C`, the `#SPX` at `0x50`, and three more files at `0x54`
to `0x5C`. The word at `0x60` is 1 for the "B" stages (ISLAND B, TOWN B,
R&R BASE B and SNOW B), which reuse another stage's files. The nine stages
with files of their own use entries 2 to 73, eight each. The game keeps the
current stage's index at `0x373C60` and `0x373CAC`.

| Index | Name | Files (DATA_CMN) |
|-------|------|------------------|
| 0 | ISLAND | 34 to 41 |
| 1 | TOWN | 66 to 73 |
| 2 | ROOM TIME MIND | 42 to 49 |
| 3 | BUDO | 10 to 17 |
| 4 | R&R BASE | 2 to 9 |
| 5 | SNOW | 58 to 65 |
| 6 | GARDEN | 18 to 25 |
| 7 | NAMEC | 50 to 57 |
| 8 | KAIOH SHIN | 26 to 33 |
| 9 to 12 | ISLAND B, TOWN B, R&R BASE B, SNOW B | as 0, 1, 4 and 5 |

Stages cannot be imported yet: the stage select screen's list is not mapped,
so an added stage could not be chosen.

Budokai 3 keeps 17 stage records of 92 bytes at `0x44AE50`, starting with BUDO
(its seventh, GOKU, is at `0x44B078`). A record starts with a 16-byte name; the
words at `0x10` and `0x14` are `DATA_CMN.AFS` entries. The first is an `#AMB`
of the map (`#MDD`), its `#MAD` and `#MAS`, the `#SPX` and the stage's own
`#AMB` pack; the second an `#AMB` of the other three files. They are the eight
files a Budokai 2 stage names, with the same versions.

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
- `budokai3.*` reads Budokai 3's character models and checks that a model fits
  a Budokai 2 character.
- `select_alternates.*` switches shared spots with R2.
- `select_screen.*` holds the select-screen layout the patched code reads.

The game reads the disc through `src/disc_image.*` (`IsoIndex`). The
`mod-disc-overlay` patch in `tools/native/patches` routes the runtime's CD
reads through the overlay. Tests are in `tests/mod_overlay_tests.cpp`,
`tests/roster_tests.cpp` and `tests/test_patch_select_screen.py`.
