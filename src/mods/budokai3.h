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
// both games, and every header matches. Budokai 3 marks its textures version 2
// where Budokai 2 uses 1; with the mark changed, Budokai 2 draws them. Budokai
// 3 meshes leave out a flag Budokai 2 sets in every material, and Budokai 2
// draws them black; imports put the Budokai 3 textures on the wearer's own
// Budokai 2 mesh, and forced imports set the flag. Movesets are not converted:
// the wearer keeps its moves and animations.
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
// For each of a wearer's texture slots, the model texture that fills it: in
// order, the next with the slot's format and size, so the wearer's mesh finds
// a texture of the shape it expects. The model's extra textures are skipped.
// Empty when a slot cannot be filled.
std::vector<size_t> match_textures(const std::vector<std::array<uint32_t, 2>> &model,
                                   const std::vector<std::array<uint32_t, 2>> &wearer);
// Whether a model's textures can be matched to a wearer's own mesh.
bool textures_fit(const ModelShape &model, const ModelShape &wearer);
// A texture pack for the wearer's mesh, built from the model's pack `theirs`
// to the layout of the wearer's pack `ours`; with `ourLighting`, the wearer's
// own shading textures stay in place of the model's. Throws when they do not
// match.
std::vector<uint8_t> match_pack(const std::vector<uint8_t> &theirs, const std::vector<uint8_t> &ours,
                                bool ourLighting = false);
// The model's pack `theirs`, its shading textures replaced by those of the
// wearer's pack `ours` of the same format and size, in order.
std::vector<uint8_t> with_budokai2_lighting(const std::vector<uint8_t> &theirs, const std::vector<uint8_t> &ours);
// The IDs of the characters in `wearers` whose mesh can wear the textures of
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

// Every word in which a Budokai 3 model's mesh differs from a Budokai 2
// model's mesh `own`, leaving out vertex data: counts, the most common
// changes and the first 200 with their rows, to find what else Budokai 2
// draws differently.
std::string mesh_differences(SourceDisc &budokai3, size_t entry, SourceDisc &game, size_t own);

// The flag Budokai 2 sets in the first word of every material record of a
// mesh (0x21B5) and Budokai 3 leaves clear (0x01B5); without it Budokai 2
// draws the mesh black.
inline constexpr uint32_t budokai2_material_flag = 0x2000u;
// Where a model's material records are: 16 bytes at a 16-byte boundary in a
// mesh block, holding flags, 0x29BD and two texture numbers. With `any`, every
// record with 0x29BD in its second word, whatever its flags.
std::vector<size_t> material_records(const std::vector<uint8_t> &model, bool any);
// A flag in the first word of draw records in a mesh's first block. Supreme
// Kai's Budokai 2 mesh sets it in 27 of 55 (0x9000022C where Budokai 3 has
// 0x9000020C) and Krillin's in none, so it is not changed on import.
inline constexpr uint32_t budokai2_draw_flag = 0x20u;
// Where a model's draw records are: 16 bytes at a 16-byte boundary in the
// mesh's first block, a word of 0x2xx with a kind of 0, 6, 8 or 9 in its top
// four bits, then a 16-byte-aligned offset in the mesh.
std::vector<size_t> draw_records(const std::vector<uint8_t> &model);
// A plain-text list of a model's material records, for the disc report.
std::string model_materials(SourceDisc &disc, size_t entry);

// A model entry's bytes, with Budokai 3 texture packs marked as Budokai 2's
// and its materials given Budokai 2's flag.
std::vector<uint8_t> model_for_budokai2(SourceDisc &disc, size_t entry);

// Versions of a Budokai 3 model for finding out in game which of its parts
// Budokai 2 draws wrongly. Each mixes the Budokai 3 model `entry` with the
// wearer's own Budokai 2 model `own`.
enum class ModelVersion {
    converted,          // textures marked as Budokai 2's (model_for_budokai2)
    unchanged,          // exactly as on the Budokai 3 disc
    budokai2_textures,  // the Budokai 3 mesh with the wearer's textures
    budokai2_mesh,      // the wearer's mesh with the Budokai 3 textures matched to it
    budokai2_lighting,  // budokai2_mesh, keeping the wearer's own shading textures
    // the whole model as converted, its shading textures replaced by the wearer's
    converted_budokai2_lighting,
};
std::vector<uint8_t> model_version(SourceDisc &budokai3, size_t entry, SourceDisc &game, size_t own,
                                   ModelVersion version);

} // namespace openkai2::mods
