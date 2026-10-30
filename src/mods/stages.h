// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/disc_overlay.h"
#include "mods/importer.h"
#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The USA executable keeps 13 stage records of 156 bytes from 0x373270. A
// record starts with a 32-byte name; from 0x40 it holds eight DATA_CMN.AFS
// entries: the stage's map (#MDD) at 0x40, its #MAD and #MAS at 0x44 and
// 0x48, its #AMB pack at 0x4C, its #SPX at 0x50 and three more files. The
// word at 0x60 is 1 for the "B" stages, which reuse another stage's files.
// The game keeps the current stage's index at 0x373C60 and 0x373CAC.
//
// The nine stages with files of their own use DATA_CMN entries 2 to 73, eight
// each.
//
// Budokai 3 keeps 17 stage records of 92 bytes from 0x44AE50: a 16-byte name,
// then two DATA_CMN.AFS entries at 0x10 and 0x14. The first is an #AMB of the
// map (#MDD), its #MAD and #MAS, the #SPX and the stage's own #AMB pack; the
// second an #AMB of the other three files. They are the eight files a Budokai
// 2 stage names, with the same versions.
namespace openkai2::mods {

inline constexpr uint32_t stage_records = 0x373270u, stage_record_bytes = 156u, stage_count = 13u;
inline constexpr uint32_t stage_files_begin = 0x40u, stage_files_end = 0x60u, stage_variant = 0x60u;
// The name of Budokai 3's seventh stage, GOKU, checked to know the table.
inline constexpr uint32_t budokai3_stage_names = 0x44B078u, budokai3_stage_bytes = 92u;
inline constexpr uint32_t budokai3_stages = 0x44AE50u, budokai3_stage_count = 17u;

struct DiscStage {
    uint32_t index = 0;
    std::string internal; // the name in the record, e.g. ROOM TIME MIND
    bool variant = false; // a "B" stage, reusing another stage's files
    // (record offset, DATA_CMN entry) for each file the record names
    std::vector<std::pair<uint32_t, uint32_t>> files;
};

// The four "B" stages (ISLAND B, TOWN B, R&R BASE B and SNOW B, records 9 to
// 12) reuse another stage's files and differ from it only in the word at 0x60.
// A stage mod turns one of them into an imported stage: its own eight files,
// a name, and 0 at 0x60. No stage with files of its own is lost.
inline constexpr uint32_t stage_first_variant = 9u;
// Stage mods are not applied and the import tool does not offer stage
// imports: a Budokai 3 stage loaded this way froze the game (it jumped to
// 0x231712 while loading), so its files need converting first.
inline constexpr bool stage_mods_enabled = false;

// A [stage] section of a mod's stages.ini, with add: values resolved to the
// DATA_CMN entries the overlay gave them.
struct StageMod {
    std::filesystem::path source; // the stages.ini it came from, for messages
    uint32_t slot = 0;            // a B stage record, 9 to 12
    std::string name;             // up to 31 characters
    std::vector<std::pair<uint32_t, uint32_t>> files; // (record offset 0x40 to 0x5C, entry)
};
// A [stage] section as written, for listing and removing.
struct StageSection {
    uint32_t slot = 0;
    std::string name;
    std::vector<std::string> added; // files from add: values
};
// Reads stages.ini from each mod, in load order; later mods win a slot.
// Throws std::runtime_error naming the file and line on bad input.
// A stage whose #AMB pack holds a file Budokai 2 cannot load is skipped,
// with a line in `notes` saying why, as it would stop the game.
std::vector<StageMod> load_stage_mods(const std::vector<std::filesystem::path> &mods,
                                      const DiscOverlay::Summary &overlay, std::vector<std::string> *notes = nullptr);
// Writes the stages into the loaded USA executable's stage table. Returns a
// line per stage for the startup log; throws if the table is not there.
std::vector<std::string> apply_stage_mods(uint8_t *memory, size_t bytes, const std::vector<StageMod> &stages);
// Adds a [stage] section to the mod's stages.ini.
void append_stage(const std::filesystem::path &mod, uint32_t slot, const std::string &name,
                  const std::vector<std::pair<uint32_t, std::string>> &files);
std::vector<StageSection> read_stage_sections(const std::filesystem::path &mod);
// Removes the stage filling `slot` from the mod's stages.ini, with the added
// files no other stage uses. Returns how many files were removed.
size_t remove_stage(const std::filesystem::path &mod, uint32_t slot);

// A Budokai 3 stage's two packs unpacked into the eight files a Budokai 2
// stage record names, as (record offset, bytes): #MDD, #MAD, #MAS, the #AMB
// pack and #SPX by their marks, then the second pack's three files in order.
// Texture packs inside are marked version 1, as Budokai 2 reads them. Throws
// when the packs are not laid out that way.
std::vector<std::pair<uint32_t, std::vector<uint8_t>>> budokai3_stage_files(SourceDisc &disc, const DiscStage &stage);

// The kinds of file Budokai 2 loads from an #AMB pack: its loader at 0x12A680
// looks each file's mark up in the table at 0x33D680 and calls a null
// function for any other, which stops the game.
inline constexpr const char *budokai2_pack_kinds[] = {"AMG", "AMT", "AMM", "AMO", "AMC", "AML", "AMP"};
// The marks of the files in an #AMB pack that Budokai 2 cannot load, e.g.
// "#AMX"; empty when it can load them all.
std::vector<std::string> unloadable_pack_files(const std::vector<uint8_t> &pack);
// Why a Budokai 3 stage can't be imported, or nothing when it can.
std::optional<std::string> budokai3_stage_problem(SourceDisc &disc, const DiscStage &stage);

// Reads the stage table from a disc's own executable: Budokai 2 USA's, or
// Budokai 3 USA's, whose stages each name their two packs at 0x10 and 0x14.
// Nothing for other discs.
std::optional<std::vector<DiscStage>> read_disc_stages(SourceDisc &disc);
// A stage's name to show, e.g. "Hyperbolic Time Chamber" for ROOM TIME MIND
// and "Island B" for ISLAND B.
std::string stage_display_name(const std::string &internal);

} // namespace openkai2::mods
