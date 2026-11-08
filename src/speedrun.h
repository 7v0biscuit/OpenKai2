// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace openkai2::speedrun {

// Persisted IDs: append categories without changing existing preferences or records.
enum class Category : unsigned {
    off = 0, any = 1, new_game_plus = 2,
    tournament_novice = 3, tournament_adept = 4, tournament_advanced = 5,
    all_dragonballs = 6, hercule_only = 7, new_game_plus_very_hard = 8
};
inline constexpr unsigned category_count = 9;
bool tournament(Category category);
const char *category_name(Category category);
const char *category_rules(Category category);
std::string category_label(Category category, unsigned cpu);
std::string splits_name(Category category, bool permadeath, unsigned cpu);

inline constexpr uint32_t task_first = 0x3B5358u;
inline constexpr uint32_t task_end = 0x3B53B0u;
inline constexpr uint32_t task_next = 40u;
inline constexpr uint32_t task_name_bytes = 20u;
inline constexpr uint32_t task_limit = 1024u;
inline constexpr uint32_t battle_result = 0x3A4D58u;
inline constexpr uint32_t battle_over = 0x3A4D60u;
inline constexpr uint32_t battle_reason = 0x3A4D64u;
inline constexpr uint32_t timeout_reason = 4u;
inline constexpr uint32_t reason_time_out = 4u;
inline constexpr uint32_t battle_pointer = 0x3A4CB4u;
inline constexpr uint32_t map_units = 0x69BFC0u;
inline constexpr uint32_t map_unit_bytes = 36u;
inline constexpr unsigned map_unit_limit = 48u;
inline constexpr double battle_memory = 120.0;
inline constexpr uint32_t fighter_health = 0x9EA4u;
inline constexpr uint32_t fighter_stride = 0x9F50u;
inline constexpr double field_rate = 60000.0 / 1001.0;

struct MapUnit {
    uint8_t character = 0, health = 0;
    bool player = false;
};

struct GuestState {
    bool intro = false;
    bool game_over = false;
    bool credits = false;
    bool dragon_world = false;
    bool battle = false;
    bool battle_over = false;
    bool main_menu = false;
    bool press_start = false;
    bool title_menu = false;
    bool tournament = false, tiers = false, entrants = false, bracket = false, champion = false;
    int32_t result = 0;
    uint32_t reason = 0;
    bool health_known = false;
    float player_health = 0, opponent_health = 0, player_max = 0, opponent_max = 0;
    std::vector<MapUnit> units;
    std::vector<std::string> tasks;
};
GuestState read_guest(const uint8_t *rdram, size_t bytes);
std::string loss_name(const GuestState &state);

struct Split {
    double real = 0, game = 0;
};

enum class Phase { waiting, running, finished };
enum class Event { none, started, restarted, battle_won, battle_lost, finished, died };

class Timer {
  public:
    explicit Timer(Category category = Category::any) : category_(category) {}
    Category category() const { return category_; }
    Event start(double real, uint64_t fields);
    Event split(double real, uint64_t fields);
    Event finish(double real, uint64_t fields);
    std::vector<Event> update(const GuestState &state, double real, uint64_t fields);
    Phase phase() const { return phase_; }
    double real(double now) const;
    double game(uint64_t fields) const;
    const std::vector<Split> &wins() const { return wins_; }
    unsigned losses() const { return losses_; }
    const std::string &last_loss() const { return last_loss_; }
    Split result() const { return final_; }
    void reset();

  private:
    Category category_;
    Phase phase_ = Phase::waiting;
    double start_real_ = 0;
    uint64_t start_fields_ = 0;
    std::vector<Split> wins_;
    Split final_;
    unsigned losses_ = 0;
    std::string last_loss_;
    bool intro_ = false, game_over_ = false, credits_ = false, battle_over_ = false;
    bool decided_ = true, dragon_world_ = false;
    std::vector<MapUnit> units_;
    double last_battle_ = -1e9;
    bool fight_live_ = false, fight_lost_ = false;
    float fight_player_ = 0, fight_opponent_ = 0;
    uint32_t fight_reason_ = 0;
    bool tiers_ = false, entrants_ = false, bracket_ = false, champion_ = false, pending_ = false;
    Split pending_split_;
    int track_fight(const GuestState &state);
    std::vector<Event> update_tournament(const GuestState &state, double real, uint64_t fields);
};

