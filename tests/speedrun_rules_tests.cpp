// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun_rules.h"
#include "launcher/content_unlock.h"
#include "launcher/speedrun_session.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace openkai2;
namespace {
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
std::vector<uint8_t> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(input)), {}};
}
void put(std::vector<uint8_t> &memory, uint32_t at, uint32_t value) {
    std::memcpy(memory.data() + at, &value, 4);
}
uint32_t word(const std::vector<uint8_t> &memory, uint32_t at) {
    uint32_t value;
    std::memcpy(&value, memory.data() + at, 4);
    return value;
}
}
int main() {
    using namespace speedrun;
    try {
        std::vector<uint8_t> plain(0xab0);
        for (unsigned i = 0x30; i < plain.size(); ++i) plain[i] = uint8_t(i * 17 + 9);
        std::memcpy(plain.data(), "#SPF 1.0", 8);
        plain[0x60] = 1; plain[0x61] = 2; plain[0x87a] = 1;
        for (unsigned category = 0; category < category_count; ++category)
            for (unsigned cpu = 0; cpu <= 3; ++cpu) {
                const auto profile = run_profile(Category(category), cpu);
                auto prepared = plain;
                prepare_run_save(prepared, profile);
                const auto decoded = launcher::decode_progress_save(launcher::encode_progress_save(prepared));
                for (size_t i = 0x30; i < plain.size(); ++i) {
                    const bool changed = (profile.fresh_story && i >= 0x800 && i < 0x89c) ||
                        (profile.very_hard && i == 0x60) || (profile.enabled && cpu && i == 0x61);
                    if (!changed) require(decoded[i] == plain[i], "Run setup changed unrelated save content");
                }
                if (profile.fresh_story) {
                    require(decoded[0x87a] == 0xff, "Run would resume an unfinished Dragon World stage");
                    for (size_t i = 0x800; i < 0x89c; ++i)
                        require(decoded[i] == (i == 0x87a ? 0xff : 0), "Previous story state survived setup");
                }
                if (profile.very_hard) require(decoded[0x60] == 4, "Required Very Hard setting was not applied");
                std::vector<uint8_t> memory(0x800000, 0x5a);
                put(memory, task_first, 0x200000);
                std::memcpy(memory.data() + 0x200000, "MOKUJI", 7);
                put(memory, 0x200000 + task_next, task_end);
                RunSettings settings(profile);
                settings.apply(memory.data(), memory.size());
                if (profile.very_hard) require(word(memory, game_difficulty_address) == 4, "Runtime difficulty is not locked");
                if (profile.enabled && cpu) require(word(memory, com_level_address) == 4, "Z difficulty did not select strongest COM scripts");
                if (profile.fresh_story) {
                    require(memory[story_stage_address] == 0xff, "Main menu can continue the old stage");
                    memory[story_save_address] = 7;
                    settings.apply(memory.data(), memory.size());
                    require(memory[story_save_address] == 7, "Same menu was reset repeatedly");
                    put(memory, task_first, 0); settings.apply(memory.data(), memory.size());
                    put(memory, task_first, 0x200000); settings.apply(memory.data(), memory.size());
                    require(memory[story_save_address] == 0, "Returning to the menu retained old story progress");
                }
                if (profile.tournament_tier >= 0) {
                    std::memcpy(memory.data() + 0x200000, "TENK_SYS", 9);
                    put(memory, 0x200000 + 0x1c, 0x241b60);
                    put(memory, tournament_selection_address + 4, 3);
                    settings.apply(memory.data(), memory.size());
                    require(word(memory, tournament_selection_address) == unsigned(profile.tournament_tier) &&
                                word(memory, tournament_tier_address) == unsigned(profile.tournament_tier),
                            "World Tournament category did not select its matching bracket");
                    put(memory, tournament_selection_address + 4, 0);
                    put(memory, tournament_selection_address, 0);
                    settings.apply(memory.data(), memory.size());
                    require(word(memory, tournament_selection_address) == 0,
                            "Tournament setup selected a missing menu entry");
                }
                settings.apply(nullptr, 0);
            }
        const auto root = std::filesystem::temp_directory_path() /
            ("openkai2-run-card-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);} } cleanup{root};
        const auto card = root / "mc0", file = card / "BASLUS-20779DBZ2" / "BASLUS-20779DBZ2";
        std::filesystem::create_directories(file.parent_path());
        const auto encoded = launcher::encode_progress_save(plain);
        {std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());}
        std::filesystem::path working;
        {
            launcher::SpeedrunCards session;
            working = session.prepare(card, run_profile(Category::new_game_plus_very_hard, 0));
            const auto updated = launcher::decode_progress_save(read(working / "BASLUS-20779DBZ2" / "BASLUS-20779DBZ2"));
            require(updated[0x60] == 4 && updated[0x87a] == 0xff, "NG+ session setup did not reach its working card");
            require(read(file) == encoded, "Speedrun changed the original save");
        }
        require(!std::filesystem::exists(working) && read(file) == encoded, "Closing a run kept its working card or removed the original");
        {
            launcher::SpeedrunCards session;
            working = session.prepare(card, run_profile(Category::any, 3));
            require(std::filesystem::is_empty(working), "Any% loaded existing unlocks or story progress");
        }
        bool rejected = false;
        try { launcher::SpeedrunCards session;session.prepare(root / "missing",run_profile(Category::new_game_plus,0)); }
        catch (const std::runtime_error &) { rejected = true; }
        require(rejected, "NG+ silently started without an existing save");
        auto locked = plain;
        locked[0xa93] = locked[0xa94] = 0;
        rejected = false;
        try { prepare_run_save(locked,run_profile(Category::tournament_advanced,0)); }
        catch (const std::runtime_error &) { rejected = true; }
        require(rejected, "Advanced Tournament was entered without its unlock requirements");
        std::cout << "Speedrun category setup, fresh attempts, locked settings and original-save preservation passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n';return 1; }
}
