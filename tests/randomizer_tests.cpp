// SPDX-License-Identifier: GPL-3.0-only
#include "cpu_difficulty.h"
#include "randomizer.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace openkai2::randomizer;

namespace {
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}

constexpr uint32_t request = 0x500000u;

struct Guest {
    std::vector<uint8_t> memory = std::vector<uint8_t>(0x800000);
    Guest() {
        for (unsigned id = 0; id < 53; ++id) {
            const uint32_t record = roster_records + id * roster_record_bytes;
            const uint16_t count = uint16_t(1 + id % 7);
            std::memcpy(memory.data() + record + roster_skill_count, &count, 2);
            for (uint16_t i = 0; i < count; ++i) {
                const uint16_t skill = uint16_t(id * 16 + i);
                std::memcpy(memory.data() + record + roster_skills + i * 2, &skill, 2);
            }
        }
        uint8_t *player = block(0), *cpu = block(1);
        player[block_character] = 0;
        player[block_port] = 0;
        cpu[block_character] = 9;
        cpu[block_port] = 0xff;
        cpu[block_skill_count] = 5;
        for (unsigned i = 0; i < 5; ++i)
            cpu[block_skills + i * 2] = uint8_t(0x74 + i);
    }
    uint8_t *block(unsigned side) { return memory.data() + request + request_player_blocks + side * request_block_bytes; }
    void tasks(std::vector<std::string> names) {
        uint32_t at = 0x200000, previous = openkai2::cpu::task_first;
        for (const auto &name : names) {
            std::memcpy(memory.data() + previous + (previous == openkai2::cpu::task_first ? 0 : 40), &at, 4);
            std::memset(memory.data() + at, 0, 48);
            std::memcpy(memory.data() + at, name.c_str(), name.size());
            previous = at;
            at += 64;
        }
        const uint32_t end = openkai2::cpu::task_end;
        std::memcpy(memory.data() + previous + (previous == openkai2::cpu::task_first ? 0 : 40), &end, 4);
    }
};

void only_the_player_changes() {
    Guest guest;
    Randomizer randomizer;
    randomizer.enable(true, 1234);
    randomizer.observe(Mode::tournament);
    std::vector<uint8_t> cpu(guest.block(1), guest.block(1) + request_block_bytes);
    std::set<uint8_t> seen;
    uint8_t previous = 0xff;
    for (unsigned fight = 0; fight < 200; ++fight) {
        require(randomizer.prepare(guest.memory.data(), guest.memory.size(), request) == 1u, "The player was not randomized");
        const uint8_t id = guest.block(0)[block_character];
        require(std::find(pool.begin(), pool.end(), id) != pool.end(), "A character outside the roster was chosen");
        require(id != previous, "The same character came twice in a row");
        previous = id;
        seen.insert(id);
        uint16_t count;
        std::memcpy(&count, guest.block(0) + block_skill_count, 2);
        require(count == 1 + id % 7, "The new character did not get its own skills");
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t skill;
            std::memcpy(&skill, guest.block(0) + block_skills + i * 2, 2);
            require(skill == id * 16 + i, "A skill of the new character is wrong");
        }
        require(std::equal(cpu.begin(), cpu.end(), guest.block(1)), "The CPU fighter was changed");
    }
    require(seen.size() > 20u, "The randomizer kept choosing the same few characters");
}

void only_dragon_world_and_tournament() {
    for (Mode mode : {Mode::none, Mode::duel, Mode::babidi}) {
        Guest guest;
        Randomizer randomizer;
        randomizer.enable(true, 99);
        randomizer.observe(mode);
        const auto before = guest.memory;
        require(randomizer.prepare(guest.memory.data(), guest.memory.size(), request) == 0u && guest.memory == before,
                "The randomizer changed a fight outside Dragon World and the World Tournament");
    }
    Guest guest;
    Randomizer off;
    off.observe(Mode::dragon_world);
    const auto before = guest.memory;
    require(off.prepare(guest.memory.data(), guest.memory.size(), request) == 0u && guest.memory == before, "A disabled randomizer changed the fight");
    Randomizer on;
    on.enable(true, 5);
    on.observe(Mode::dragon_world);
    require(on.prepare(guest.memory.data(), guest.memory.size(), request) == 1u, "Dragon World fights were not randomized");
    require(on.prepare(nullptr, 0, request) == 0u && on.prepare(guest.memory.data(), guest.memory.size(), 0x10u) == 0u,
            "An invalid request was accepted");
}

void modes_follow_the_screens() {
    Guest guest;
    guest.tasks({"MNC_MAIN", "MNU_ITEM"});
    require(mode_from_tasks(guest.memory.data(), guest.memory.size(), Mode::tournament) == Mode::none, "The main menu kept an old mode");
    guest.tasks({"TENK_SYS", "TMAP_SYS"});
    require(mode_from_tasks(guest.memory.data(), guest.memory.size(), Mode::none) == Mode::tournament, "The World Tournament was not recognised");
    guest.tasks({"DW_MUSIC", "DWC_CTRL"});
    require(mode_from_tasks(guest.memory.data(), guest.memory.size(), Mode::none) == Mode::dragon_world, "Dragon World was not recognised");
    guest.tasks({"MNB_SYS", "MNB_DRAW"});
    require(mode_from_tasks(guest.memory.data(), guest.memory.size(), Mode::dragon_world) == Mode::babidi, "Babidi's Spaceship was not recognised");
    guest.tasks({"VS_SEL", "VS_FIX"});
    require(mode_from_tasks(guest.memory.data(), guest.memory.size(), Mode::none) == Mode::duel, "Dueling was not recognised");
    guest.tasks({"[BTL] MAIN"});
    require(mode_from_tasks(guest.memory.data(), guest.memory.size(), Mode::tournament) == Mode::tournament, "A fight forgot the mode it came from");
    require(std::string(display_name(0)) == "Goku" && std::string(display_name(9)) == "Raditz", "Character names");
}
}

int main() {
    try {
        only_the_player_changes();
        only_dragon_world_and_tournament();
        modes_follow_the_screens();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "randomizer tests passed\n";
    return 0;
}
