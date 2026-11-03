// SPDX-License-Identifier: GPL-3.0-only
#include "cpu_difficulty.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace openkai2::cpu {
namespace {

uint32_t read32(const uint8_t *memory, size_t bytes, uint32_t address) {
    address &= 0x1FFFFFFu;
    if (size_t(address) + 4 > bytes)
        return 0;
    uint32_t value;
    std::memcpy(&value, memory + address, 4);
    return value;
}

bool in_battle(const uint8_t *memory, size_t bytes) {
    uint32_t task = read32(memory, bytes, task_first);
    for (unsigned count = 0; task && (task & 0x1FFFFFFu) != task_end && count < 1024; ++count) {
        const uint32_t at = task & 0x1FFFFFFu;
        if (size_t(at) + 48 > bytes)
            return false;
        if (std::memcmp(memory + at, "[BTL]", 5) == 0)
            return true;
        task = read32(memory, bytes, at + 40);
    }
    return false;
}

bool plausible(const Fighter &f) {
    return std::isfinite(f.health) && std::isfinite(f.ki) && f.max_health > 0 && f.max_health <= 100000 &&
           f.health >= -f.max_health && f.health <= f.max_health && f.max_ki > 0 && f.max_ki <= 10000 &&
           f.ki >= 0 && f.ki <= f.max_ki;
}

struct Installed {
    std::mutex mutex;
    uint8_t *memory = nullptr;
    size_t bytes = 0;
    unsigned level = 0;
    bool allowed = false;
    Tracker tracker;
};
Installed &installed() {
    static Installed value;
    return value;
}

}

Edge edge(unsigned level) {
    switch (level) {
    case 1: return {1.10f, 1.00f, 0.03f};
    case 2: return {1.20f, 0.90f, 0.06f};
    case 3: return {1.30f, 0.80f, 0.10f};
    default: return {};
    }
}

const char *level_name(unsigned level) {
    switch (level) {
    case 1: return "Z1";
    case 2: return "Z2";
    case 3: return "Z3";
    default: return "game setting";
    }
}

std::array<bool, 2> cpu_sides(uint8_t mode, uint32_t duel) {
    if (mode == 0)
        return {false, true};
    if (mode == 1)
        switch (duel) {
        case 0: return {false, true};
        case 2: return {true, false};
        default: return {false, false};
        }
    return {false, false};
}

void Tracker::step(std::array<Fighter, 2> &fighters, std::array<bool, 2> cpu, const Edge &edge) {
    const bool scaled = cpu[0] != cpu[1];
    for (size_t side = 0; side < 2; ++side) {
        auto &f = fighters[side];
        const float before = health_[side];
        if (known_ && scaled && f.health < before && before > 0) {
            const float damage = before - f.health;
            if (cpu[side]) {
                if (f.health > 0)
                    f.health = std::min(f.max_health, f.health + damage * (1.0f - edge.taken));
            } else if (f.health > 0) {
                f.health = std::max(1.0f, f.health - damage * (edge.dealt - 1.0f));
            }
        }
        if (cpu[side] && f.health > 0)
            f.ki = std::min(f.max_ki, f.ki + edge.ki);
        health_[side] = f.health;
    }
    known_ = true;
}

void tick(uint8_t *memory, size_t bytes, unsigned level, Tracker &tracker) {
    if (!level || level >= level_count) {
        tracker.reset();
        return;
    }
    const uint32_t battle = read32(memory, bytes, battle_pointer) & 0x1FFFFFFu;
    if (!in_battle(memory, bytes) || read32(memory, bytes, battle_over) == 1u || battle < 0x100000u || size_t(battle) + battle_level + 2 > bytes ||
        size_t(battle) + fighter_health + fighter_stride + 16 > bytes) {
        tracker.reset();
        return;
    }
    const uint8_t mode = memory[battle + battle_mode];
    if (mode <= 2) {
        memory[battle + battle_level] = strongest;
        if (mode == 2)
            memory[battle + battle_level + 1] = strongest;
    }
    std::array<Fighter, 2> fighters;
    for (size_t side = 0; side < 2; ++side) {
        std::memcpy(&fighters[side], memory + battle + fighter_health + side * fighter_stride, sizeof(Fighter));
        if (!plausible(fighters[side])) {
            tracker.reset();
            return;
        }
    }
    if (mode > 2) {
        tracker.reset();
        return;
    }
    const auto before = fighters;
    tracker.step(fighters, cpu_sides(mode, read32(memory, bytes, duel_sides)), edge(level));
    for (size_t side = 0; side < 2; ++side) {
        auto *at = memory + battle + fighter_health + side * fighter_stride;
        if (fighters[side].health != before[side].health)
            std::memcpy(at, &fighters[side].health, 4);
        if (fighters[side].ki != before[side].ki)
            std::memcpy(at + 8, &fighters[side].ki, 4);
    }
}

void install(uint8_t *memory, size_t bytes, unsigned level, bool allowed) {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    state.allowed = allowed;
    state.memory = allowed ? memory : nullptr;
    state.bytes = bytes;
    state.level = allowed && level < level_count ? level : 0;
    state.tracker.reset();
}

bool cycle(unsigned &level) {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    if (!state.allowed || !state.memory)
        return false;
    state.level = (state.level + 1) % level_count;
    state.tracker.reset();
    level = state.level;
    return true;
}

void on_frame() {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    if (state.memory && state.level)
        tick(state.memory, state.bytes, state.level, state.tracker);
}

}
