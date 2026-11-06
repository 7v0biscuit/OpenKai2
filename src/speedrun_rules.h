// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "speedrun.h"
#include <cstdint>
#include <vector>

namespace openkai2::speedrun {
struct RunProfile {
    Category category = Category::off;
    unsigned cpu = 0;
    bool enabled = false, new_save = false, existing_save = false;
    bool fresh_story = false, very_hard = false;
    int tournament_tier = -1;
};
RunProfile run_profile(Category category, unsigned cpu);
void prepare_run_save(std::vector<uint8_t> &decoded, const RunProfile &profile);

inline constexpr uint32_t game_difficulty_address = 0x694814;
inline constexpr uint32_t com_level_address = 0x694818;
inline constexpr uint32_t story_save_address = 0x69421c;
inline constexpr uint32_t story_stage_address = 0x694296;
inline constexpr size_t story_save_bytes = 0x9c;
inline constexpr uint32_t tournament_tier_address = 0x693f08;
inline constexpr uint32_t tournament_selection_address = 0x3a4ee0;

class RunSettings {
  public:
    explicit RunSettings(RunProfile profile = {}) : profile_(profile) {}
    void apply(uint8_t *memory, size_t bytes);
  private:
    RunProfile profile_;
    bool was_menu_ = false;
};
void install_run_settings(uint8_t *memory, size_t bytes, RunProfile profile);
void enforce_run_settings();
}
