// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/roster.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Characters that share a spot on the select screen. The screen is a wheel
// of 29 spots plus Random; pressing R2 (Right Shift on the keyboard) on a
// spot with alternates switches the character it holds, so added characters
// do not push anyone off the screen. The spot keeps its original portrait.
//
// The select screen keeps a state block per player; pointers to them are at
// 0x3A54B8 (gp - 28728). In a block, +88 is the player, +128 the hovered
// character ID, +129 its costume and +192 the spot under the cursor. The
// game reads the spot's character from the grid at 0x3A4140 when a player
// confirms, so switching writes both.
namespace openkai2::mods {

inline constexpr uint32_t select_state_pointers = 0x3A54B8u;
inline constexpr uint32_t select_state_bytes = 1664u;
inline constexpr uint32_t costume_counts = 0x37B310u; // costumes per character ID
inline constexpr uint16_t alternate_button = 0x0200u; // R2, active high

struct SelectAlternates {
    // For each spot, the characters it can hold; the first is the original.
    std::array<std::vector<uint8_t>, roster_select_cells> spots;
    bool empty() const;
};

// Builds the spot lists from the grid in memory, after roster edits, and the
// characters' `alternate` keys.
SelectAlternates build_alternates(const uint8_t *memory, size_t bytes, const std::vector<RosterCharacter> &characters);

// Switches the spot under a player's cursor to its next character when
// `pressed` holds a new R2 press. Returns a line for the log when it does.
// Does nothing unless the player's select-screen state is live and agrees
// with the grid, so other screens are never written.
std::optional<std::string> cycle_alternate(uint8_t *memory, size_t bytes, int player, uint16_t pressed,
                                           const SelectAlternates &alternates);

// Installs the alternates the pad hook uses; empty ones turn it off.
void install_alternates(uint8_t *memory, size_t bytes, SelectAlternates alternates);

} // namespace openkai2::mods
