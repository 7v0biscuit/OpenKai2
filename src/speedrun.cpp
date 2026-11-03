// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun.h"
#include "disc_image.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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
    default: return "Off";
    }
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
        std::memcpy(&state.opponent_health, rdram + battle + fighter_health + fighter_stride, 4);
        state.health_known = std::isfinite(state.player_health) && std::isfinite(state.opponent_health);
    }
    return state;
}

std::string loss_name(const GuestState &) { return "lost a fight and map health"; }

void Timer::reset() { *this = Timer{}; }

std::vector<Event> Timer::update(const GuestState &state, double real, uint64_t fields) {
    std::vector<Event> events;
    const bool fresh = state.intro && !intro_;
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
    const int outcome = state.result == 1 ? 1 : state.result == -1 ? -1 : 0;
    bool won = false, lost = false;
    if (fresh) {
        decided_ = outcome != 0;
    } else if (outcome == 0) {
        decided_ = false;
    } else if (!decided_) {
        decided_ = true;
        won = outcome == 1;
    }
    const bool after_battle = real - last_battle_ <= battle_memory && state.result == -1;
    for (size_t i = 0; i < state.units.size() && i < units_.size(); ++i) {
        const auto &now = state.units[i], &before = units_[i];
        if (now.character == before.character && now.player && before.player && now.health < before.health &&
            after_battle && !fresh)
            lost = true;
    }
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
        if (state.credits && !credits_) {
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

std::string format_time(double seconds, bool hundredths) {
    if (!(seconds > 0))
        seconds = 0;
    const auto centis = uint64_t(std::floor(seconds * 100.0));
    const auto whole = centis / 100, hours = whole / 3600, minutes = whole / 60 % 60, secs = whole % 60;
    char text[48];
    if (hours)
        std::snprintf(text, sizeof text, "%llu:%02llu:%02llu", static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes), static_cast<unsigned long long>(secs));
    else
        std::snprintf(text, sizeof text, "%llu:%02llu", static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(secs));
    std::string result = text;
    if (hundredths) {
        std::snprintf(text, sizeof text, ".%02llu", static_cast<unsigned long long>(centis % 100));
        result += text;
    }
    return result;
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
        row.name = final_split ? "Kid Buu" : "Battle " + std::to_string(i + 1);
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
