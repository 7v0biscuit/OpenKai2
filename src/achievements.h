// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "speedrun.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace openkai2::achievements {

struct Definition {
    const char *id;
    const char *title;
    const char *description;
    unsigned points;
    unsigned goal;
};

inline constexpr Definition definitions[] = {
    {"first_win", "Kamehameha!", "Win your first battle.", 5, 1},
    {"wins_10", "Saiyan Pride", "Win 10 battles.", 10, 10},
    {"wins_50", "Elite Warrior", "Win 50 battles.", 25, 50},
    {"wins_200", "Legendary Super Saiyan", "Win 200 battles.", 50, 200},
    {"flawless", "Not a Scratch", "Win a battle without losing any health.", 10, 1},
    {"comeback", "Never Give Up", "Win a battle with less than a tenth of your health left.", 10, 1},
    {"time_up", "Time's Up!", "Win a battle when the timer runs out.", 5, 1},
    {"quick_win", "Instant Transmission", "Win a battle in under 20 seconds.", 10, 1},
    {"rematch", "Rematch!", "Win a battle right after losing one.", 5, 1},
    {"dragon_world", "A New Adventure", "Start Dragon World.", 5, 1},
    {"credits", "The Saga Ends", "Reach the end credits of Dragon World.", 25, 1},
    {"champion", "World Champion", "Win the World Tournament.", 25, 1},
    {"online_win", "Online Debut", "Win an online battle.", 10, 1},
    {"marathon", "Hyperbolic Time Chamber", "Fight 20 battles in one session.", 10, 20},
    {"speed_finish", "Fast as Lightning", "Finish a speedrun.", 10, 1},
    {"speed_tournament", "Tournament Sprinter", "Finish a World Tournament speedrun.", 10, 1},
    {"speed_story", "Saga Speedster", "Finish an Any%, New Game+ or All Dragon Balls speedrun.", 25, 1},
    {"speed_permadeath", "No Second Chances", "Finish a speedrun with permadeath on.", 25, 1},
    {"speed_z_level", "Beyond the Limit", "Finish a speedrun with the CPU on Z4 or higher.", 25, 1},
    {"speed_record", "New Record", "Set a personal best in a speedrun.", 5, 1},
    {"speed_clean", "Clean Run", "Finish a verified speedrun with nothing flagged.", 10, 1},
    {"speed_hercule", "The Champ's Run", "Finish the Hercule Only speedrun.", 25, 1},
    {"wins_500", "Unstoppable Force", "Win 500 battles.", 100, 500},
    {"battles_100", "Battle Hardened", "Fight 100 battles.", 10, 100},
    {"streak", "On a Roll", "Win 5 battles in a row.", 15, 5},
    {"lightning", "Blink of an Eye", "Win a battle in under 10 seconds.", 15, 1},
    {"by_a_thread", "By a Thread", "Win a battle with less than 2% of your health left.", 15, 1},
    {"online_10", "Online Contender", "Win 10 online battles.", 20, 10},
    {"marathon_50", "Training Arc", "Fight 50 battles in one session.", 20, 50},
    {"z4_win", "Beyond Human", "Beat a CPU on Z4 or higher.", 15, 1},
    {"z6_win", "Limit Breaker", "Beat a CPU on Z6.", 25, 1},
    {"z6_flawless", "Ultra Instinct", "Beat a Z6 CPU without losing any health.", 50, 1},
    {"z6_comeback", "Against All Odds", "Beat a Z6 CPU with less than a tenth of your health left.", 30, 1},
    {"z4_quick", "Swift Justice", "Beat a CPU on Z4 or higher in under 20 seconds.", 25, 1},
    {"flawless_3", "Perfect Form", "Win 3 battles in a row without losing any health.", 30, 3},
    {"iron_wall", "Iron Wall", "Win a battle without dropping below three quarters of your health.", 10, 1},
    {"turnaround", "Turn the Tables", "Win after dropping under a quarter of your health while your opponent had over three quarters.", 20, 1},
    {"blitz", "Faster Than Sight", "Win a battle in under 6 seconds.", 25, 1},
    {"photo_finish", "Photo Finish", "Win a time-out by less than 5% of your health.", 15, 1},
    {"online_flawless", "Online Untouchable", "Win an online battle without losing any health.", 30, 1},
    {"online_streak", "Online Rampage", "Win 3 online battles in a row.", 25, 3},
    {"clean_sweep", "Clean Sweep", "Finish a speedrun without losing a battle.", 25, 1},
};
inline constexpr size_t definition_count = sizeof(definitions) / sizeof(definitions[0]);
const Definition *find(const std::string &id);
unsigned total_points();

struct Progress {
    std::map<std::string, int64_t> unlocked;
    unsigned wins = 0, battles = 0, online_wins = 0;
    bool has(const std::string &id) const { return unlocked.count(id) != 0; }
    unsigned points() const;
    unsigned count(const Definition &definition) const;
};

std::filesystem::path progress_path();
Progress load(const std::filesystem::path &file);
void save(const std::filesystem::path &file, const Progress &progress);

struct Run {
    speedrun::Category category = speedrun::Category::off;
    bool permadeath = false, verified = false, personal_best = false;
    unsigned cpu = 0, losses = 0;
};

class Tracker {
  public:
    std::vector<const Definition *> finished(const Run &run, int64_t stamp);
    void set_cpu(unsigned level) { cpu_ = level; }
    explicit Tracker(Progress progress = {}) : progress_(std::move(progress)) {}
    std::vector<const Definition *> update(const speedrun::GuestState &state, double now, bool online, int64_t stamp);
    const Progress &progress() const { return progress_; }

  private:
    Progress progress_;
    bool live_ = false, lost_last_ = false, dragon_world_ = false, credits_ = false, champion_ = false;
    float player_ = 0, opponent_ = 0, player_max_ = 0, lowest_ = 0;
    uint32_t reason_ = 0;
    double started_ = 0;
    unsigned session_battles_ = 0, streak_ = 0, cpu_ = 0, battle_cpu_ = 0, flawless_streak_ = 0, online_streak_ = 0;
    float opponent_max_ = 0;
    bool trailing_ = false, cpu_known_ = false;
    void unlock(const char *id, int64_t stamp, std::vector<const Definition *> &out);
};

}
