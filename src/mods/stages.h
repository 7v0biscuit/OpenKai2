// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/disc_overlay.h"
#include "mods/importer.h"
#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openkai2::mods {

inline constexpr uint32_t stage_records = 0x373270u, stage_record_bytes = 156u, stage_count = 13u;
inline constexpr uint32_t stage_files_begin = 0x40u, stage_files_end = 0x60u, stage_variant = 0x60u;
inline constexpr uint32_t budokai3_stage_names = 0x44B078u, budokai3_stage_bytes = 92u;
inline constexpr uint32_t budokai3_stages = 0x44AE50u, budokai3_stage_count = 17u;

struct DiscStage {
    uint32_t index = 0;
    std::string internal;
    bool variant = false;
    std::vector<std::pair<uint32_t, uint32_t>> files;
};

inline constexpr uint32_t stage_first_variant = 9u;
inline constexpr bool stage_mods_enabled = true;

struct StageMod {
    std::filesystem::path source;
    uint32_t slot = 0;
    std::string name;
    std::vector<std::pair<uint32_t, uint32_t>> files;
};
struct StageSection {
    uint32_t slot = 0;
    std::string name;
    std::vector<std::string> added;
};
std::vector<StageMod> load_stage_mods(const std::vector<std::filesystem::path> &mods,
                                      const DiscOverlay::Summary &overlay, std::vector<std::string> *notes = nullptr);
std::vector<std::string> apply_stage_mods(uint8_t *memory, size_t bytes, const std::vector<StageMod> &stages);
void append_stage(const std::filesystem::path &mod, uint32_t slot, const std::string &name,
                  const std::vector<std::pair<uint32_t, std::string>> &files);
std::vector<StageSection> read_stage_sections(const std::filesystem::path &mod);
size_t remove_stage(const std::filesystem::path &mod, uint32_t slot);

std::vector<std::pair<uint32_t, std::vector<uint8_t>>> budokai3_stage_files(SourceDisc &disc, const DiscStage &stage);

inline constexpr const char *budokai2_pack_kinds[] = {"AMG", "AMT", "AMM", "AMO", "AMC", "AML", "AMP"};
std::vector<std::string> unloadable_pack_files(const std::vector<uint8_t> &pack);
std::vector<uint8_t> without_unloadable_files(const std::vector<uint8_t> &pack);
std::optional<std::string> budokai3_stage_problem(SourceDisc &disc, const DiscStage &stage);

std::optional<std::vector<DiscStage>> read_disc_stages(SourceDisc &disc);
std::string stage_display_name(const std::string &internal);

}
