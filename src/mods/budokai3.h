// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/importer.h"
#include "mods/roster.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
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

// The shape of a DATA_CMN entry that is a costume model, or nothing.
std::optional<ModelShape> model_shape(SourceDisc &disc, size_t entry);

// The Budokai 3 characters' models as a roster: each character is one model,
// with that model as its only file (at offset 0), and its shape. Nothing for
// other discs.
std::optional<DiscRoster> read_budokai3_models(SourceDisc &disc);

// The record fields of a Budokai 2 character that hold its costume models.
std::vector<std::pair<uint32_t, uint32_t>> costume_fields(SourceDisc &game, const DiscCharacter &character);
// Reads the shape of each Budokai 2 character's first costume model.
void read_model_shapes(SourceDisc &game, DiscRoster &roster);
// The IDs of the characters in `wearers` whose model has the same shape as
// the model `model` in `models`, in roster order.
std::vector<uint32_t> fitting_wearers(const DiscRoster &models, uint32_t model, const DiscRoster &wearers);

// A plain-text dump of a costume model's mesh: its header, each part's row
// and the first bytes of each part's data, for working out how parts attach
// to the skeleton.
std::string model_structure(SourceDisc &disc, size_t entry);

// A plain-text dump of a costume model's textures and mesh blocks: each
// texture's header with the first bytes of its pixels and palette, and the
// first bytes of each block the mesh header lists.
std::string model_contents(SourceDisc &disc, size_t entry);

// Check values of a costume model's mesh blocks and textures, so two games'
// models can be compared piece by piece, then the first 512 bytes of the
// second mesh block and the last 128 bytes of the texture pack.
std::string model_digest(SourceDisc &disc, size_t entry);

// A model entry's bytes, with Budokai 3 texture packs marked as Budokai 2's.
std::vector<uint8_t> model_for_budokai2(SourceDisc &disc, size_t entry);

} // namespace openkai2::mods
