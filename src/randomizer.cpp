// SPDX-License-Identifier: GPL-3.0-only
#include "randomizer.h"
#include "cpu_difficulty.h"
#include <cstring>

namespace openkai2::randomizer {
namespace {

uint32_t read32(const uint8_t *memory, size_t bytes, uint32_t address) {
    address &= 0x1FFFFFFu;
    if (size_t(address) + 4 > bytes)
        return 0;
    uint32_t value;
    std::memcpy(&value, memory + address, 4);
    return value;
}

bool starts(const uint8_t *name, const char *prefix) { return std::strncmp(reinterpret_cast<const char *>(name), prefix, std::strlen(prefix)) == 0; }

}

Mode mode_from_tasks(const uint8_t *memory, size_t bytes, Mode previous) {
    if (!memory)
        return previous;
    bool dragon = false, tournament = false, duel = false, babidi = false, menu = false;
    uint32_t task = read32(memory, bytes, cpu::task_first);
    for (unsigned count = 0; task && (task & 0x1FFFFFFu) != cpu::task_end && count < 1024; ++count) {
        const uint32_t at = task & 0x1FFFFFFu;
        if (size_t(at) + 48 > bytes)
            break;
        const uint8_t *name = memory + at;
        if (starts(name, "DW") || starts(name, "#[DW"))
            dragon = true;
        else if (starts(name, "TENK_SYS") || starts(name, "TMAP_") || starts(name, "MTM_") || starts(name, "FG_NOVICE") ||
                 starts(name, "FG_ADEPT") || starts(name, "FG_ADVANCED"))
            tournament = true;
        else if (starts(name, "MNB_"))
            babidi = true;
        else if (starts(name, "VS_SEL") || starts(name, "VS_FIX"))
            duel = true;
        else if (starts(name, "MNC_MAIN") || starts(name, "TITLEMENU") || starts(name, "PRESSSTART"))
            menu = true;
        task = read32(memory, bytes, at + 40);
    }
    if (babidi)
        return Mode::babidi;
    if (duel)
        return Mode::duel;
    if (tournament)
        return Mode::tournament;
    if (dragon)
        return Mode::dragon_world;
    if (menu)
        return Mode::none;
    return previous;
}

void Randomizer::enable(bool on, uint64_t seed) {
    enabled_ = on;
    state_ = seed ? seed : 0x9e3779b97f4a7c15ull;
    last_.store(0xff);
    changes_.store(0);
    staged_ = false;
}

uint8_t Randomizer::pick() {
    for (;;) {
        state_ ^= state_ << 13u;
        state_ ^= state_ >> 7u;
        state_ ^= state_ << 17u;
        const uint8_t id = pool[size_t(state_ % pool.size())];
        if (id != last())
            return id;
    }
}

unsigned Randomizer::prepare(uint8_t *memory, size_t bytes, uint32_t request) {
    const Mode current = mode();
    if (!enabled_ || !memory || (current != Mode::dragon_world && current != Mode::tournament))
        return 0;
    request &= 0x1FFFFFFu;
    if (request < 0x100000u || size_t(request) + request_player_blocks + 2 * request_block_bytes > bytes)
        return 0;
    unsigned changed = 0;
    for (unsigned side = 0; side < 2; ++side) {
        uint8_t *block = memory + request + request_player_blocks + side * request_block_bytes;
        if (int8_t(block[block_port]) < 0)
            continue;
        const uint8_t id = pick();
        const size_t record = size_t(roster_records) + size_t(id) * roster_record_bytes;
        if (record + roster_record_bytes > bytes)
            continue;
        uint16_t count;
        std::memcpy(&count, memory + record + roster_skill_count, 2);
        if (count > block_skill_slots)
            count = 0;
        block[block_character] = id;
        block[block_costume] = 0;
        std::memset(block + block_skills, 0, block_skill_slots * 2);
        std::memcpy(block + block_skill_count, &count, 2);
        std::memcpy(block + block_skills, memory + record + roster_skills, size_t(count) * 2);
        last_.store(id, std::memory_order_relaxed);
        changes_.fetch_add(1, std::memory_order_release);
        ++changed;
    }
    return changed;
}

unsigned Randomizer::load_dragon_world(uint8_t *memory, size_t bytes) {
    if (staged_ || mode() != Mode::dragon_world)
        return 0;
    const unsigned changed = prepare(memory, bytes, dragon_world_request);
    staged_ = changed != 0;
    return changed;
}

unsigned Randomizer::request_battle(uint8_t *memory, size_t bytes, uint32_t request) {
    if ((request & 0x1FFFFFFu) == dragon_world_request) {
        staged_ = false;
        return 0;
    }
    return prepare(memory, bytes, request);
}

Randomizer &active() {
    static Randomizer value;
    return value;
}

const char *display_name(uint8_t id) {
    static constexpr const char *names[] = {
        "Goku", "Kid Gohan", "Teen Gohan", "Vegeta", "Krillin", "Trunks", "Piccolo", "Tien", "Yamcha", "Raditz",
        "Nappa", "Captain Ginyu", "Recoome", "Zarbon", "Dodoria", "Frieza", "Android 16", "Android 17", "Android 18",
        "Android 19", "Cell", "Hercule", "Saibaman", "Cell Jr.", "Great Saiyaman", "Dr. Gero", "Goten", "Kid Trunks",
        "Gohan", "Majin Buu", "Super Buu", "Kid Buu", "Dabura", "Videl", "Majin Vegeta", "Supreme Kai"};
    return id < sizeof(names) / sizeof(names[0]) ? names[id] : "Unknown";
}

}