std::string format_time(double seconds, bool hundredths = true);
std::string format_time(double seconds, unsigned decimals);
bool parse_time(const std::string &text, double &seconds);

class SpeedCheck {
  public:
    bool sample(double real, uint64_t fields);
    double rate() const { return rate_; }

  private:
    bool started_ = false;
    double real_ = 0;
    uint64_t fields_ = 0;
    double rate_ = field_rate;
};

class ClockCheck {
  public:
    bool sample(double steady, double wall);
    double ratio() const { return ratio_; }

  private:
    bool started_ = false;
    double steady_ = 0, wall_ = 0, ratio_ = 1;
};

struct KnownFile {
    const char *path;
    const char *sha256;
};
inline constexpr KnownFile retail_files[] = {
    {"USR/ADX_US.AFS", "b656534dd5c90b133b7171fb3c63d91b74ca2a96dacf08703a1d973618426b83"},
    {"USR/DATA_CMN.AFS", "5bec61cc8630b268ca94c1de3bc92d32862e43edaf8e7b76ea356311be97b602"},
    {"USR/DATA_US.AFS", "0e5f54be211470e273e0bed3f7571e76c7cee23ff2c211504878336188deea33"},
    {"USR/MOVIE_US.SFD", "450fbb830abdf7923eec0b8a0cdbbdae09917cf5da0b209ee1d1f69f6981ac4a"},
};
struct DiscFileHash {
    std::string path, sha256;
};
std::vector<std::string> check_game_files(const std::vector<DiscFileHash> &files);

bool suspicious_process(std::string name);

struct Attempt {
    std::string date;
    bool finished = false;
    double real = 0;
    std::vector<double> splits;
};

class SplitBook {
  public:
    static SplitBook load(const std::filesystem::path &file);
    void save(const std::filesystem::path &file) const;
    bool record(const Attempt &attempt);
    bool has_best() const { return !best_.empty(); }
    double best_time() const { return best_.empty() ? 0 : best_.back(); }
    const std::vector<double> &best() const { return best_; }
    const std::vector<double> &golds() const { return golds_; }
    bool delta(size_t index, double time, double &difference) const;
    bool gold(size_t index, double segment) const;
    unsigned attempts() const { return attempts_; }
    unsigned completed() const { return completed_; }
    const std::vector<std::string> &history() const { return history_; }
    bool set_split(size_t index, double time);
    void add_split(double time);
    bool remove_split(size_t index);
    void clear_best();

  private:
    unsigned attempts_ = 0, completed_ = 0;
    std::vector<double> best_, golds_;
    std::vector<std::string> history_;
};

struct SplitRow {
    std::string name;
    double time = 0;
    size_t index = 0;
    bool done = false, current = false, has_time = false, has_delta = false, gold = false;
    double delta = 0;
};

struct RunView {
    double timer = 0, segment = 0;
    int pace = 0;
    std::vector<SplitRow> rows;
    bool has_previous = false, has_time_save = false, has_best_possible = false, has_sum_of_best = false;
    double previous = 0, time_save = 0, best_possible = 0, sum_of_best = 0;
    unsigned attempt = 0, finished = 0;
};
RunView make_view(const Timer &timer, const SplitBook &book, double now, size_t rows);

class RunLog {
  public:
    void open(const std::filesystem::path &file, const std::string &header);
    void write(const std::string &line);
    bool is_open() const { return out_.is_open(); }
    const std::filesystem::path &path() const { return path_; }
    static bool verify(const std::filesystem::path &file);

  private:
    std::ofstream out_;
    std::filesystem::path path_;
    std::string chain_;
};

}
