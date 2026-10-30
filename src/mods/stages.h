// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/importer.h"
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

// Reads the stage table from a disc's own executable: Budokai 2 USA's, or
// Budokai 3 USA's, whose stages each name their two packs at 0x10 and 0x14.
// Nothing for other discs.
std::optional<std::vector<DiscStage>> read_disc_stages(SourceDisc &disc);
// A stage's name to show, e.g. "Hyperbolic Time Chamber" for ROOM TIME MIND.
std::string stage_display_name(const std::string &internal);

} // namespace openkai2::mods
