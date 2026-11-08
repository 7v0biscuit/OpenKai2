// SPDX-License-Identifier: GPL-3.0-only
#include "cpu_difficulty.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace openkai2::cpu;

namespace {
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}

constexpr uint32_t battle = 0x3DFBE0u;

struct Guest {
    std::vector<uint8_t> memory = std::vector<uint8_t>(0x800000);
    void put(uint32_t address, uint32_t value) { std::memcpy(memory.data() + address, &value, 4); }
    void putf(uint32_t address, float value) { std::memcpy(memory.data() + address, &value, 4); }
    float getf(uint32_t address) const {
        float value;
        std::memcpy(&value, memory.data() + address, 4);
        return value;
    }
    uint32_t fighter(int side) const { return battle + fighter_health + uint32_t(side) * fighter_stride; }
    void fight(uint8_t mode, uint32_t duel) {
        put(battle_pointer, 0x80000000u | battle);
        put(task_first, 0x200000);
        std::memcpy(memory.data() + 0x200000, "[BTL] MAIN", 11);
        put(0x200000 + 40, task_end);
        put(battle_over, 0);
        put(duel_sides, duel);
        memory[battle + battle_mode] = mode;
        memory[battle + battle_level] = 1;
        for (int side = 0; side < 2; ++side) {
            putf(fighter(side), 3000);
            putf(fighter(side) + 4, 3000);
            putf(fighter(side) + 8, 300);
            putf(fighter(side) + 12, 700);
        }
    }
    float health(int side) const { return getf(fighter(side)); }
    float ki(int side) const { return getf(fighter(side) + 8); }
};

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

void game_setting_changes_nothing() {
    Guest guest;
    guest.fight(0, 0);
    Tracker tracker;
    tick(guest.memory.data(), guest.memory.size(), 0, tracker);
    guest.putf(guest.fighter(0), 2900);
    tick(guest.memory.data(), guest.memory.size(), 0, tracker);
    require(near(guest.health(0), 2900) && guest.memory[battle + battle_level] == 1, "Game setting changed the fight");
}

void dragon_world_edge() {
    Guest guest;
    guest.fight(0, 0);
    Tracker tracker;
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(guest.memory[battle + battle_level] == strongest, "Z3 did not use the strongest CPU scripts");
    require(near(guest.ki(1), 300 + edge(3).ki) && near(guest.ki(0), 300), "Ki went to the wrong fighter");
    guest.putf(guest.fighter(0), 2900);
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(near(guest.health(0), 2870), "CPU hits were not stronger");
    guest.putf(guest.fighter(1), 2900);
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(near(guest.health(1), 2920), "CPU did not take less damage");
    guest.putf(guest.fighter(1), 0);
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(guest.health(1) == 0, "Knocked out CPU was revived");
    guest.putf(guest.fighter(0), 50);
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(near(guest.health(0), 1), "Extra damage knocked out the player itself");
}

void duel_sides_follow_the_menu() {
    for (uint32_t duel : {0u, 1u, 2u, 3u}) {
        Guest guest;
        guest.fight(1, duel);
        Tracker tracker;
        tick(guest.memory.data(), guest.memory.size(), 2, tracker);
        const bool p1 = guest.ki(0) > 300, p2 = guest.ki(1) > 300;
        require(p1 == (duel == 2) && p2 == (duel == 0), "Duel CPU side wrong for arrangement " + std::to_string(duel));
        guest.putf(guest.fighter(0), 2000);
        tick(guest.memory.data(), guest.memory.size(), 2, tracker);
        const float expected = duel == 0 ? 1800.0f : duel == 2 ? 2100.0f : 2000.0f;
        require(near(guest.health(0), expected), "Duel damage edge wrong for arrangement " + std::to_string(duel));
    }
}

void tournament_preserves_brackets_and_targets_cpu() {
    for (unsigned level = 1; level < level_count; ++level)
        for (unsigned tier = 0; tier < 3; ++tier)
            for (unsigned round = 0; round < 4; ++round)
                for (unsigned arrangement = 0; arrangement < 4; ++arrangement) {
                    Guest guest;
                    guest.fight(2, 0);
                    guest.memory[battle + battle_level] = uint8_t(tier);
                    guest.memory[battle + battle_level + 1] = uint8_t(round);
                    for (unsigned side = 0; side < 2; ++side) {
                        const int16_t controller = (arrangement & (1u << side)) ? -1 : int16_t(side);
                        std::memcpy(guest.memory.data() + battle + fighter_port + side * fighter_stride, &controller, 2);
                    }
                    const auto before = guest.memory;
                    Tracker tracker;
                    tick(guest.memory.data(), guest.memory.size(), level, tracker);
                    for (unsigned side = 0; side < 2; ++side)
                        require(near(guest.ki(side), (arrangement & (1u << side)) ? std::min(700.0f, std::max(300.0f, 700 * edge(level).start_ki) + edge(level).ki) : 300.0f),
                                "Tournament ki modifier reached the wrong side");
                    guest.putf(guest.fighter(0), 2900);
                    tick(guest.memory.data(), guest.memory.size(), level, tracker);
                    const float expected = arrangement == 1 ? 2900 + 100 * (1 - edge(level).taken) + 3000 * edge(level).regen
                        : arrangement == 2 ? 2900 - 100 * (edge(level).dealt - 1) : 2900;
                    require(near(guest.health(0), expected), "Tournament damage modifier reached the wrong side");
                    for (unsigned side = 0; side < 2; ++side)
                        std::memcpy(guest.memory.data() + guest.fighter(side), before.data() + guest.fighter(side), sizeof(Fighter));
                    require(guest.memory == before, "Tournament changed bracket, round, or unrelated memory");
                }
}

