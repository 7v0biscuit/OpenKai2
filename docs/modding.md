# Modding

Mods change the game's files without touching your ISO. When the game starts,
OpenKai2 reads the enabled mods and builds a virtual disc on top of the ISO:
the game sees the modded files at their usual paths, and the ISO itself stays
unchanged. Turn a mod off and the game is back to the original disc.

Characters, movesets and stages are stored as numbered entries in the disc's
AFS archives. A mod can replace entries, add new ones or replace whole files.
It can add any number of entries, and any number of mods can be enabled.

## The Mods page

The launcher's **Mods** page (Ctrl+4) lists every mod in load order:

- **Enable / disable** switches the selected mod. Double-clicking it does the
  same.
- **Move up** and **Move down** change the load order. Mods load top to bottom
  and later ones win where two change the same thing.
- **Create mod** makes an empty mod with the name you type.
- **Open mods folder** opens `%LOCALAPPDATA%\OpenKai2\mods`.
- **Import tool…** opens the import tool.
- **Export mod…** saves the selected mod as one `.okmod` file to share.
- **Install mod…** adds a mod from a `.okmod` or `.zip` file. The mod is
  enabled and loads last. A package may hold the mod's folder or its contents;
  files are checked against the package's checksums, and paths that would land
  outside the new mod's folder are refused.

When two enabled mods change the same disc file, archive entry, character slot
or stage slot, or one replaces a whole archive another edits, the list says so:
the later mod shows "overrides" with the earlier one's name and how many
changes it takes over, and the earlier one shows "partly overridden by". The
later mod wins, so move one up or down, or disable it, to choose.

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
are a newer file version than Budokai 2 reads and are not converted yet.

What is known about converting Budokai 3 movesets:

- A move script (`#BSK`) is a table of entries, each a 48-byte record: a
  motion as `bank << 16 | motion`, flags, then a command count and offset.
  Commands are 8 bytes: `size << 16 | type` (types 0 to 7, the same in both
  games) and an offset to `size` 16-byte items. Budokai 2 uses version 3
  (Goku: 1,336 entries); Budokai 3 version 4 (about 2,000), with the same
  record and command layout and mostly identical items.
- Budokai 2 keeps a character's whole move script in one file (Goku's
  `DATA_CMN` 173, with `#AMC`, `#BCM` and `#SPX`), its own motions in another
  (175), and reads motions from bank 1 (its own, 0 to 165 for Goku) and bank 0
  (shared by everyone).
- Budokai 3 splits it: `DATA_CMN` 165 holds the moves every character shares
  (359 motions and a `#BSK` of 2,172 entries) and each character's file holds
  only its own (Goku: 262, motions in bank 3, 0 to 94).
- Entry numbers mostly mean the same action in both games: 139 of the 302
  entries both Goku files use have the same flags and command types, and 53
  more match one to three entries away.

**Force a new character with its Budokai 3 moves (experimental)**, under
**On the select screen** with a Budokai 3 model chosen, does this conversion:

