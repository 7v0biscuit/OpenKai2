// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun.h"
#include "disc_image.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace openkai2::speedrun {
namespace {
uint32_t word(const uint8_t *rdram, size_t bytes, uint32_t address) {
    address &= 0x1FFFFFFu;
    if (size_t(address) + 4 > bytes)
        return 0;
    uint32_t value;
    std::memcpy(&value, rdram + address, 4);
    return value;
}
bool starts(const std::string &text, const char *prefix) { return text.rfind(prefix, 0) == 0; }
}

const char *category_name(Category category) {
    switch (category) {
    case Category::any: return "Any%";
    case Category::new_game_plus: return "NG+";
    case Category::tournament_novice: return "Tournament (Novice)";
    case Category::tournament_adept: return "Tournament (Adept)";
    case Category::tournament_advanced: return "Tournament (Advanced)";
    case Category::all_dragonballs: return "All Dragonballs";
    case Category::hercule_only: return "Hercule Only";
    case Category::new_game_plus_very_hard: return "NG+ (Very Hard)";
    default: return "Off";
    }
}

bool tournament(Category category) {
    return category >= Category::tournament_novice && category <= Category::tournament_advanced;
}

const char *category_rules(Category category) {
    switch (category) {
    case Category::tournament_novice:
    case Category::tournament_adept:
    case Category::tournament_advanced:
        return "The matching World Tournament tier is selected automatically and must already be unlocked. Start when confirming Normal or Custom for your character. "
               "Finish on the first frame of the final opponent's ring-out animation, Time-Out! or K.O. Milliseconds are required for top-three times.";
    case Category::all_dragonballs:
        return "Start on Dragon World or Restart Dragon World. Collect all seven Dragon Balls before the final hit defeats Kid Buu in Stage 9. Pre-equipped capsules are allowed.";
    case Category::hercule_only:
        return "Start on Dragon World or Restart Dragon World. Finish on the final hit against Kid Buu in Stage 9. Only Hercule may win battles; all other characters must have zero wins. Show the results screen in your recording.";
    case Category::new_game_plus_very_hard:
        return "Very Hard is applied automatically and locked. Start a fresh Dragon World attempt using an existing save's unlocks. Finish on the final hit against Kid Buu in Stage 9. Pre-equipped capsules are allowed. Z difficulty modifiers are separate variants.";
    case Category::new_game_plus:
        return "An existing save's unlocks are copied into a fresh Dragon World attempt. Finish on the final hit against Kid Buu in Stage 9. Pre-equipped capsules are allowed. Quitting ends the attempt.";
    case Category::any:
        return "An empty memory card is prepared automatically. Start on Dragon World and finish on the final hit against Kid Buu in Stage 9. Quitting ends the attempt; the next launch starts fresh.";
    default: return "Choose a category to view its timing rules.";
    }
}

std::string category_label(Category category, unsigned cpu) {
    static const char *const levels[] = {"", " Z1", " Z2", " Z3"};
    return std::string(category_name(category)) + levels[cpu < 4u ? cpu : 0u];
}

std::string splits_name(Category category, bool permadeath, unsigned cpu) {
    static constexpr const char *names[] = {"off", "any", "ngplus", "tournament-novice", "tournament-adept",
        "tournament-advanced", "all-dragonballs", "hercule-only", "ngplus-very-hard"};
    const auto index = unsigned(category);
    std::string name = names[index < category_count ? index : 0];
    if (permadeath)
        name += "-permadeath";
    if (cpu >= 1u && cpu <= 3u)
        name += "-z" + std::to_string(cpu);
    return name + ".txt";
}

