// SPDX-License-Identifier: GPL-3.0-only
#include "mods/select_screen.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace openkai2::mods {
namespace {
// Written once before the game starts and read by the game thread.
struct Layout {
    uint32_t cells = select_cells_original;
    std::array<uint8_t, select_cells_max> icons{}; // portrait cell for each added cell
    std::array<uint8_t, 64> preview{};             // base ID + 1 for filled IDs
} layout;
} // namespace

uint32_t select_cells() { return layout.cells; }
uint32_t select_last() { return layout.cells - 1; }

uint32_t select_offset(uint32_t original) {
    const uint32_t end = select_cells_offset + select_cell_bytes * layout.cells;
    switch (original) {
    case select_cells_offset + select_cell_bytes * (select_cells_original - 1): return end - select_cell_bytes;
    case 1648: return end;
    case 1652: return end + 4;
    case 1664: return end + 16;
    default: return original;
    }
}

uint32_t select_icon(uint32_t cell) {
    if (cell < select_cells_original - 1)
        return cell;
    if (cell == layout.cells - 1)
        return select_cells_original - 1; // Random keeps its sprite
    return cell < select_cells_max ? layout.icons[cell] : 0;
}

uint64_t select_random_bit() { return uint64_t(1) << (layout.cells - 1); }

uint64_t select_extra_mask() {
    // Added cells have no unlock bit in the save, so they are always open.
    const auto below = [](uint32_t bits) { return bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1; };
    return below(layout.cells - 1) & ~below(select_cells_original - 1);
}

uint32_t preview_id(uint32_t id) {
    return id < layout.preview.size() && layout.preview[id] ? layout.preview[id] - 1u : preview_ids - 1;
}

uint32_t preview_row(uint32_t id) {
    return id < layout.preview.size() && layout.preview[id] ? layout.preview[id] - 1u : id;
}

void reset_select_screen() { layout = Layout{}; }

std::vector<std::string> configure_select_screen(uint8_t *memory, size_t bytes,
                                                 const std::vector<RosterCharacter> &characters) {
    reset_select_screen();
    std::vector<std::string> log;
    if (bytes < roster_select_grid + 64)
        return log;
    Layout next;
    std::vector<const RosterCharacter *> added;
    for (const auto &character : characters) {
        // Every filled ID takes its base's preview pose and camera: the rows of
        // the unused IDs below 37 hold a placeholder that frames the model far
        // too close. IDs above 36 also show the base's model there.
        if (character.slot < next.preview.size() && character.base)
            next.preview[character.slot] = uint8_t((*character.base < preview_ids ? *character.base : 36u) + 1u);
        if (character.new_cell &&
            std::none_of(added.begin(), added.end(), [&](const auto *other) { return other->slot == character.slot; }))
            added.push_back(&character);
    }
    if (!added.empty()) {
        if (select_cells_original + added.size() > select_cells_max)
            throw std::runtime_error("Roster mods add " + std::to_string(added.size()) + " select-screen spots; at most " +
                                     std::to_string(select_cells_max - select_cells_original) + " fit");
        next.cells = select_cells_original + uint32_t(added.size());
        uint8_t *grid = memory + roster_select_grid;
        uint8_t *cell_of = memory + select_id_cells;
        for (size_t i = 0; i < added.size(); ++i) {
            const auto &character = *added[i];
            const uint32_t cell = select_cells_original - 1 + uint32_t(i);
            grid[cell] = uint8_t(character.slot);
            cell_of[character.slot] = uint8_t(cell);
            // A new spot borrows the portrait of the cell named by icon, or of
            // its base character's cell.
            uint32_t icon = 0;
            if (character.icon_cell)
                icon = *character.icon_cell;
            else if (character.base && int8_t(cell_of[*character.base]) >= 0 &&
                     uint32_t(cell_of[*character.base]) < select_cells_original - 1)
                icon = cell_of[*character.base];
            next.icons[cell] = uint8_t(icon);
            log.push_back("select: spot " + std::to_string(cell + 1) + " added for ID " + std::to_string(character.slot) +
                          " with the portrait of spot " + std::to_string(icon + 1));
        }
        grid[next.cells - 1] = select_random_id;
        cell_of[select_random_id] = uint8_t(next.cells - 1);
        log.push_back("select: the wheel has " + std::to_string(next.cells) + " spots");
    }
    layout = next;
    return log;
}

} // namespace openkai2::mods
