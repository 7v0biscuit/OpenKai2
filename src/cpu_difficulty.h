// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace openkai2::cpu {

inline constexpr unsigned level_count = 7;
inline constexpr uint32_t battle_pointer = 0x3A4CB4u;
inline constexpr uint32_t battle_over = 0x3A4D60u;
inline constexpr uint32_t task_first = 0x3B5358u;
inline constexpr uint32_t task_end = 0x3B53B0u;
inline constexpr uint32_t duel_sides = 0x3A4F48u;
inline constexpr uint32_t battle_mode = 0x19C7Du;
inline constexpr uint32_t battle_level = 0x19C83u;
// Original input dispatch at 0x1e5ef4: negative port selects CPU commands.
inline constexpr uint32_t fighter_port = 0x97C0u;
inline constexpr uint32_t ai_context = 0x66D450u;
inline constexpr uint32_t ai_stride = 0xA0u;
inline constexpr uint32_t fighter_health = 0x9EA4u;
inline constexpr uint32_t fighter_stride = 0x9F50u;
inline constexpr uint8_t strongest = 4;

struct Edge {
    float dealt = 1, taken = 1, ki = 0, regen = 0, start_ki = 0;
};
Edge edge(unsigned level);
const char *level_name(unsigned level);

struct Fighter {
    float health = 0, max_health = 0, ki = 0, max_ki = 0;
};

std::array<bool, 2> cpu_sides(uint8_t mode, uint32_t duel);

class Tracker {
  public:
    void reset() { known_ = false; reported_ = false; }
    bool observe(uint64_t key) {
        if (reported_ && key_ == key) return false;
        known_ = false; reported_ = true; key_ = key; return true;
    }
    void step(std::array<Fighter, 2> &fighters, std::array<bool, 2> cpu, const Edge &edge);

  private:
    bool known_ = false, reported_ = false;
    uint64_t key_ = 0;
    std::array<float, 2> health_{};
};

void tick(uint8_t *memory, size_t bytes, unsigned level, Tracker &tracker);

// Called before the original AI initializer rebuilds its active parameters.
unsigned prepare_ai(uint8_t *memory, size_t bytes, unsigned level);
void prepare_installed_ai(uint8_t *memory, size_t bytes);

void install(uint8_t *memory, size_t bytes, unsigned level, bool allowed = true, bool locked = false);
bool cycle(unsigned &level);
void on_frame();

}