GuestState read_guest(const uint8_t *rdram, size_t bytes) {
    GuestState state;
    uint32_t task = word(rdram, bytes, task_first);
    for (uint32_t count = 0; task && (task & 0x1FFFFFFu) != task_end && count < task_limit; ++count) {
        const uint32_t at = task & 0x1FFFFFFu;
        if (size_t(at) + task_next + 4 > bytes)
            break;
        std::string name(reinterpret_cast<const char *>(rdram + at),
                         strnlen(reinterpret_cast<const char *>(rdram + at), task_name_bytes));
        if (name == "DW INTRO MAIN")
            state.intro = true;
        else if (starts(name, "#[DW GOVER]"))
            state.game_over = true;
        else if (starts(name, "#[DW STAFF]"))
            state.credits = true;
        else if (name == "TENK_SYS")
            state.tournament = true;
        else if (name == "FG_NOVICE")
            state.tiers = true;
        else if (name == "FG_ICON")
            state.entrants = true;
        else if (name == "TMAP_SYS")
            state.bracket = true;
        else if (name == "MTM_CLEAR")
            state.champion = true;
        else if (starts(name, "[BTL]"))
            state.battle = true;
        else if (name == "MOKUJI")
            state.main_menu = true;
        else if (name == "PRESSSTART")
            state.press_start = true;
        else if (name == "TITLEMENU")
            state.title_menu = true;
        if (starts(name, "DW") || starts(name, "#[DW"))
            state.dragon_world = true;
        state.tasks.push_back(std::move(name));
        task = word(rdram, bytes, at + task_next);
    }
    state.result = int32_t(word(rdram, bytes, battle_result));
    state.battle_over = word(rdram, bytes, battle_over) == 1u;
    state.reason = word(rdram, bytes, battle_reason);
    for (unsigned index = 0; index < map_unit_limit; ++index) {
        const size_t at = size_t(map_units) + size_t(index) * map_unit_bytes;
        if (at + map_unit_bytes > bytes || rdram[at + 24] != 0xB8u || rdram[at + 25] != 0x0Bu)
            break;
        MapUnit unit;
        unit.character = rdram[at + 8];
        unit.health = rdram[at + 5];
        unit.player = rdram[at + 10] != 0 && (rdram[at + 10] & 0x20u) == 0;
        state.units.push_back(unit);
    }
    const uint32_t battle = word(rdram, bytes, battle_pointer) & 0x1FFFFFFu;
    if (battle >= 0x100000u && size_t(battle) + fighter_health + fighter_stride + 4 <= bytes) {
        std::memcpy(&state.player_health, rdram + battle + fighter_health, 4);
        std::memcpy(&state.player_max, rdram + battle + fighter_health + 4, 4);
        std::memcpy(&state.opponent_health, rdram + battle + fighter_health + fighter_stride, 4);
        std::memcpy(&state.opponent_max, rdram + battle + fighter_health + fighter_stride + 4, 4);
        const auto valid = [](float health, float max) {
            return std::isfinite(health) && std::isfinite(max) && max > 0 && max <= 100000 && health <= max;
        };
        state.health_known = valid(state.player_health, state.player_max) && valid(state.opponent_health, state.opponent_max);
    }
    return state;
}

std::string loss_name(const GuestState &) { return "knocked out in Dragon World"; }

void Timer::reset() { *this = Timer{category_}; }

Event Timer::start(double real, uint64_t fields) {
    const auto event = phase_ == Phase::waiting ? Event::started : Event::restarted;
    reset();
    phase_ = Phase::running;
    start_real_ = real;
    start_fields_ = fields;
    return event;
}

Event Timer::finish(double real, uint64_t fields) {
    if (phase_ != Phase::running || this->real(real) <= 0)
        return Event::none;
    final_ = {this->real(real), game(fields)};
    if (wins_.empty() || wins_.back().real < final_.real)
        wins_.push_back(final_);
    phase_ = Phase::finished;
    return Event::finished;
}

Event Timer::split(double real, uint64_t fields) {
    if (phase_ != Phase::running)
        return Event::none;
    const Split value{this->real(real), game(fields)};
    if (value.real <= 0 || (!wins_.empty() && wins_.back().real >= value.real))
        return Event::none;
    wins_.push_back(value);
    return Event::battle_won;
}

int Timer::track_fight(const GuestState &state) {
    if (state.battle && !state.battle_over && !fight_live_) {
        fight_live_ = state.health_known;
        fight_lost_ = false;
    }
    if (fight_live_ && state.battle && state.health_known) {
        fight_player_ = state.player_health;
        fight_opponent_ = state.opponent_health;
        fight_reason_ = state.reason;
    }
    int fight = 0;
    if (fight_live_ && (state.battle_over || !state.battle)) {
        fight_live_ = false;
        if (fight_player_ <= 0)
            fight = -1;
        else if (fight_opponent_ <= 0)
            fight = 1;
        else if (fight_reason_ == timeout_reason)
            fight = fight_player_ < fight_opponent_ ? -1 : fight_player_ > fight_opponent_ ? 1 : 0;
    }
    return fight;
}

