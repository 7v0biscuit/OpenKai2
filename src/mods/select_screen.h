// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/roster.h"
#include <cstdint>
#include <string>
#include <vector>

// The character select screen is a wheel of cells. Each player's state block
// holds 30 cells of 48 bytes from offset 208, the last being Random, then two
// words at 1648 and 1652; the block is 1664 bytes. The cell for each
// character ID is at 0x3A4100 (ID 63 is Random), the character in each cell
// at 0x3A4140, and cell c draws sprite 36 + c of the select sprite sheet.
//
// tools/native/patch-select-screen.py makes the translated select-screen
// code read the cell count, the offsets that follow the cells and the
// portrait sprite from the functions below. They return the original
// values unless a roster mod adds cells, so the unmodded screen is unchanged.
namespace openkai2::mods {

inline constexpr uint32_t select_cells_original = 30u;
inline constexpr uint32_t select_cells_max = 64u; // the unlock mask has 64 bits
inline constexpr uint32_t select_cell_bytes = 48u, select_cells_offset = 208u;
inline constexpr uint32_t select_id_cells = 0x3A4100u; // cell per character ID
inline constexpr uint32_t select_random_id = 0x3Fu;
// The model preview's tables cover IDs below 37; higher IDs were shown as 36.
inline constexpr uint32_t preview_ids = 37u;

// The values the translated code uses.
uint32_t select_cells();
uint32_t select_last();
uint32_t select_offset(uint32_t original);
uint32_t select_icon(uint32_t cell);
uint64_t select_random_bit();
uint64_t select_extra_mask();
uint32_t preview_id(uint32_t id);  // in place of the clamp to 36
uint32_t preview_row(uint32_t id); // a pose-table row: the base's for filled IDs above 36

// Adds a cell before Random for each character whose roster.ini asks for a
// new spot, and points IDs above 36 at their base character's preview.
// Writes the grid and the cell map; returns lines for the startup log.
std::vector<std::string> configure_select_screen(uint8_t *memory, size_t bytes,
                                                 const std::vector<RosterCharacter> &characters);
// Restores the original screen.
void reset_select_screen();

} // namespace openkai2::mods
