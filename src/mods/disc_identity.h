// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/importer.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openkai2::mods {

enum class DragonBallGame { budokai, budokai2, budokai3, infinite_world, other, none };
const char *game_name(DragonBallGame game);

struct DiscIdentity {
    DragonBallGame game = DragonBallGame::none;
    std::string serial;
    std::string executable;
};

DiscIdentity identify_disc(SourceDisc &disc);

struct ExecutableImage {
    std::vector<uint8_t> memory;
    bool contains(uint32_t address, size_t bytes) const { return address + bytes <= memory.size(); }
};
std::optional<ExecutableImage> load_executable(const std::vector<uint8_t> &elf);

struct CharacterTable {
    uint32_t first = 0;
    uint32_t stride = 0;
    bool pointers = false;
    std::vector<std::string> names;
};
std::vector<CharacterTable> find_character_tables(const ExecutableImage &image);

std::string disc_report(SourceDisc &disc);

}
