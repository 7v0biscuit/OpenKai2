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
// each. Budokai 3 names its stages 92 bytes apart from 0x44B078; that table
// is not mapped yet.
namespace openkai2::mods {

inline constexpr uint32_t stage_records = 0x373270u, stage_record_bytes = 156u, stage_count = 13u;
inline constexpr uint32_t stage_files_begin = 0x40u, stage_files_end = 0x60u, stage_variant = 0x60u;
inline constexpr uint32_t budokai3_stage_names = 0x44B078u, budokai3_stage_bytes = 92u;

struct DiscStage {
    uint32_t index = 0;
    std::string internal; // the name in the record, e.g. ROOM TIME MIND
    bool variant = false; // a "B" stage, reusing another stage's files
    // (record offset, DATA_CMN entry) for each file the record names
    std::vector<std::pair<uint32_t, uint32_t>> files;
};

// Reads the stage table from a disc's own executable. Nothing when the disc
// is not Budokai 2 USA.
std::optional<std::vector<DiscStage>> read_disc_stages(SourceDisc &disc);

} // namespace openkai2::mods