std::vector<Event> Timer::update_tournament(const GuestState &state, double real, uint64_t fields) {
    std::vector<Event> events;
    const bool tiers = state.tiers && !tiers_, entrants = state.entrants && !entrants_;
    const bool bracket = state.bracket && !bracket_, champion = state.champion && !champion_;
    const bool decided = state.battle && state.battle_over && !battle_over_;
    bool fresh = false;
    if (entrants && phase_ != Phase::running) {
        events.push_back(start(real, fields));
        pending_ = false;
        fresh = true;
    }
    if (phase_ == Phase::running && !fresh) {
        if (decided) {
            pending_ = true;
            pending_split_ = {this->real(real), game(fields)};
        }
        if (pending_ && (bracket || state.champion)) {
            wins_.push_back(pending_split_);
            pending_ = false;
            events.push_back(Event::battle_won);
        }
        if (champion && !wins_.empty()) {
            final_ = wins_.back();
            phase_ = Phase::finished;
            events.push_back(Event::finished);
        } else if (tiers || state.main_menu || state.title_menu || state.press_start) {
            ++losses_;
            last_loss_ = pending_ ? "lost a tournament match" : "left the tournament";
            events.push_back(Event::died);
            phase_ = Phase::waiting;
            pending_ = false;
        }
    }
    tiers_ = state.tiers;
    entrants_ = state.entrants;
    bracket_ = state.bracket;
    champion_ = state.champion;
    battle_over_ = state.battle_over;
    return events;
}

std::vector<Event> Timer::update(const GuestState &state, double real, uint64_t fields) {
    if (tournament(category_))
        return update_tournament(state, real, fields);
    std::vector<Event> events;
    const bool fresh = !tournament(category_) && state.intro && !intro_;
    if (fresh) {
        events.push_back(phase_ == Phase::waiting ? Event::started : Event::restarted);
        phase_ = Phase::running;
        start_real_ = real;
        start_fields_ = fields;
        wins_.clear();
        final_ = {};
        losses_ = 0;
        last_loss_.clear();
    }
    if (state.dragon_world || state.intro)
        dragon_world_ = true;
    else if (state.main_menu || state.title_menu || state.press_start)
        dragon_world_ = false;
    if (state.battle)
        last_battle_ = real;
    const int fight = track_fight(state);
    const int outcome = state.result == 1 ? 1 : state.result == -1 ? -1 : 0;
    bool won = false, lost = false;
    if (fresh) {
        decided_ = outcome != 0;
    } else if (outcome == 0) {
        decided_ = false;
    } else if (!decided_) {
        decided_ = true;
        won = outcome == 1 && !dragon_world_;
    }
    if (dragon_world_ && !fresh) {
        if (fight == 1)
            won = true;
        if (fight == -1) {
            lost = true;
            fight_lost_ = true;
        }
    }
    const bool after_battle = real - last_battle_ <= battle_memory && state.result == -1;
    for (size_t i = 0; i < state.units.size() && i < units_.size(); ++i) {
        const auto &now = state.units[i], &before = units_[i];
        if (now.character == before.character && now.player && before.player && now.health < before.health &&
            after_battle && !fresh && !fight_lost_)
            lost = true;
    }
    if (lost)
        fight_lost_ = true;
    if (!state.units.empty() || !dragon_world_)
        units_ = state.units;
    if (phase_ == Phase::running && won) {
        wins_.push_back({this->real(real), game(fields)});
        events.push_back(Event::battle_won);
    }
    if (lost && dragon_world_) {
        ++losses_;
        last_loss_ = loss_name(state);
        events.push_back(Event::battle_lost);
    }
    if (phase_ == Phase::running) {
        if (state.game_over && !game_over_)
            events.push_back(Event::died);
        if (!tournament(category_) && state.credits && !credits_) {
            final_ = wins_.empty() ? Split{this->real(real), game(fields)} : wins_.back();
            phase_ = Phase::finished;
            events.push_back(Event::finished);
        }
    }
    intro_ = state.intro;
    game_over_ = state.game_over;
    credits_ = state.credits;
    battle_over_ = state.battle_over;
    return events;
}

