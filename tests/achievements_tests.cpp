// SPDX-License-Identifier: GPL-3.0-only
#include "achievements.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using namespace openkai2;
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}
speedrun::GuestState fighting(float player, float opponent, float max = 100.0f) {
    speedrun::GuestState state;
    state.battle = true;
    state.health_known = true;
    state.player_health = player;
    state.opponent_health = opponent;
    state.player_max = max;
    state.opponent_max = max;
    return state;
}
speedrun::GuestState over(float player, float opponent, uint32_t reason = 0) {
    auto state = fighting(player, opponent);
    state.battle_over = true;
    state.reason = reason;
    return state;
}
std::set<std::string> ids(const std::vector<const achievements::Definition *> &list) {
    std::set<std::string> out;
    for (const auto *definition : list)
        out.insert(definition->id);
    return out;
}
std::set<std::string> fight(achievements::Tracker &tracker, float start, float end, float opponent, double seconds,
                            bool online = false, uint32_t reason = 0, double at = 0) {
    tracker.update(fighting(start, 100.0f), at, online, 1);
    tracker.update(fighting(end, opponent), at + seconds * 0.5, online, 1);
    return ids(tracker.update(over(end, opponent, reason), at + seconds, online, 1));
}
}

int main() {
    try {
        std::set<std::string> unique;
        for (const auto &definition : achievements::definitions) {
            require(unique.insert(definition.id).second, "Two achievements share an id");
            require(definition.points > 0 && definition.goal > 0 && *definition.title && *definition.description,
                    "An achievement is missing its title, description, points or goal");
        }
        require(achievements::total_points() > 100, "The built-in set is too small");

        achievements::Tracker tracker;
        auto first = fight(tracker, 100.0f, 100.0f, 0.0f, 12.0);
        require(first.count("first_win") && first.count("flawless") && first.count("quick_win"),
                "A quick flawless first win did not unlock its achievements");
        require(tracker.progress().wins == 1 && tracker.progress().battles == 1, "The win was not counted");
        auto again = fight(tracker, 100.0f, 100.0f, 0.0f, 12.0);
        require(again.empty(), "An achievement unlocked twice");

        auto loss = fight(tracker, 100.0f, 0.0f, 50.0f, 40.0);
        require(loss.empty() && tracker.progress().wins == 2 && tracker.progress().battles == 3, "A loss was counted as a win");
        auto comeback = fight(tracker, 100.0f, 5.0f, 0.0f, 60.0);
        require(comeback.count("rematch") && comeback.count("comeback") && !comeback.count("flawless") && !comeback.count("quick_win"),
                "A close win after a loss did not unlock Rematch and Never Give Up");

        auto timeout = fight(tracker, 100.0f, 60.0f, 30.0f, 99.0, false, speedrun::timeout_reason);
        require(timeout.count("time_up"), "A time-out win did not unlock Time's Up");
        auto draw = fight(tracker, 100.0f, 50.0f, 50.0f, 99.0, false, speedrun::timeout_reason);
        require(draw.empty(), "A drawn time-out counted as a win");

        auto online = fight(tracker, 100.0f, 80.0f, 0.0f, 30.0, true);
        require(online.count("online_win"), "An online win did not unlock Online Debut");

        for (int i = 0; i < 6; ++i)
            fight(tracker, 100.0f, 80.0f, 0.0f, 30.0);
        require(tracker.progress().has("wins_10"), "Ten wins did not unlock Saiyan Pride");

        speedrun::GuestState world;
        world.dragon_world = true;
        require(ids(tracker.update(world, 0, false, 1)).count("dragon_world"), "Starting Dragon World did not unlock");
        world.credits = true;
        require(ids(tracker.update(world, 0, false, 1)).count("credits"), "Reaching the credits did not unlock");
        speedrun::GuestState tournament;
        tournament.champion = true;
        require(ids(tracker.update(tournament, 0, false, 1)).count("champion"), "Winning the World Tournament did not unlock");

        speedrun::GuestState idle;
        require(tracker.update(idle, 0, false, 1).empty(), "An idle frame unlocked something");
        speedrun::GuestState unknown = fighting(0.0f, 0.0f, 0.0f);
        unknown.health_known = false;
        tracker.update(unknown, 0, false, 1);
        unknown.battle_over = true;
        require(tracker.update(unknown, 0, false, 1).empty(), "A battle without health data unlocked something");

        const auto root = std::filesystem::temp_directory_path() / "openkai2-achievement-tests";
        std::filesystem::remove_all(root);
        const auto file = root / "achievements.ini";
        achievements::save(file, tracker.progress());
        const auto read = achievements::load(file);
        require(read.unlocked == tracker.progress().unlocked && read.wins == tracker.progress().wins &&
                    read.battles == tracker.progress().battles && read.points() == tracker.progress().points(),
                "Achievement progress did not round-trip");
        require(read.count(*achievements::find("wins_50")) == read.wins, "Win progress toward Elite Warrior is wrong");
        std::filesystem::remove_all(root);
        std::cout << "Achievement tests passed." << std::endl;
        return 0;
    } catch (const std::exception &problem) {
        std::cerr << "::error title=Achievements::" << problem.what() << std::endl;
        return 1;
    }
}
