// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/importer.h"
#include "mods/roster.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openkai2::mods {

inline constexpr uint32_t budokai3_names = 0x4314B0u, budokai3_records = 0x447440u;
inline constexpr uint32_t budokai3_record_bytes = 96u, budokai3_name_bytes = 32u, budokai3_count = 106u;

std::optional<ModelShape> model_shape(SourceDisc &disc, size_t entry);

std::optional<DiscRoster> read_budokai3_models(SourceDisc &disc);

std::vector<std::pair<uint32_t, uint32_t>> costume_fields(SourceDisc &game, const DiscCharacter &character);
void read_model_shapes(SourceDisc &game, DiscRoster &roster);
std::vector<size_t> match_textures(const std::vector<std::array<uint32_t, 2>> &model,
                                   const std::vector<std::array<uint32_t, 2>> &wearer);
bool textures_fit(const ModelShape &model, const ModelShape &wearer);
std::vector<uint8_t> match_pack(const std::vector<uint8_t> &theirs, const std::vector<uint8_t> &ours,
                                bool ourLighting = false);
std::vector<uint8_t> with_budokai2_lighting(const std::vector<uint8_t> &theirs, const std::vector<uint8_t> &ours);
std::vector<uint32_t> fitting_wearers(const DiscRoster &models, uint32_t model, const DiscRoster &wearers);

std::string model_structure(SourceDisc &disc, size_t entry);

std::string model_contents(SourceDisc &disc, size_t entry);

std::string model_digest(SourceDisc &disc, size_t entry);

std::string mesh_differences(SourceDisc &budokai3, size_t entry, SourceDisc &game, size_t own);

inline constexpr uint32_t budokai2_material_flag = 0x2000u;
std::vector<size_t> material_records(const std::vector<uint8_t> &model, bool any);
inline constexpr uint32_t budokai2_draw_flag = 0x20u;
std::vector<size_t> draw_records(const std::vector<uint8_t> &model);
std::string model_materials(SourceDisc &disc, size_t entry);

std::vector<uint8_t> model_for_budokai2(SourceDisc &disc, size_t entry);

enum class ModelVersion {
    converted,
    unchanged,
    budokai2_textures,
    budokai2_mesh,
    budokai2_lighting,
    converted_budokai2_lighting,
};
std::vector<uint8_t> model_version(SourceDisc &budokai3, size_t entry, SourceDisc &game, size_t own,
                                   ModelVersion version);

}