double Timer::real(double now) const {
    if (phase_ == Phase::waiting)
        return 0;
    if (phase_ == Phase::finished)
        return final_.real;
    return std::max(0.0, now - start_real_);
}

double Timer::game(uint64_t fields) const {
    if (phase_ == Phase::waiting)
        return 0;
    if (phase_ == Phase::finished)
        return final_.game;
    return fields > start_fields_ ? double(fields - start_fields_) / field_rate : 0.0;
}

bool parse_time(const std::string &text, double &seconds) {
    std::vector<double> parts;
    std::string current;
    for (char c : text + ':') {
        if (c == ' ')
            continue;
        if (c == ':') {
            if (current.empty())
                return false;
            char *end = nullptr;
            const double value = std::strtod(current.c_str(), &end);
            if (*end || value < 0)
                return false;
            parts.push_back(value);
            current.clear();
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            current += c;
        } else {
            return false;
        }
    }
    if (parts.empty() || parts.size() > 3)
        return false;
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        if (parts[i] != std::floor(parts[i]) || (i > 0 && parts[i] >= 60))
            return false;
    if (parts.size() > 1 && parts.back() >= 60)
        return false;
    seconds = 0;
    for (double part : parts)
        seconds = seconds * 60 + part;
    return seconds > 0;
}

std::string format_time(double seconds, unsigned decimals) {
    decimals = std::min(decimals, 3u);
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        seconds = 0;
    static constexpr uint64_t scales[] = {1, 10, 100, 1000};
    const auto scale = scales[decimals];
    const auto ticks = uint64_t(std::floor(seconds * double(scale)));
    const auto whole = ticks / scale, hours = whole / 3600, minutes = whole / 60 % 60, secs = whole % 60;
    char text[64];
    if (hours)
        std::snprintf(text, sizeof text, "%llu:%02llu:%02llu", static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes), static_cast<unsigned long long>(secs));
    else
        std::snprintf(text, sizeof text, "%llu:%02llu", static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(secs));
    std::string result = text;
    if (decimals) {
        std::snprintf(text, sizeof text, ".%0*llu", int(decimals), static_cast<unsigned long long>(ticks % scale));
        result += text;
    }
    return result;
}

std::string format_time(double seconds, bool hundredths) {
    return format_time(seconds, hundredths ? 2u : 0u);
}

bool SpeedCheck::sample(double real, uint64_t fields) {
    if (!started_ || fields < fields_ || real < real_) {
        started_ = true;
        real_ = real;
        fields_ = fields;
        return false;
    }
    const double span = real - real_;
    if (span < 5.0)
        return false;
    rate_ = double(fields - fields_) / span;
    real_ = real;
    fields_ = fields;
    return rate_ > field_rate * 1.02;
}

bool ClockCheck::sample(double steady, double wall) {
    if (!started_ || wall < wall_ || steady < steady_) {
        started_ = true;
        steady_ = steady;
        wall_ = wall;
        return false;
    }
    const double span = wall - wall_;
    if (span < 10.0)
        return false;
    ratio_ = (steady - steady_) / span;
    steady_ = steady;
    wall_ = wall;
    if (span > 60.0)
        return false;
    return std::fabs(ratio_ - 1.0) > 0.03;
}

std::vector<std::string> check_game_files(const std::vector<DiscFileHash> &files) {
    std::vector<std::string> problems;
    for (const auto &known : retail_files) {
        const auto found = std::find_if(files.begin(), files.end(), [&](const DiscFileHash &file) {
            std::string path = file.path;
            std::replace(path.begin(), path.end(), '\\', '/');
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char c) { return char(std::toupper(c)); });
            return path == known.path;
        });
        if (found == files.end())
            problems.push_back(std::string(known.path) + " is missing");
        else if (found->sha256 != known.sha256)
            problems.push_back(std::string(known.path) + " does not match the US retail disc");
    }
    return problems;
}

