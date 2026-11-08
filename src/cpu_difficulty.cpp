// SPDX-License-Identifier: GPL-3.0-only
#include "cpu_difficulty.h"
#include "speedrun_rules.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <iostream>

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

int16_t port(const uint8_t *memory, uint32_t battle, unsigned side) {
    int16_t value;
    std::memcpy(&value, memory + battle + fighter_port + side * fighter_stride, 2);
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
    bool allowed = false, locked = false;
    Tracker tracker;
};
Installed &installed() {
    static Installed value;
    return value;
}

}

Edge edge(unsigned level) {
    switch (level) {
    case 1: return {1.10f, 1.00f, 0.03f, 0.0f, 0.0f};
    case 2: return {1.20f, 0.90f, 0.06f, 0.0f, 0.10f};
    case 3: return {1.30f, 0.80f, 0.10f, 0.0f, 0.25f};
    case 4: return {1.40f, 0.70f, 0.14f, 0.00005f, 0.40f};
    case 5: return {1.55f, 0.60f, 0.18f, 0.00010f, 0.60f};
    case 6: return {1.70f, 0.50f, 0.25f, 0.00015f, 1.00f};
    default: return {};
    }
}

const char *level_name(unsigned level) {
    switch (level) {
    case 1: return "Z1";
    case 2: return "Z2";
    case 3: return "Z3";
    case 4: return "Z4";
    case 5: return "Z5";
    case 6: return "Z6";
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
        if (cpu[side] && f.health > 0) {
            if (!known_)
                f.ki = std::max(f.ki, std::min(f.max_ki, f.max_ki * edge.start_ki));
            f.ki = std::min(f.max_ki, f.ki + edge.ki);
            if (scaled)
                f.health = std::min(f.max_health, f.health + f.max_health * edge.regen);
        }
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
    // Tournament uses these bytes for tier and round, not COM difficulty.
    if (mode <= 1)
        memory[battle + battle_level] = strongest;
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
    const auto sides = mode == 2
        ? std::array<bool, 2>{port(memory, battle, 0) < 0, port(memory, battle, 1) < 0}
        : cpu_sides(mode, read32(memory, bytes, duel_sides));
    const uint64_t key = uint64_t(battle) | (uint64_t(mode) << 25) | (uint64_t(level) << 28) |
        (uint64_t(sides[0]) << 30) | (uint64_t(sides[1]) << 31) |
        (uint64_t(memory[battle + battle_level]) << 32) |
        (uint64_t(memory[battle + battle_level + 1]) << 40);
    if (tracker.observe(key)) {
        std::cout << "[cpu:battle] mode=" << unsigned(mode) << " difficulty=" << level_name(level)
                  << " cpu_left=" << sides[0] << " cpu_right=" << sides[1];
        if (mode == 2)
            std::cout << " tier=" << unsigned(memory[battle + battle_level])
                      << " round=" << unsigned(memory[battle + battle_level + 1]);
        std::cout << std::endl;
    }
    const auto before = fighters;
    tracker.step(fighters, sides, edge(level));
    for (size_t side = 0; side < 2; ++side) {
        auto *at = memory + battle + fighter_health + side * fighter_stride;
        if (fighters[side].health != before[side].health)
            std::memcpy(at, &fighters[side].health, 4);
        if (fighters[side].ki != before[side].ki)
            std::memcpy(at + 8, &fighters[side].ki, 4);
    }
}

unsigned prepare_ai(uint8_t *memory, size_t bytes, unsigned level) {
    if (!memory || !level || level >= level_count) return 0;
    const uint32_t battle = read32(memory, bytes, battle_pointer) & 0x1FFFFFFu;
    if (battle < 0x100000u || size_t(battle) + battle_level + 2 > bytes ||
        size_t(battle) + fighter_port + fighter_stride + 2 > bytes ||
        memory[battle + battle_mode] > 2) return 0;
    const unsigned mode = memory[battle + battle_mode];
    unsigned applied = 0;
    for (unsigned side = 0; side < 2; ++side) {
        if (port(memory, battle, side) >= 0) continue;
        const uint32_t context = ai_context + side * ai_stride;
        if (size_t(context) + 0x17 > bytes) continue;
        const uint32_t file = read32(memory, bytes, context + 8) & 0x1FFFFFFu;
        if (file < 0x100000u || size_t(file) + 0x3c > bytes) continue;
        // Original tables resolve a difficulty to a preset, not necessarily 4.
        // Tournament uses the duel's Very Strong preset without changing its tier.
        const uint32_t selection = read32(memory, bytes, file + (mode == 0 ? 0x14 : 0x28) + strongest * 4) & 0x1FFFFFFu;
        const unsigned opponent = mode == 0 ? memory[battle + 0x143a3] : 0;
        if (selection < 0x100000u || size_t(selection) + opponent >= bytes) continue;
        const unsigned preset = memory[selection + opponent];
        const uint32_t table = read32(memory, bytes, file + 0x10) & 0x1FFFFFFu;
        if (preset >= 128 || table < 0x100000u || size_t(table) + preset * 4 + 4 > bytes) continue;
        const uint32_t parameters = read32(memory, bytes, table + preset * 4) & 0x1FFFFFFu;
        if (parameters < 0x100000u || size_t(parameters) + 0x11f > bytes) continue;
        memory[context + 0x15] = uint8_t(preset);
        ++applied;
        std::cout << "[cpu:ai] mode=" << mode << " side=" << side << " difficulty=" << level_name(level)
                  << " preset=" << preset << std::endl;
    }
    return applied;
}

void prepare_installed_ai(uint8_t *memory, size_t bytes) {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    if (state.memory == memory && state.bytes == bytes)
        prepare_ai(memory, bytes, state.level);
}

void install(uint8_t *memory, size_t bytes, unsigned level, bool allowed, bool locked) {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    state.allowed = allowed;
    state.locked = locked;
    state.memory = allowed ? memory : nullptr;
    state.bytes = bytes;
    state.level = allowed && level < level_count ? level : 0;
    state.tracker.reset();
}

bool cycle(unsigned &level) {
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    if (!state.allowed || !state.memory || state.locked)
        return false;
    state.level = (state.level + 1) % level_count;
    state.tracker.reset();
    level = state.level;
    return true;
}

void on_frame() {
    speedrun::enforce_run_settings();
    auto &state = installed();
    std::lock_guard lock(state.mutex);
    if (state.memory && state.level)
        tick(state.memory, state.bytes, state.level, state.tracker);
}

}
