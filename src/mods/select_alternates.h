// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/roster.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openkai2::mods {

inline constexpr uint32_t select_state_pointers = 0x3A54B8u;
inline constexpr uint32_t select_state_bytes = 1664u;
inline constexpr uint16_t alternate_button = 0x0200u;

struct SelectAlternates {
    std::array<std::vector<uint8_t>, roster_select_cells> spots;
    bool empty() const;
};

SelectAlternates build_alternates(const uint8_t *memory, size_t bytes, const std::vector<RosterCharacter> &characters);

std::optional<std::string> cycle_alternate(uint8_t *memory, size_t bytes, int player, uint16_t pressed,
                                           const SelectAlternates &alternates);

void install_alternates(uint8_t *memory, size_t bytes, SelectAlternates alternates);

}