bool suspicious_process(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const char *tool : {"cheatengine", "cheat engine", "artmoney", "speedhack", "speedgear", "x64dbg",
                             "x32dbg", "ollydbg", "scanmem", "gameconqueror", "wemod", "squalr"})
        if (name.find(tool) != std::string::npos)
            return true;
    return false;
}

SplitBook SplitBook::load(const std::filesystem::path &file) {
    SplitBook book;
    std::ifstream in(file);
    std::string line;
    auto numbers = [](std::istringstream &row) {
        std::vector<double> values;
        double value;
        while (row >> value)
            values.push_back(value);
        return values;
    };
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string key;
        row >> key;
        if (key == "attempts")
            row >> book.attempts_;
        else if (key == "completed")
            row >> book.completed_;
        else if (key == "best")
            book.best_ = numbers(row);
        else if (key == "golds")
            book.golds_ = numbers(row);
        else if (key == "run") {
            std::string rest;
            std::getline(row >> std::ws, rest);
            book.history_.push_back(rest);
        }
    }
    return book;
}

void SplitBook::save(const std::filesystem::path &file) const {
    std::filesystem::create_directories(file.parent_path());
    auto temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out)
            throw std::runtime_error("Cannot save splits: " + file.string());
        out.precision(3);
        out << std::fixed << "# OpenKai2 splits, real time in seconds\nattempts " << attempts_ << "\ncompleted "
            << completed_ << "\nbest";
        for (double value : best_)
            out << ' ' << value;
        out << "\ngolds";
        for (double value : golds_)
            out << ' ' << value;
        out << '\n';
        for (const auto &line : history_)
            out << "run " << line << '\n';
        if (!out)
            throw std::runtime_error("Cannot save splits: " + file.string());
    }
    std::filesystem::rename(temporary, file);
}

bool SplitBook::record(const Attempt &attempt) {
    ++attempts_;
    double previous = 0;
    for (size_t i = 0; i < attempt.splits.size(); ++i) {
        const double segment = attempt.splits[i] - previous;
        previous = attempt.splits[i];
        if (i >= golds_.size())
            golds_.push_back(segment);
        else if (segment < golds_[i])
            golds_[i] = segment;
    }
    std::string line = attempt.date + (attempt.finished ? "  finished " : "  reset ") + format_time(attempt.real) +
                       "  battles " + std::to_string(attempt.splits.size());
    bool personal_best = false;
    if (attempt.finished) {
        ++completed_;
        if (best_.empty() || attempt.real < best_time()) {
            best_ = attempt.splits;
            if (best_.empty() || best_.back() != attempt.real)
                best_.push_back(attempt.real);
            personal_best = true;
            line += "  personal best";
        }
    }
    history_.push_back(line);
    return personal_best;
}

bool SplitBook::set_split(size_t index, double time) {
    if (index >= best_.size() || !(time > 0))
        return false;
    best_[index] = time;
    std::sort(best_.begin(), best_.end());
    return true;
}

void SplitBook::add_split(double time) {
    if (!(time > 0))
        return;
    best_.push_back(time);
    std::sort(best_.begin(), best_.end());
    if (golds_.size() < best_.size())
        golds_.resize(best_.size(), 0.0);
    double previous = 0;
    for (size_t i = 0; i < best_.size(); ++i) {
        const double segment = best_[i] - previous;
        previous = best_[i];
        if (golds_[i] <= 0 || segment < golds_[i])
            golds_[i] = segment;
    }
}

bool SplitBook::remove_split(size_t index) {
    if (index >= best_.size())
        return false;
    best_.erase(best_.begin() + std::ptrdiff_t(index));
    if (index < golds_.size())
        golds_.erase(golds_.begin() + std::ptrdiff_t(index));
    return true;
}

void SplitBook::clear_best() {
    best_.clear();
    golds_.clear();
}

bool SplitBook::delta(size_t index, double time, double &difference) const {
    if (index >= best_.size())
        return false;
    difference = time - best_[index];
    return true;
}

bool SplitBook::gold(size_t index, double segment) const { return index >= golds_.size() || segment < golds_[index]; }

