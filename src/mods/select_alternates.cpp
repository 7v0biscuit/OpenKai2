// SPDX-License-Identifier: GPL-3.0-only
#include "mods/select_alternates.h"
#include "host_input.h"
#include <algorithm>
#include <iostream>
#include <mutex>

namespace openkai2::mods {
namespace {
uint32_t read32(const uint8_t *memory, uint32_t at) {
    return memory[at] | memory[at + 1] << 8 | memory[at + 2] << 16 | uint32_t(memory[at + 3]) << 24;
}

struct Installed {
    std::mutex mutex;
    uint8_t *memory = nullptr;
    size_t bytes = 0;
    SelectAlternates alternates;
    std::array<uint16_t, 2> held{};
};
Installed &installed() {
    static Installed value;
    return value;
}
} // namespace

bool SelectAlternates::empty() const {
    return std::all_of(spots.begin(), spots.end(), [](const auto &spot) { return spot.size() < 2; });
}

SelectAlternates build_alternates(const uint8_t *memory, size_t bytes, const std::vector<RosterCharacter> &characters) {
    SelectAlternates result;
    if (bytes < roster_select_grid + roster_select_cells)
        return result;
    for (uint32_t spot = 0; spot < roster_select_cells; ++spot)
        result.spots[spot].push_back(memory[roster_select_grid + spot]);
    for (const auto &character : characters)
        for (const auto spot : character.alternate_cells) {
            auto &list = result.spots[spot];
            if (std::find(list.begin(), list.end(), uint8_t(character.slot)) == list.end())
                list.push_back(uint8_t(character.slot));
        }
    return result;
}

std::optional<std::string> cycle_alternate(uint8_t *memory, size_t bytes, int player, uint16_t pressed,
                                           const SelectAlternates &alternates) {
    if (!(pressed & alternate_button) || player < 0 || player > 1 || bytes < roster_select_grid + 64)
        return std::nullopt;
    const uint32_t state = read32(memory, select_state_pointers + 4u * uint32_t(player)) & 0x1fffffffu;
    if (state < 0x100000u || state + select_state_bytes > bytes || memory[state + 88] != player)
        return std::nullopt;
    const uint32_t spot = read32(memory, state + 192);
    if (spot >= roster_select_cells)
        return std::nullopt;
    const auto &list = alternates.spots[spot];
    const uint8_t current = memory[state + 128];
    const auto at = std::find(list.begin(), list.end(), current);
    if (list.size() < 2 || at == list.end() || memory[roster_select_grid + spot] != current)
        return std::nullopt;
    const uint8_t next = *(at + 1 == list.end() ? list.begin() : at + 1);
    memory[roster_select_grid + spot] = next;
    memory[state + 128] = next;
    memory[state + 129] = 0; // the first costume, which every character has
    return "select: player " + std::to_string(player + 1) + " switched spot " + std::to_string(spot + 1) +
           " from ID " + std::to_string(current) + " to ID " + std::to_string(next);
}

void install_alternates(uint8_t *memory, size_t bytes, SelectAlternates alternates) {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    state.memory = alternates.empty() ? nullptr : memory;
    state.bytes = bytes;
    state.alternates = std::move(alternates);
    state.held = {};
}

} // namespace openkai2::mods

namespace openkai2 {
// Called with each pad packet the game reads. Buttons are active low.
void observe_pad_packet(int port, const uint8_t *packet) {
    auto &state = mods::installed();
    std::lock_guard lock(state.mutex);
    if (!state.memory || port < 0 || port > 1)
        return;
    const uint16_t down = uint16_t(~(packet[2] | packet[3] << 8));
    const uint16_t pressed = uint16_t(down & ~state.held[size_t(port)]);
    state.held[size_t(port)] = down;
    if (const auto line = mods::cycle_alternate(state.memory, state.bytes, port, pressed, state.alternates))
        std::cout << "[mods] " << *line << std::endl;
}
} // namespace openkai2
