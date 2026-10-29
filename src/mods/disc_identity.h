// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/importer.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Tells which Dragon Ball game a PS2 disc holds. SYSTEM.CNF names the boot
// executable, whose name is the disc's serial. Each Budokai executable keeps
// its memory card folder name, such as BASLUS-20779DBZ2, which pairs the
// serial with the game: DBZ for Budokai, DBZ2 for Budokai 2, DBZ3 for
// Budokai 3. Budokai 2 also names Budokai's folder, BASLUS-20591DBZ.
namespace openkai2::mods {

enum class DragonBallGame { budokai, budokai2, budokai3, infinite_world, other, none };
const char *game_name(DragonBallGame game);

struct DiscIdentity {
    DragonBallGame game = DragonBallGame::none;
    std::string serial;     // e.g. SLUS-20779
    std::string executable; // disc path of the boot executable
};

// Reads SYSTEM.CNF and the boot executable. Throws std::runtime_error when
// the disc cannot be read; a disc that is not a Dragon Ball game comes back
// with DragonBallGame::none.
DiscIdentity identify_disc(SourceDisc &disc);

// An executable's loadable segments placed at their addresses.
struct ExecutableImage {
    std::vector<uint8_t> memory; // from address 0
    bool contains(uint32_t address, size_t bytes) const { return address + bytes <= memory.size(); }
};
std::optional<ExecutableImage> load_executable(const std::vector<uint8_t> &elf);

// A run of records whose names are Dragon Ball characters, found by their
// spacing. Used to map games whose tables are not known yet. Budokai 2 keeps
// each name in its record; Budokai 3 keeps its names in a list of their own,
// so its records are found through words that point at the names.
struct CharacterTable {
    uint32_t first = 0;  // address of the first name, or of the first pointer
    uint32_t stride = 0; // bytes from one record to the next
    bool pointers = false;
    std::vector<std::string> names;
};
std::vector<CharacterTable> find_character_tables(const ExecutableImage &image);

// A plain-text description of a disc for mapping its tables: the serial, the
// files and archives, character tables found in the executable with the
// fields that look like file numbers, and the first bytes of archive entries
// so file formats can be compared. It holds no game data beyond names,
// numbers and those first bytes.
std::string disc_report(SourceDisc &disc);

} // namespace openkai2::mods
