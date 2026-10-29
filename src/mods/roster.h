// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/disc_overlay.h"
#include "mods/importer.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The USA executable keeps the roster in its data segment:
//
//   0x389D40  53 character records of 712 bytes, indexed by character ID. A
//             record starts with a 32-byte name; u32 fields from 0x74 to
//             0x1E0 hold the character's DATA_CMN.AFS entries.
//   0x3A4140  the character select grid: one character ID per cell. The
//             screen has 30 cells; cell 29 is Random (ID 0x3F).
//
// File IDs in the game are (archive << 16) | entry, and archive 0 is
// DATA_CMN.AFS, so record fields hold plain DATA_CMN entry numbers.
//
// IDs 1, 13, 14, 19 and 43 to 52 have names but no data: the game never
// uses them. A mod can fill one of them and give it a select cell.
namespace openkai2::mods {

inline constexpr uint32_t roster_records = 0x389D40u;
inline constexpr uint32_t roster_record_bytes = 712u;
inline constexpr uint32_t roster_count = 53u;
inline constexpr uint32_t roster_name_bytes = 32u;
inline constexpr uint32_t roster_select_grid = 0x3A4140u;
inline constexpr uint32_t roster_select_cells = 29u; // without the Random cell
inline constexpr const char *roster_archive = "USR/DATA_CMN.AFS";
inline constexpr uint32_t roster_id_cells = 0x3A4100u;       // select cell per ID, -1 for none
inline constexpr uint32_t roster_costumes = 0x37B310u;       // costumes per ID


bool roster_slot_unused(uint32_t id);

struct RosterField {
    uint32_t offset = 0;
    unsigned bytes = 4; // 1, 2 or 4
    bool real = false;  // a 32-bit float
    uint32_t value = 0; // the bits to write
};

struct RosterCharacter {
    std::filesystem::path source; // the roster.ini it came from, for messages
    uint32_t slot = 0;
    std::optional<uint32_t> base;
    std::optional<std::string> name;
    std::vector<RosterField> fields;
    std::optional<uint32_t> select_cell;
    // Spots the character shares with the character shown there; R2 switches.
    std::vector<uint32_t> alternate_cells;
    // A spot of its own, added before Random, showing the portrait of the
    // spot `icon_cell` names (or its base character's spot).
    bool new_cell = false;
    std::optional<uint32_t> icon_cell;
};

// Record fields that hold DATA_CMN entries; unused ones hold 0 or -1.
inline constexpr uint32_t roster_files_begin = 0x74u, roster_files_end = 0x1E4u;

struct DiscCharacter {
    uint32_t id = 0;
    std::string internal; // the name in the record, e.g. KULILIN
    std::string name;     // the English name, e.g. Krillin
    // (record offset, DATA_CMN entry) for every file the record names
    std::vector<std::pair<uint32_t, uint32_t>> files;
};
struct DiscRoster {
    std::vector<DiscCharacter> characters; // only characters that have files
    std::array<uint8_t, roster_select_cells> grid{};
    // Models from another game (budokai3.h): each character is one model,
    // worn by the Budokai 2 character it is imported onto. There is no grid.
    bool models = false;
    const DiscCharacter *find(uint32_t id) const;
};

// The English name for a record name, or the record name if it has none.
std::string roster_display_name(const std::string &internal);
// Reads the roster from a disc's own executable. Nothing when the disc is
// not Budokai 2 USA, whose table layout is the only one mapped so far.
std::optional<DiscRoster> read_disc_roster(SourceDisc &disc);
// The slots that the mods' roster.ini files fill, read without resolving
// their values, so a broken file elsewhere does not hide a slot.
std::vector<uint32_t> roster_slots_in_use(const std::vector<std::filesystem::path> &mods);
// The lowest unused ID that is not in `taken`.
std::optional<uint32_t> free_roster_slot(const std::vector<uint32_t> &taken);
// Adds a [character] section to the mod's roster.ini.
void append_roster(const std::filesystem::path &mod, const RosterCharacter &character,
                   const std::vector<std::pair<uint32_t, std::string>> &files);

// Reads roster.ini from each mod, in load order. "add:<name>" values are
// resolved to the entry numbers the overlay gave those added DATA_CMN
// entries. Throws std::runtime_error naming the file and line on bad input.
std::vector<RosterCharacter> load_roster(const std::vector<std::filesystem::path> &mods,
                                         const DiscOverlay::Summary &overlay);

// Applies the characters to guest memory after the executable is loaded.
// Returns a line per change for the startup log. Throws if the memory does
// not hold the USA roster, so a different executable is never patched.
std::vector<std::string> apply_roster(uint8_t *memory, size_t bytes, const std::vector<RosterCharacter> &characters);

} // namespace openkai2::mods
