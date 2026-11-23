// SPDX-License-Identifier: GPL-3.0-only
#include "achievements.h"
#include "disc_image.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace openkai2::achievements {

const Definition *find(const std::string &id) {
    for (const auto &definition : definitions)
        if (id == definition.id)
            return &definition;
    return nullptr;
}

unsigned total_points() {
    unsigned total = 0;
    for (const auto &definition : definitions)
        total += definition.points;
    return total;
}

unsigned Progress::points() const {
    unsigned total = 0;
    for (const auto &[id, stamp] : unlocked)
        if (const auto *definition = find(id))
            total += definition->points;
    return total;
}

unsigned Progress::count(const Definition &definition) const {
    const std::string id = definition.id;
    if (has(id))
        return definition.goal;
    if (id.rfind("wins_", 0) == 0 || id == "first_win")
        return std::min(wins, definition.goal);
    if (id == "battles_100")
        return std::min(battles, definition.goal);
    if (id == "online_10")
        return std::min(online_wins, definition.goal);
    return 0;
}

std::filesystem::path progress_path() { return default_cache_directory().parent_path() / "achievements.ini"; }

Progress load(const std::filesystem::path &file) {
    Progress progress;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string key;
        row >> key;
        if (key == "unlocked") {
            std::string id;
            int64_t stamp = 0;
            if (row >> id >> stamp && find(id))
                progress.unlocked[id] = stamp;
        } else if (key == "wins") {
            row >> progress.wins;
        } else if (key == "battles") {
            row >> progress.battles;
        } else if (key == "online_wins") {
            row >> progress.online_wins;
        }
    }
    return progress;
}

void save(const std::filesystem::path &file, const Progress &progress) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const auto temporary = file.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "version 1\nwins " << progress.wins << "\nbattles " << progress.battles << "\nonline_wins " << progress.online_wins << '\n';
        for (const auto &[id, stamp] : progress.unlocked)
            out << "unlocked " << id << ' ' << stamp << '\n';
        if (!out)
            return;
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(file, error);
        std::filesystem::rename(temporary, file, error);
    }
}

void Tracker::unlock(const char *id, int64_t stamp, std::vector<const Definition *> &out) {
    if (progress_.has(id))
        return;
    progress_.unlocked[id] = stamp;
    if (const auto *definition = find(id))
        out.push_back(definition);
}