- Motions: a motion pack (`#AMM`) starts with its count, a table of 16-byte
  entries (the last word is the motion's offset), the bone count at `0x18`
  and, at `0x1C`, the offset of a list of 32-byte bone names. Each motion
  starts with 8 bytes per bone: offsets from the pack's start to that bone's
  tracks. The shared and the character's own motions are joined into one
  pack in the model's bone order, matched by bone name without its prefix
  (`GOK_LARM1` and `GNY_LARM1` are the same bone); bones a motion lacks stay
  still, and every offset is moved to the motion's new place.
- Move script: the shared and own entries are merged (the character's own
  win), marked version 3, and their motions point at bank 1: shared motion
  `n` stays `n`, own motion `n` becomes `n` plus the shared count. The
  character's 0x6A00-byte block of settings at the end is kept.
- Files: the character's `#AMC`, `#BCM` and `#SPX` file and its move file are
  found next to its first costume model, and the new character's fields
  `0x1D8` (moves) and `0x1B4` (motions) point at the converted files. The
  wearer's second motion pack is kept.

- Entry numbers: Budokai 2's code asks for a move by its entry number, and
  Budokai 3 renumbered many entries. For one character, the importer reads its
  Budokai 2 script and its merged Budokai 3 script, scores every pair of
  entries by how many 16-byte command items they share, and aligns the two
  lists in order, allowing entries to be missing on either side. Pairs that
  share at least half their items are a match, and the converted entry is
  filed under the Budokai 2 number as well as kept at its own.
- It does this for all 31 characters in both games and takes a vote. Numbers
  below 512 are numbered the same way for everyone (358 becomes 375 for 30
  characters, 12 becomes 16 for 27), so a number takes the Budokai 3 entry most
  characters agree on, when at least 3 do and they are more than half. Numbers
  from 512 up are each character's own special moves and do not agree, so a
  character who is in both games uses its own alignment first, and the vote
  fills the rest. Goku now has 290 numbers matched instead of 275 and Vegeta
  299, with his own specials instead of Goku's. A character only in Budokai 3
  gets the 231 voted numbers; the 59 more that Goku's table gave were all
  Goku's own specials. Numbers with no match keep the Budokai 3 entry of that
  number, as before.

Checked in a fight with Budokai 3 Goku: he stands, walks, dashes, jumps and
fights in his Budokai 3 animations. Moves Budokai 3 does differently from
Budokai 2, or that Budokai 2's code numbers in a way Budokai 3 lacks, can still
do another move.

### Stages and capsules

The **Stages** tab lists the source disc's stages, Budokai 2's or Budokai 3's,
with the `DATA_CMN.AFS` entries each uses. A Budokai 3 stage can be imported
in place of ISLAND B, TOWN B, R&R BASE B or SNOW B. To play it, pick ISLAND,
TOWN, R&R BASE or SNOW on the stage select screen and confirm with Circle (C on
the keyboard) instead of Cross. Its particle effects are left out (see
[Stages](#stages)). Capsules are not mapped in any game yet.

### Budokai 3 models

With a Budokai 3 USA disc (SLUS-20998) as the source, the list on the left
holds Budokai 3's character models, one row per form or costume, such as
"Goku, model 3". The tool brings over a model's textures and puts them on a
Budokai 2 character's own body:

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

#### Shading colours

Each material names a texture and a shading texture, a 64 by 64 ramp the
game looks up by the surface's angle to the light. Budokai 2's ramps are black
or white with only the transparency changing: they darken or lighten the
texture under them. Budokai 3's are solid colours taken off the texture under
them, so its skin textures are white and a blue ramp turns them tan. Budokai 2
painted those solid colours over the body, which drew Budokai 3 characters
black, grey, white or blue.

Every Budokai 3 import now converts the ramps. For each ramp, the colour most
of it uses is taken off the colours of the textures drawn with it, except the
colours the shading skips (marked by full transparency, such as eye whites and
lines). The ramp then becomes Budokai 2's kind: black where it was darker than
that colour, white where it was lighter, at a transparency that matches the
difference. Ramps that already vary only in transparency, as in Budokai 2's
own models, are left alone. Characters imported with an earlier version keep
the old colours until they are imported again.

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
| Movesets   | Yes       | Not yet | Experimental | Not yet     |
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
shows them. IDs 53 to 62 have no record in the executable at all; OpenKai2
keeps their records at `0xA0000`, in memory below the executable that the
game never uses, and the translated code that finds a record by ID (every
multiply by 712 that indexes `0x389D40`) goes through `roster_offset`
(`src/mods/roster.h`), which points IDs 53 to 62 there. ID 63 (`0x3F`) is
Random, so 62 is the last. A mod fills an ID with `roster.ini`; IDs 53 to 62
need a `base`:

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
voice = add:my_voice.bin   ; a USR/DATA_US.AFS entry this mod adds, or a number
```

The changes are made in memory after the executable loads and are listed in
the startup log as `[mods] roster:` and `[mods] select:` lines. Later mods win.

A fighter's voice is a sound bank in `USR/DATA_US.AFS`: an `#AMB` of three
files (the samples, an `IECS` head and an `IECS` sequence file). The fight
loads the entry in the record's word at `0x1DC` (Goku 0x61, Krillin 0x6D; read
at `0x1D2F88`). `voice` sets that word, and an `add:` name is looked up among
the entries mods add to `USR/DATA_US.AFS` rather than `DATA_CMN.AFS`.

Budokai 3's banks have the same layout and version. Budokai 3 keeps a 372-byte
record per character at `0x428C60`, in the same order as its character records
at `0x447440`; the word at `0x24` is the character's bank in
`USR/DATA_USA.AFS` and the word at `0x2C` is always `0x64080000`. Krillin's,
Raditz's, Nappa's and Cell's banks are byte for byte the size of Budokai 2's.
When the import tool adds a whole Budokai 3 model as a new character, it copies
the character's bank and sets `voice`. The 44 characters in that table have a
bank; fusions and the characters after them keep their base character's voice.
A Krillin copy with Broly's bank loads it and fights with sound; the lines
were not checked by ear. Which sound a move asks for comes from the moveset, so
with a Budokai 2 moveset a move may play a different line from the same voice.

A filled ID also takes its base character's entries in the other tables the
game indexes by character ID, where the unused IDs hold nothing:

| Table | Entry | Covers | What it holds |
|-------|-------|--------|---------------|
| `0x376F30` | 12 bytes | IDs 0 to 40 | per form, the index of the move effects its attacks use (read at `0x20B340`); unused IDs hold -1, so their special moves draw nothing and hit nothing |
| `0x378850` | 4 bytes | IDs 0 to 31 | voice clips in `ADX_US.AFS`, as `(2 << 16) \| entry` |
| `0x3788D0`, `0x378960` | 4 bytes | IDs 0 to 35 | voice files in `ADX_US.AFS`, as `(2 << 16) \| entry` |
| `0x377120` | 4 bytes | IDs 0 to 52 | the `DATA_US.AFS` entry of the name plate (IDs 53 to 62 read their base's through `id_table_row`, at `0x20BD20` and `0x20C3B0`) |
| `0x3A1D60` | 20 bytes | IDs 0 to 40 | hair physics per costume (read at `0x200AC0`) |
| `0x3743A0` | 4 bytes | IDs 0 to 56 | a `DATA_US.AFS` entry loaded for each fighter (read at `0x1C7940`) |
| `0x378FF0` | 16 bytes | IDs 0 to 42 | the fight HUD's portrait: a `DATA_US.AFS` entry and its layout (read at `0x267DC0`) |
| `0x377AC0` | 24 bytes | IDs 0 to 52 | the aura of each form: its type (one of 7 handlers at `0x377330`), colour and size (read at `0x211970`) |

The last three, and the name plates of IDs 53 to 62, are read through
`roster_row` (`src/mods/select_screen.h`): a filled ID past a table's end, or
any filled ID from 53 to 62, reads its base character's row. Without it an ID
past the portrait table loaded entry 0 and showed no portrait in the fight
HUD, and IDs 53 to 62 loaded garbage entries or called an aura handler that
does not exist. The fighter entry and the HUD portrait go through
`portrait_row` instead, which also sends a filled unused ID below the table's
end (1, 13, 14, 19, 43 to 52) to its base's row: those IDs have placeholder
rows of their own, so an added character in ID 1 showed another character's
face in the fight HUD until its fighter entry came from its base.

IDs 43 to 52 lie past the move-effects and hair tables. The translated
code that reads them (`0x20B340`, `0x20BA60` and `0x20C3B0` for move effects,
`0x200AC0` for hair) scales the character ID through `id_table_row`
(`src/mods/select_screen.h`), which gives a filled ID past the tables its
base character's row, so its special moves and hair work. The voice player
at `0x24A0C0` reads the three voice tables the same way through `voice_row`,
so a filled ID past them speaks with its base character's voice.

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

### Stage mods

**Experimental since v0.1.62.** A Budokai 3 stage's file at `0x4C` is an
`#AMB` of 641 slots, indexed by effect: `#AME` particle effects, whole
animated stage objects (`#AMO` models, `#AMM` motions, `#AMC`, their own
`#AMT` textures), `#MTA` files, and the stage's effect texture in the last
slot. Budokai 2's stage packs hold up to 37 slots: the effect texture first,
then `#AME` effects. Loaded as it was, a Budokai 3 pack froze the game
(`0x231712`). The import tool builds a Budokai 2 pack instead.

Both games look stage effects up by a kind and the stage's area (the record's
`0x60` word, which the code at `0x1B95F8` flips when a fight moves to the
stage's other area). Budokai 2 reads slot `kind + 10 × area`, with kinds 5 to
9 fixed (`0x1AEC30`, `0x1AEDA0`, `0x1AF100` to `0x1AF510`) and other kinds
from move data (`0x1AEF20`); slots 1 to 4 are shock lines. Budokai 3 reads slot
`kind + 128 × area`, and its kinds start 5 lower: Budokai 3 Island's slot 129
(kind 1, area 1) is Budokai 2 Island's slot 6. So the built pack holds 25
slots: the effect texture first, Budokai 3's kinds 1 to 9 of the first area
in slots 6 to 14 and of the second area (or the first again) in slots 16 to
24. The game skips the empty slots. Dust and impact effects show in fights on
Budokai 3's Town. Animated objects (`#AMO`, `#AMM`) and Budokai 3's stage-wide
effects (slots 100 and up) are left out.

`#AME` itself is not the cause: Budokai 2's own stage packs hold
them too. Every `#AMB` record is 16 bytes:
offset, size, a loader word, and 0. Both games set the loader word to 1 for
`#AMO`, 2 for `#AMT`, 3 for `#AMM`, 4 for `#AMP`, 5 for `#AMC`, 6 for `#AML`
and 8 for `#SPX`, and to `0xFFFFFFFF` for files the pack loader leaves alone
(`#AME`, `#ASE`, `#BCM`, `#BSK`, nested `#AMB` and `STD`).

Stage mods holding a pack with a kind the loader table at `0x33D680` lacks
are skipped with a startup log line, as are packs of more than 64 slots,
which stage imports made before v0.1.65 left behind; import those again. `stage_mods_enabled` in `src/mods/stages.h` turns stage
mods off.

The four B stages (records 9 to 12: ISLAND B, TOWN B, R&R BASE B and SNOW B)
reuse another stage's files and differ from it only in the word at `0x60`, so
a stage mod can turn one into an imported stage without losing any stage that
has files of its own. A mod's `stages.ini` holds one section per stage:

```ini
[stage]
slot = 10                 ; a B stage record, 9 to 12
name = B3 BUDO            ; up to 31 characters
file.40 = add:stage_budo_40.bin
...                       ; file.40 to file.5c, all eight
```

Values are `DATA_CMN.AFS` entry numbers or `add:` names, as in `roster.ini`.
At start-up the record takes the files and name and its `0x60` word becomes 0;
the startup log has a `[mods] stage:` line for it. Later mods win a slot.

The B stages have no spot of their own on the stage select screen. Its ten
spots are listed at `0x3A4220`, two bytes each: the stage a spot plays, and
the stage it plays when you confirm with Circle instead of Cross (read at
`0x2B0900`). ISLAND, TOWN, R&R BASE and SNOW have ISLAND B, TOWN B,
R&R BASE B and SNOW B as their Circle stage; the other spots and Random play
the same stage either way. So a stage imported into TOWN B is played by
picking TOWN and pressing Circle (C on the keyboard). The spot still shows
TOWN's name and picture.

The import tool's **Stages** tab does this for Budokai 3 discs: choose a stage
on the left and a B stage on the right. It unpacks the stage's two packs into
the eight files in Budokai 2's order (by their `#MDD`, `#MAD`, `#MAS`, `#AMB`
and `#SPX` marks, then the second pack's three files) and marks texture packs
inside version 1. The Mods page lists a mod's stages, and **Remove** puts the
B stage back. A Budokai 3 ROCK imported into TOWN B loads and plays a full
fight. Adding stages beyond the 13 records, or spots of their own, still needs
the stage select screen's spot count and pictures mapped.

Budokai 3 keeps 17 stage records of 92 bytes at `0x44AE50`, starting with BUDO
(its seventh, GOKU, is at `0x44B078`). A record starts with a 16-byte name; the
words at `0x10` and `0x14` are `DATA_CMN.AFS` entries. The first is an `#AMB`
of the map (`#MDD`), its `#MAD` and `#MAS`, the `#SPX` and the stage's own
`#AMB` pack; the second an `#AMB` of the other three files. They are the eight
files a Budokai 2 stage names, with the same versions.

## Capsules

Budokai 2's capsules are `DATA_CMN.AFS` entry 851, a `#SKA` file: a 32-byte
head (version 1, 520 records of 32 bytes) and then the records. The game copies
them to `0x694AA0` and numbers capsules 0 to 519 everywhere: the save keeps how
many of each you own at `0x89E`, and twelve functions loop over exactly 520
(`slti ..., 0x208`). A record starts with an 8-byte mask of the characters who
may equip it; byte `0x0D` is how many you can own, and 106 records have 0 there.
Numbers `0x1CD` to `0x1E9` unlock characters through the byte table at
`0x379E03`, and `0x1FE` onwards unlock stages (read at `0x283680`).

| Offset | What it holds |
|--------|---------------|
| `0x00` | 8-byte mask of the character IDs that may equip it; 0 for everyone |
| `0x08` | the kind: `0x21` skills (a move), `0x11`, `0x12`, `0x13`, `0x14` and `0x17` other capsules, 0 for unlocks and unused numbers |
| `0x0B` | flags; bits `0x10`, `0x20` and `0x40` say where the capsule applies (read at `0x1EF060`) |
| `0x0D` | how many you can own |
| `0x14` | the effect: an index into the 14 handlers at `0x3943F0` (read at `0x1EF060`), or for a skill the move it grants |
| `0x15` | the effect's sub-kind (`0x15` and `0x16` scale a stat, read at `0x1EA8F0`) |
| `0x16`, `0x17` | the effect's amounts |

Budokai 3 keeps its capsules in `USR/DATA_USA.AFS` entry 4, a version 2 `#SKA`
of 596 records of 40 bytes: the same 8-byte mask (numbered by Budokai 3's
characters), the same head bytes `0x08` to `0x0F`, and a 24-byte tail in place
of Budokai 2's 16. In the tail the effect amounts are 16-bit (`0x16` and
`0x18`), and the effect numbers and icon numbers at `0x14` and `0x15` differ from
Budokai 2's: a capsule with the same category byte (`0x09`) and amounts has a
different effect number in each game. 595 records are used and 518 can be owned.
None is byte for byte the same as a Budokai 2 record.

A mod whose entry 851 holds more than 520 records moves the table to `0xB0000`
and keeps the owned counts past 520 in `openkai2-capsules.bin` beside the
memory card. Three more places still hold exactly 520 counts:

- the copy of the inventory at `0x699850` (`0x210` bytes, made at `0x238160`
  and restored at `0x2386B0`), which Edit Skills in versus mode reads; with a
  larger table it moves to `0xE8000` and grows with the table;
- the memory-card save images at `0x69A800`, `0xAB0` bytes each, with the
  counts at `0x89E`. The Skill List, the shop and 20 other functions read the
  counts straight from these, so capsules past 520 do not show there yet;
- the capsule names, which are pre-drawn images looked up by capsule number
  (read at `0x281C78`), so a capsule past 520 has no name yet.

A character mask also has to be renumbered to Budokai 2's IDs, and a skill
needs a move number in Budokai 2's numbering.

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

- **There are 24 unused IDs to fill** (1, 13, 14, 19 and 43 to 62), so 24
  added characters at most. ID 63 is Random, and the game keeps character IDs
  in single bytes beside it.
- **New spots borrow a portrait and show Random's name.** The sprite sheet
  (`DATA_CMN` entry 901, an `#AMA` of 120 sprites whose frames point at
  texture pages in another file) has one portrait per original spot. The name
  shown over the 3D model follows the spot too: a character put on spot 1 with
  `select` shows spot 1's original name, and a new spot, numbered from 29 where
  Random was, shows Random's. The sprite it is drawn from is not found yet.
- **Not every character table is mapped.** Data the record does not name, such
  as select-screen voice lines, story scenes and skill capsules, stays the base
  character's.
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
