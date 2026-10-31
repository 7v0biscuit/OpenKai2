// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/disc_overlay.h"
#include "mods/importer.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace openkai2::mods {

inline constexpr uint32_t roster_records = 0x389D40u;
inline constexpr uint32_t roster_record_bytes = 712u;
inline constexpr uint32_t roster_count = 53u;
inline constexpr uint32_t roster_name_bytes = 32u;
inline constexpr uint32_t roster_select_grid = 0x3A4140u;
inline constexpr uint32_t roster_select_cells = 29u;
inline constexpr const char *roster_archive = "USR/DATA_CMN.AFS";
inline constexpr uint32_t roster_id_cells = 0x3A4100u;
inline constexpr uint32_t roster_costumes = 0x37B310u;

struct RosterIdTable {
    uint32_t address, bytes, count;
    const char *name;
};
inline constexpr RosterIdTable roster_id_tables[] = {
    {0x376F30u, 12u, 41u, "move effects"},
    {0x3788D0u, 4u, 36u, "voice"},
    {0x378960u, 4u, 36u, "voice set"},
    {0x377120u, 4u, 53u, "name plate"},
    {0x3A1D60u, 20u, 41u, "hair"},
};


bool roster_slot_unused(uint32_t id);

struct RosterField {
    uint32_t offset = 0;
    unsigned bytes = 4;
    bool real = false;
    uint32_t value = 0;
};

struct RosterCharacter {
    std::filesystem::path source;
    uint32_t slot = 0;
    std::optional<uint32_t> base;
    std::optional<std::string> name;
    std::vector<RosterField> fields;
    std::optional<uint32_t> select_cell;
    std::vector<uint32_t> alternate_cells;
    bool new_cell = false;
    std::optional<uint32_t> icon_cell;
};

inline constexpr uint32_t roster_files_begin = 0x74u, roster_files_end = 0x1E4u;

struct DiscCharacter {
    uint32_t id = 0;
    std::string internal;
    std::string name;
    std::vector<std::pair<uint32_t, uint32_t>> files;
};
struct ModelShape {
    std::vector<std::array<uint32_t, 4>> parts;
    std::vector<std::array<uint32_t, 2>> textures;
    bool operator==(const ModelShape &other) const { return parts == other.parts; }
};

struct DiscRoster {
    std::vector<DiscCharacter> characters;
    std::array<uint8_t, roster_select_cells> grid{};
    bool models = false;
    std::map<uint32_t, ModelShape> shapes;
    const DiscCharacter *find(uint32_t id) const;
};

std::string roster_display_name(const std::string &internal);
std::optional<DiscRoster> read_disc_roster(SourceDisc &disc);
std::vector<uint32_t> roster_slots_in_use(const std::vector<std::filesystem::path> &mods);
std::optional<uint32_t> free_roster_slot(const std::vector<uint32_t> &taken);
void append_roster(const std::filesystem::path &mod, const RosterCharacter &character,
                   const std::vector<std::pair<uint32_t, std::string>> &files);

struct RosterSection {
    uint32_t slot = 0;
    std::string name;
    std::optional<uint32_t> base;
    std::vector<uint32_t> shared_cells;
    bool new_cell = false;
    std::vector<std::string> added;
};
std::vector<RosterSection> read_roster_sections(const std::filesystem::path &mod);
size_t remove_roster_character(const std::filesystem::path &mod, uint32_t slot);

std::vector<RosterCharacter> load_roster(const std::vector<std::filesystem::path> &mods,
                                         const DiscOverlay::Summary &overlay);

std::vector<std::string> apply_roster(uint8_t *memory, size_t bytes, const std::vector<RosterCharacter> &characters);

}
