// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun_rules.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace openkai2::speedrun {
namespace {
bool is_tournament(Category category) {
    return category >= Category::tournament_novice && category <= Category::tournament_advanced;
}
uint32_t word(const uint8_t *memory, size_t bytes, uint32_t address) {
    address &= 0x1ffffff;
    uint32_t value = 0;
    if (address <= bytes && sizeof(value) <= bytes - address)
        std::memcpy(&value, memory + address, sizeof(value));
    return value;
}
bool main_menu(const uint8_t *memory, size_t bytes) {
    uint32_t task = word(memory, bytes, task_first);
    for (unsigned count = 0; task && count < task_limit; ++count) {
        task &= 0x1ffffff;
        if (task == task_end || task > bytes || task_next + 4 > bytes - task)
            return false;
        if (std::memcmp(memory + task, "MOKUJI", 7) == 0)
            return true;
        task = word(memory, bytes, task + task_next);
    }
    return false;
}
uint32_t tournament_menu(const uint8_t *memory, size_t bytes) {
    uint32_t task = word(memory, bytes, task_first);
    for (unsigned count = 0; task && count < task_limit; ++count) {
        task &= 0x1ffffff;
        if (task == task_end || task > bytes || task_next + 4 > bytes - task)
            return 0;
        if (std::memcmp(memory + task, "TENK_SYS", 9) == 0)
            return word(memory, bytes, task + 0x1c);
        task = word(memory, bytes, task + task_next);
    }
    return 0;
}
struct Installed {
    std::mutex mutex;
    uint8_t *memory = nullptr;
    size_t bytes = 0;
    RunSettings settings;
};
Installed &installed() { static Installed state; return state; }
}

RunProfile run_profile(Category category, unsigned cpu) {
    if (unsigned(category) >= category_count || cpu > 6)
        throw std::runtime_error("Invalid speedrun category or Z difficulty");
    RunProfile profile;
    profile.category = category;
    profile.cpu = cpu;
    profile.enabled = category != Category::off;
    profile.new_save = category == Category::any;
    profile.existing_save = category == Category::new_game_plus || category == Category::new_game_plus_very_hard;
    profile.fresh_story = profile.enabled && !is_tournament(category);
    profile.very_hard = category == Category::new_game_plus_very_hard || (profile.enabled && cpu != 0);
    if (is_tournament(category)) {
        profile.tournament_tier = int(category) - int(Category::tournament_novice);
        profile.existing_save = profile.tournament_tier != 0;
    }
    if (category == Category::hercule_only) profile.existing_save = true;
    return profile;
}

void prepare_run_save(std::vector<uint8_t> &decoded, const RunProfile &profile) {
    if (!profile.enabled)
        return;
    if (decoded.size() != 0xab0 || std::memcmp(decoded.data(), "#SPF 1.0", 8) != 0)
        throw std::runtime_error("Unsupported Budokai 2 speedrun save");
    if (profile.category == Category::hercule_only && decoded[0x89e + 472] == 0)
        throw std::runtime_error("Hercule Only needs a save with Hercule unlocked");
    if (profile.tournament_tier > 0) {
        // The game's enrollment menu checks the roster count and tier capsule.
        constexpr unsigned roster[] = {461,0,462,465,469,467,468,470,471,476,477,479,478,0,0,480,481,482,
            483,0,485,473,0,0,472,484,464,466,463,487,488,489,486,474,0,475};
        unsigned characters = 0;
        for (auto capsule : roster) characters += decoded[0x89e + capsule] != 0;
        if (!decoded[0xa92 + profile.tournament_tier] ||
            characters < (profile.tournament_tier == 1 ? 16u : 29u))
            throw std::runtime_error("Unlock the selected World Tournament tier before starting this category");
    }
    if (profile.fresh_story) {
        // The original Restart Dragon World clears this same 0x9c-byte block.
        // -1 makes the main menu start a new story rather than offer Continue.
        std::fill(decoded.begin() + 0x800, decoded.begin() + 0x89c, 0);
        decoded[0x87a] = 0xff;
    }
    if (profile.very_hard)
        decoded[0x60] = 4;
    if (profile.cpu)
        decoded[0x61] = 4;
}

void RunSettings::apply(uint8_t *memory, size_t bytes) {
    if (!profile_.enabled || !memory || bytes < com_level_address + 4)
        return;
    const uint32_t strongest = 4;
    if (profile_.very_hard)
        std::memcpy(memory + game_difficulty_address, &strongest, 4);
    if (profile_.cpu)
        std::memcpy(memory + com_level_address, &strongest, 4);
    if (profile_.tournament_tier >= 0) {
        const uint32_t tier = uint32_t(profile_.tournament_tier);
        std::memcpy(memory + tournament_tier_address, &tier, 4);
        const auto controller = tournament_menu(memory, bytes);
        if ((controller == 0x241b60 || controller == 0x241880) &&
            word(memory, bytes, tournament_selection_address + 4) > tier)
            std::memcpy(memory + tournament_selection_address, &tier, 4);
    }
    const bool menu = profile_.fresh_story && main_menu(memory, bytes);
    if (menu && !was_menu_) {
        std::memset(memory + story_save_address, 0, story_save_bytes);
        memory[story_stage_address] = 0xff;
    }
    was_menu_ = menu;
}

void install_run_settings(uint8_t *memory, size_t bytes, RunProfile profile) {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    state.memory = memory;
    state.bytes = bytes;
    state.settings = RunSettings{profile};
}
void enforce_run_settings() {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    state.settings.apply(state.memory, state.bytes);
}
}
