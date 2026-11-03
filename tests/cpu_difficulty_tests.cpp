// SPDX-License-Identifier: GPL-3.0-only
#include "cpu_difficulty.h"
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
    for (unsigned level = 1; level < 3; ++level)
        require(edge(level + 1).dealt > edge(level).dealt && edge(level + 1).taken <= edge(level).taken &&
                    edge(level + 1).ki > edge(level).ki,
                "Z levels do not grow");
    require(std::string(level_name(1)) == "Z1" && std::string(level_name(3)) == "Z3", "Level names");
}
}

int main() {
    try {
        game_setting_changes_nothing();
        dragon_world_edge();
        duel_sides_follow_the_menu();
        leaves_other_screens_alone();
        edges_grow();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "cpu difficulty tests passed\n";
    return 0;
}
