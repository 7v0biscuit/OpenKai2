// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/roster.h"
#include <cstdint>
#include <string>
#include <vector>

namespace openkai2::mods {

inline constexpr uint32_t select_cells_original = 30u;
inline constexpr uint32_t select_cells_max = 64u;
inline constexpr uint32_t select_cell_bytes = 48u, select_cells_offset = 208u;
inline constexpr uint32_t select_id_cells = 0x3A4100u;
inline constexpr uint32_t select_random_id = 0x3Fu;
inline constexpr uint32_t preview_ids = 37u;

uint32_t select_cells();
uint32_t select_last();
uint32_t select_offset(uint32_t original);
uint32_t select_icon(uint32_t cell);
uint64_t select_random_bit();
uint64_t select_extra_mask();
uint32_t preview_id(uint32_t id);
uint32_t preview_row(uint32_t id);
uint32_t id_table_row(uint32_t id);

std::vector<std::string> configure_select_screen(uint8_t *memory, size_t bytes,
                                                 const std::vector<RosterCharacter> &characters);
void reset_select_screen();

}
