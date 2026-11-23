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

        require(tracker.progress().online_wins == 1, "Online wins were not counted");
        require(tracker.progress().has("streak"), "Five wins in a row did not unlock On a Roll");
        auto blink = fight(tracker, 100.0f, 1.0f, 0.0f, 8.0);
        require(blink.count("lightning") && blink.count("by_a_thread"), "An eight second win on 1% health did not unlock its achievements");

        achievements::Run run;
        require(tracker.finished(run, 1).empty(), "A run with speedrun mode off unlocked something");
        run.category = speedrun::Category::tournament_novice;
        auto sprint = ids(tracker.finished(run, 1));
        require(sprint.count("speed_finish") && sprint.count("speed_tournament") && !sprint.count("speed_story") && !sprint.count("speed_record"),
                "A tournament speedrun unlocked the wrong achievements");
        run.category = speedrun::Category::hercule_only;
        run.permadeath = true;
        run.cpu = 4;
        run.verified = true;
        run.personal_best = true;
        auto champ = ids(tracker.finished(run, 1));
        require(champ.count("speed_hercule") && champ.count("speed_permadeath") && champ.count("speed_z_level") && champ.count("speed_clean") &&
                    champ.count("speed_record") && !champ.count("speed_finish"),
                "A Hercule Only permadeath Z4 record did not unlock its achievements");
        run.category = speedrun::Category::new_game_plus;
        require(ids(tracker.finished(run, 1)).count("speed_story"), "A New Game+ speedrun did not unlock Saga Speedster");

        achievements::Tracker skill;
        skill.set_cpu(6);
        auto z6 = fight(skill, 100.0f, 100.0f, 0.0f, 5.0);
        require(z6.count("z4_win") && z6.count("z6_win") && z6.count("z6_flawless") && z6.count("z4_quick") && z6.count("blitz") &&
                    z6.count("iron_wall") && !z6.count("z6_comeback"),
                "A five second flawless Z6 win did not unlock its skill achievements");
        fight(skill, 100.0f, 100.0f, 0.0f, 30.0);
        require(fight(skill, 100.0f, 100.0f, 0.0f, 30.0).count("flawless_3"), "Three flawless wins in a row did not unlock Perfect Form");
        skill.set_cpu(0);
        skill.update(fighting(100.0f, 100.0f), 0, false, 1);
        skill.set_cpu(6);
        skill.update(fighting(100.0f, 60.0f), 3, false, 1);
        auto late = ids(skill.update(over(100.0f, 0.0f), 4, false, 1));
        require(!late.count("z4_win") && !late.count("z6_win"), "Raising the Z level partway through a fight counted as a Z6 win");
        skill.set_cpu(6);
        skill.update(fighting(100.0f, 100.0f), 10, false, 1);
        skill.update(fighting(20.0f, 90.0f), 20, false, 1);
        auto turned = ids(skill.update(over(8.0f, 0.0f), 40, false, 1));
        require(turned.count("turnaround") && turned.count("z6_comeback") && !turned.count("iron_wall"),
                "Winning from a quarter of health against a healthy opponent did not unlock Turn the Tables");
        auto photo = fight(skill, 100.0f, 52.0f, 50.0f, 99.0, false, speedrun::timeout_reason);
        require(photo.count("photo_finish"), "A two point time-out win did not unlock Photo Finish");
        for (int i = 0; i < 2; ++i)
            fight(skill, 100.0f, 100.0f, 0.0f, 30.0, true);
        auto rampage = fight(skill, 100.0f, 90.0f, 0.0f, 30.0, true);
        require(skill.progress().has("online_flawless") && rampage.count("online_streak"),
                "Online skill achievements did not unlock");
        achievements::Run clean;
        clean.category = speedrun::Category::any;
        require(ids(skill.finished(clean, 1)).count("clean_sweep"), "A speedrun without a loss did not unlock Clean Sweep");

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
                    read.battles == tracker.progress().battles && read.online_wins == tracker.progress().online_wins && read.points() == tracker.progress().points(),
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
