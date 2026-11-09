// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace openkai2::randomizer {

inline constexpr uint32_t battle_request_entry = 0x1d6cd0u;
inline constexpr uint32_t battle_state = 0x3DFBE0u, battle_keep_flag = 0x19C8Fu;
inline constexpr uint32_t request_player_blocks = 16u, request_block_bytes = 64u;
inline constexpr uint32_t block_character = 16u, block_costume = 17u, block_port = 19u;
inline constexpr uint32_t block_skill_count = 30u, block_skills = 32u, block_skill_slots = 7u;
inline constexpr uint32_t roster_records = 0x389D40u, roster_record_bytes = 712u, roster_name_bytes = 32u;
inline constexpr uint32_t roster_skill_count = 0x2B8u, roster_skills = 0x2BAu;
inline constexpr std::array<uint8_t, 29> pool = {0, 2, 28, 24, 33, 6, 4, 8, 7, 21, 35, 32, 29, 30, 31,
                                                 25, 20, 18, 17, 16, 15, 11, 12, 9, 10, 3, 5, 27, 26};

enum class Mode : unsigned { none, dragon_world, tournament, duel, babidi };

Mode mode_from_tasks(const uint8_t *memory, size_t bytes, Mode previous);

class Randomizer {
  public:
    void enable(bool on, uint64_t seed);
    bool enabled() const { return enabled_; }
    void observe(Mode mode) { mode_.store(mode, std::memory_order_relaxed); }
    Mode mode() const { return mode_.load(std::memory_order_relaxed); }
    unsigned prepare(uint8_t *memory, size_t bytes, uint32_t request);
    uint8_t last() const { return last_.load(std::memory_order_relaxed); }
    uint64_t changes() const { return changes_.load(std::memory_order_acquire); }

  private:
    uint8_t pick();
    bool enabled_ = false;
    std::atomic<Mode> mode_{Mode::none};
    uint64_t state_ = 0x9e3779b97f4a7c15ull;
    std::atomic<uint64_t> changes_{0};
    std::atomic<uint8_t> last_{0xff};
};

Randomizer &active();
const char *display_name(uint8_t id);

}