RunView make_view(const Timer &timer, const SplitBook &book, double now, size_t rows) {
    RunView view;
    const auto &wins = timer.wins();
    const auto &best = book.best();
    const auto &golds = book.golds();
    const Phase phase = timer.phase();
    const size_t done = wins.size();
    const bool running = phase == Phase::running;
    view.timer = timer.real(now);
    const double last = done ? wins.back().real : 0.0;
    view.segment = running ? std::max(0.0, view.timer - last) : 0.0;
    view.attempt = book.attempts() + (running ? 1u : 0u);
    view.finished = book.completed();

    auto pb_segment = [&](size_t i) { return best[i] - (i ? best[i - 1] : 0.0); };
    if (done && done <= best.size()) {
        const double segment = last - (done > 1 ? wins[done - 2].real : 0.0);
        view.has_previous = true;
        view.previous = segment - pb_segment(done - 1);
    }
    if (running && done < best.size() && done < golds.size()) {
        view.has_time_save = true;
        view.time_save = std::max(0.0, pb_segment(done) - golds[done]);
    }
    const size_t total = best.empty() ? golds.size() : best.size();
    if (total && total <= golds.size()) {
        view.has_sum_of_best = true;
        for (size_t i = 0; i < total; ++i)
            view.sum_of_best += golds[i];
        if (phase == Phase::finished) {
            view.has_best_possible = true;
            view.best_possible = timer.result().real;
        } else if (done <= total) {
            view.has_best_possible = true;
            view.best_possible = last;
            for (size_t i = done; i < total; ++i)
                view.best_possible += golds[i];
            if (running && done < total)
                view.best_possible += std::max(0.0, view.segment - golds[done]);
        }
    }

    if (phase == Phase::finished && !best.empty())
        view.pace = timer.result().real <= best.back() ? -1 : 1;
    else if (running && !best.empty()) {
        if (done < best.size() && view.timer > best[done])
            view.pace = 1;
        else if (done && done <= best.size())
            view.pace = wins.back().real < best[done - 1] ? -1 : 1;
        else
            view.pace = done < best.size() ? -1 : 1;
    }

    const size_t count = std::max(best.size(), done + (running ? 1u : 0u));
    if (!rows || !count)
        return view;
    const size_t current = running ? done : count;
    size_t first = 0;
    if (count > rows)
        first = std::min(count - rows, current + 2 > rows ? current + 2 - rows : 0);
    for (size_t i = first; i < count && view.rows.size() < rows; ++i) {
        SplitRow row;
        const bool final_split = !best.empty() && i + 1 == best.size();
        row.name = tournament(timer.category()) ? (final_split ? "Final" : "Round " + std::to_string(i + 1))
                                              : (final_split ? "Kid Buu" : "Battle " + std::to_string(i + 1));
        row.done = i < done;
        row.current = running && i == done;
        if (row.done) {
            row.time = wins[i].real;
            row.has_time = true;
            if (i < best.size()) {
                row.has_delta = true;
                row.delta = wins[i].real - best[i];
            }
            const double segment = wins[i].real - (i ? wins[i - 1].real : 0.0);
            row.gold = i < golds.size() && segment < golds[i];
        } else if (i < best.size()) {
            row.time = best[i];
            row.has_time = true;
            if (row.current && view.timer > best[i]) {
                row.has_delta = true;
                row.delta = view.timer - best[i];
            }
        }
        view.rows.push_back(row);
    }
    return view;
}

void RunLog::open(const std::filesystem::path &file, const std::string &header) {
    std::filesystem::create_directories(file.parent_path());
    out_.open(file, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out_)
        throw std::runtime_error("Cannot write speedrun log: " + file.string());
    path_ = file;
    chain_.clear();
    write(header);
}

void RunLog::write(const std::string &line) {
    if (!out_.is_open())
        return;
    chain_ = sha256_text(chain_ + line);
    out_ << line << "  #" << chain_.substr(0, 16) << '\n';
    out_.flush();
}

bool RunLog::verify(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::string line, chain;
    bool any = false;
    while (std::getline(in, line)) {
        const auto mark = line.rfind("  #");
        if (mark == std::string::npos || line.size() - mark != 19)
            return false;
        chain = sha256_text(chain + line.substr(0, mark));
        if (chain.compare(0, 16, line, mark + 3, 16) != 0)
            return false;
        any = true;
    }
    return any;
}

}
