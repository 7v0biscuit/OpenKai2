// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/importer.h"
#include "mods/roster.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Character models from the Budokai 3 USA disc (SLUS-20998), worn by Budokai 2
// characters.
//
// Budokai 3 keeps its character names in 32-byte slots from 0x4314B0 and one
// 96-byte record per character from 0x447440, in the same order. A record
// holds a pointer to its name, four bytes whose first two multiply to the
// number of its models, and a pointer to a list of 12-byte items, each
// starting with a DATA_CMN.AFS entry number.
//
// A costume model is an #AMB pack of a mesh (#AMO) and its textures (#AMT) in
// both games. The mesh format and every texture header match; Budokai 3 marks
// its textures version 2 where Budokai 2 uses 1, so imported models are
// marked version 1. Movesets are not converted: the model is worn by a Budokai
// 2 character with the same body, whose moves and animations it keeps.
namespace openkai2::mods {

inline constexpr uint32_t budokai3_names = 0x4314B0u, budokai3_records = 0x447440u;
inline constexpr uint32_t budokai3_record_bytes = 96u, budokai3_name_bytes = 32u, budokai3_count = 106u;

// A model's parts and how they link, which must match for a Budokai 2
// character's animations to move it.
struct ModelShape {
    std::vector<std::array<uint32_t, 4>> parts;
    bool operator==(const ModelShape &other) const { return parts == other.parts; }
};
// The shape of a DATA_CMN entry that is a costume model, or nothing.
std::optional<ModelShape> model_shape(SourceDisc &disc, size_t entry);

// The Budokai 3 characters' models as a roster: each character is one model,
// with that model as its only file (at offset 0). Nothing for other discs.
std::optional<DiscRoster> read_budokai3_models(SourceDisc &disc);

// The record fields of a Budokai 2 character that hold its costume models.
std::vector<std::pair<uint32_t, uint32_t>> costume_fields(SourceDisc &game, const DiscCharacter &character);

// A model entry's bytes, with Budokai 3 texture packs marked as Budokai 2's.
std::vector<uint8_t> model_for_budokai2(SourceDisc &disc, size_t entry);

} // namespace openkai2::mods