void tournament_uses_original_strongest_preset() {
    Guest guest;
    guest.fight(2, 0);
    for (unsigned side = 0; side < 2; ++side) {
        const uint32_t context = ai_context + side * ai_stride;
        guest.put(context + 8, 0x80600000);
        guest.put(0x600038, 0x80601000);
        guest.memory[0x601000] = 7;
        guest.put(0x600010, 0x80602000);
        guest.put(0x60201c, 0x80603000);
        guest.memory[context + 0x15] = 2;
        const int16_t controller = side ? -1 : 0;
        std::memcpy(guest.memory.data() + battle + fighter_port + side * fighter_stride, &controller, 2);
    }
    auto before = guest.memory;
    require(prepare_ai(guest.memory.data(), guest.memory.size(), 0) == 0 && guest.memory == before,
            "Original difficulty changed the AI preset");
    for (unsigned level = 1; level < level_count; ++level) {
        guest.memory = before;
        require(prepare_ai(guest.memory.data(), guest.memory.size(), level) == 1, "Tournament CPU preset not selected");
        require(guest.memory[ai_context + 0x15] == 2 && guest.memory[ai_context + ai_stride + 0x15] == 7,
                "AI selected a hard-coded preset or changed the human side");
        auto expected = before;
        expected[ai_context + ai_stride + 0x15] = 7;
        require(guest.memory == expected, "Preset selection changed active scripts before original initialization");
    }
    guest.memory = before;
    guest.put(0x600038, 0xffffffff);
    before = guest.memory;
    require(prepare_ai(guest.memory.data(), guest.memory.size(), 3) == 0 && guest.memory == before,
            "Invalid AI data changed the battle");
    require(prepare_ai(nullptr, 0, 3) == 0, "Null AI memory accepted");
}

void leaves_other_screens_alone() {
    Guest guest;
    guest.fight(0, 0);
    std::memcpy(guest.memory.data() + 0x200000, "DW MAP", 7);
    Tracker tracker;
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(near(guest.ki(1), 300) && guest.memory[battle + battle_level] == 1, "Changed memory outside a battle");
    guest.fight(0, 0);
    guest.put(battle_over, 1);
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(near(guest.ki(1), 300), "Changed memory after the battle ended");
    guest.fight(0, 0);
    guest.putf(guest.fighter(1) + 4, NAN);
    tick(guest.memory.data(), guest.memory.size(), 3, tracker);
    require(near(guest.ki(1), 300), "Changed a fighter that is not loaded");
}

void edges_grow() {
    require(edge(0).dealt == 1 && edge(0).taken == 1 && edge(0).ki == 0, "Game setting has an edge");
    for (unsigned level = 1; level + 1 < level_count; ++level)
        require(edge(level + 1).dealt > edge(level).dealt && edge(level + 1).taken <= edge(level).taken &&
                    edge(level + 1).ki > edge(level).ki && edge(level + 1).regen >= edge(level).regen &&
                    edge(level + 1).start_ki >= edge(level).start_ki,
                "Z levels do not grow");
    require(level_count == 7 && std::string(level_name(1)) == "Z1" && std::string(level_name(6)) == "Z6", "Level names");
}

void top_levels_recover_and_start_charged() {
    Guest guest;
    guest.fight(0, 0);
    guest.putf(guest.fighter(1) + 8, 0);
    Tracker tracker;
    tick(guest.memory.data(), guest.memory.size(), 6, tracker);
    require(near(guest.ki(1), 700) && near(guest.ki(0), 300), "Z6 did not give only the CPU a full Ki gauge");
    guest.putf(guest.fighter(1), 2000);
    tick(guest.memory.data(), guest.memory.size(), 6, tracker);
    const float hit = guest.health(1);
    require(hit > 2000 + 1000 * 0.5f - 1 && hit < 2000 + 1000 * 0.5f + 3000 * 0.0002f + 1, "Z6 damage reduction or recovery wrong");
    for (int frame = 0; frame < 600; ++frame)
        tick(guest.memory.data(), guest.memory.size(), 6, tracker);
    require(guest.health(1) > hit + 200 && guest.health(1) <= 3000, "Z6 CPU did not recover health over time");
    guest.putf(guest.fighter(1), 0);
    tick(guest.memory.data(), guest.memory.size(), 6, tracker);
    tick(guest.memory.data(), guest.memory.size(), 6, tracker);
    require(guest.health(1) == 0, "A knocked out CPU recovered");
    Guest lower;
    lower.fight(0, 0);
    Tracker plain;
    lower.putf(lower.fighter(1), 2000);
    tick(lower.memory.data(), lower.memory.size(), 3, plain);
    for (int frame = 0; frame < 600; ++frame)
        tick(lower.memory.data(), lower.memory.size(), 3, plain);
    require(near(lower.health(1), 2000), "Z3 recovered health");
}
}

int main() {
    try {
        game_setting_changes_nothing();
        dragon_world_edge();
        duel_sides_follow_the_menu();
        tournament_preserves_brackets_and_targets_cpu();
        tournament_uses_original_strongest_preset();
        leaves_other_screens_alone();
        edges_grow();
        top_levels_recover_and_start_charged();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "cpu difficulty tests passed\n";
    return 0;
}