std::vector<const Definition *> Tracker::update(const speedrun::GuestState &state, double now, bool online, int64_t stamp) {
    std::vector<const Definition *> out;
    if (state.dragon_world && !dragon_world_)
        unlock("dragon_world", stamp, out);
    dragon_world_ = state.dragon_world;
    if (state.credits && !credits_)
        unlock("credits", stamp, out);
    credits_ = state.credits;
    if (state.champion && !champion_)
        unlock("champion", stamp, out);
    champion_ = state.champion;

    if (state.battle && !state.battle_over && !live_ && state.health_known && state.player_max > 0) {
        live_ = true;
        started_ = now;
        player_max_ = state.player_max;
        opponent_max_ = state.opponent_max;
        lowest_ = state.player_health;
        battle_cpu_ = cpu_;
        cpu_known_ = cpu_ > 0;
        trailing_ = false;
    }
    if (live_ && state.battle && state.health_known) {
        if (cpu_known_)
            battle_cpu_ = std::min(battle_cpu_, cpu_);
        else if (cpu_ > 0 && now - started_ < 2.0) {
            battle_cpu_ = cpu_;
            cpu_known_ = true;
        }
    }
    if (live_ && state.battle && state.health_known) {
        player_ = state.player_health;
        opponent_ = state.opponent_health;
        reason_ = state.reason;
        lowest_ = std::min(lowest_, player_);
        if (player_max_ > 0 && opponent_max_ > 0 && player_ > 0 && player_ < player_max_ * 0.25f && opponent_ > opponent_max_ * 0.75f)
            trailing_ = true;
    }
    if (live_ && (state.battle_over || !state.battle)) {
        live_ = false;
        int result = 0;
        if (player_ <= 0)
            result = -1;
        else if (opponent_ <= 0)
            result = 1;
        else if (reason_ == speedrun::timeout_reason)
            result = player_ < opponent_ ? -1 : player_ > opponent_ ? 1 : 0;
        if (result != 0) {
            ++progress_.battles;
            ++session_battles_;
            if (session_battles_ >= 20)
                unlock("marathon", stamp, out);
            if (session_battles_ >= 50)
                unlock("marathon_50", stamp, out);
            if (progress_.battles >= 100)
                unlock("battles_100", stamp, out);
            streak_ = result > 0 ? streak_ + 1 : 0;
            if (streak_ >= 5)
                unlock("streak", stamp, out);
        }
        if (result > 0) {
            ++progress_.wins;
            unlock("first_win", stamp, out);
            if (progress_.wins >= 10)
                unlock("wins_10", stamp, out);
            if (progress_.wins >= 50)
                unlock("wins_50", stamp, out);
            if (progress_.wins >= 200)
                unlock("wins_200", stamp, out);
            if (progress_.wins >= 500)
                unlock("wins_500", stamp, out);
            if (lowest_ >= player_max_ && player_max_ > 0)
                unlock("flawless", stamp, out);
            if (player_max_ > 0 && player_ < player_max_ * 0.1f)
                unlock("comeback", stamp, out);
            if (reason_ == speedrun::timeout_reason && opponent_ > 0)
                unlock("time_up", stamp, out);
            if (player_max_ > 0 && player_ < player_max_ * 0.02f)
                unlock("by_a_thread", stamp, out);
            if (now - started_ < 20.0)
                unlock("quick_win", stamp, out);
            if (now - started_ < 10.0)
                unlock("lightning", stamp, out);
            if (lost_last_)
                unlock("rematch", stamp, out);
            const double taken = now - started_;
            const bool flawless = lowest_ >= player_max_ && player_max_ > 0;
            if (battle_cpu_ >= 4) {
                unlock("z4_win", stamp, out);
                if (taken < 20.0)
                    unlock("z4_quick", stamp, out);
            }
            if (battle_cpu_ >= 6) {
                unlock("z6_win", stamp, out);
                if (flawless)
                    unlock("z6_flawless", stamp, out);
                if (player_max_ > 0 && player_ < player_max_ * 0.1f)
                    unlock("z6_comeback", stamp, out);
            }
            flawless_streak_ = flawless ? flawless_streak_ + 1 : 0;
            if (flawless_streak_ >= 3)
                unlock("flawless_3", stamp, out);
            if (player_max_ > 0 && lowest_ >= player_max_ * 0.75f)
                unlock("iron_wall", stamp, out);
            if (trailing_)
                unlock("turnaround", stamp, out);
            if (taken < 6.0)
                unlock("blitz", stamp, out);
            if (reason_ == speedrun::timeout_reason && opponent_ > 0 && player_max_ > 0 && player_ - opponent_ < player_max_ * 0.05f)
                unlock("photo_finish", stamp, out);
            if (online) {
                if (flawless)
                    unlock("online_flawless", stamp, out);
                if (++online_streak_ >= 3)
                    unlock("online_streak", stamp, out);
                ++progress_.online_wins;
                unlock("online_win", stamp, out);
                if (progress_.online_wins >= 10)
                    unlock("online_10", stamp, out);
            }
        }
        if (result < 0) {
            flawless_streak_ = 0;
            if (online)
                online_streak_ = 0;
        }
        if (result != 0)
            lost_last_ = result < 0;
    }
    return out;
}

std::vector<const Definition *> Tracker::finished(const Run &run, int64_t stamp) {
    using speedrun::Category;
    std::vector<const Definition *> out;
    if (run.category == Category::off)
        return out;
    unlock("speed_finish", stamp, out);
    if (speedrun::tournament(run.category))
        unlock("speed_tournament", stamp, out);
    if (run.category == Category::any || run.category == Category::new_game_plus || run.category == Category::new_game_plus_very_hard ||
        run.category == Category::all_dragonballs)
        unlock("speed_story", stamp, out);
    if (run.category == Category::hercule_only)
        unlock("speed_hercule", stamp, out);
    if (run.permadeath)
        unlock("speed_permadeath", stamp, out);
    if (run.cpu >= 4)
        unlock("speed_z_level", stamp, out);
    if (run.verified)
        unlock("speed_clean", stamp, out);
    if (run.personal_best)
        unlock("speed_record", stamp, out);
    if (run.losses == 0)
        unlock("clean_sweep", stamp, out);
    return out;
}

}
