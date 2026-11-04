// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace openkai2::speedrun;

namespace {
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}

struct Guest {
    std::vector<uint8_t> memory = std::vector<uint8_t>(0x800000);
    void put(uint32_t address, uint32_t value) { std::memcpy(memory.data() + address, &value, 4); }
    void tasks(const std::vector<std::string> &names) {
        uint32_t address = 0x200000;
        put(task_first, names.empty() ? task_end : address);
        for (size_t i = 0; i < names.size(); ++i, address += 0x80) {
            std::memset(memory.data() + address, 0, 0x80);
            std::memcpy(memory.data() + address, names[i].data(), std::min<size_t>(names[i].size(), 19));
            put(address + task_next, i + 1 < names.size() ? 0x80000000u | (address + 0x80) : task_end);
        }
    }
    void health(float player, float opponent) {
        put(battle_pointer, 0x80000000u | 0x180000u);
        std::memcpy(memory.data() + 0x180000 + fighter_health, &player, 4);
        std::memcpy(memory.data() + 0x180000 + fighter_health + fighter_stride, &opponent, 4);
        const float max = 3000;
        std::memcpy(memory.data() + 0x180000 + fighter_health + 4, &max, 4);
        std::memcpy(memory.data() + 0x180000 + fighter_health + fighter_stride + 4, &max, 4);
    }
    void battle(bool over, int32_t result, uint32_t reason = 6u) {
        put(battle_over, over ? 1u : 0u);
        put(battle_result, uint32_t(result));
        put(battle_reason, reason);
    }
    void unit(unsigned index, uint8_t character, uint8_t health, bool player) {
        const size_t at = map_units + index * map_unit_bytes;
        memory[at] = 0;
        memory[at + 24] = 0xB8;
        memory[at + 25] = 0x0B;
        memory[at + 5] = health;
        memory[at + 8] = character;
        memory[at + 10] = player ? 0x12u : 0x22u;
    }
    GuestState read() const { return read_guest(memory.data(), memory.size()); }
};

bool has(const std::vector<Event> &events, Event event) {
    for (auto e : events)
        if (e == event)
            return true;
    return false;
}

void reads_tasks() {
    Guest guest;
    guest.tasks({"TITLE", "PRESSSTART", "MOKUJI"});
    auto state = guest.read();
    require(state.press_start && state.main_menu, "Title and main menu not recognised");
    guest.tasks({"TITLE", "TITLEMENU"});
    state = guest.read();
    require(state.tasks.size() == 2 && state.tasks[1] == "TITLEMENU", "Task list was not walked");
    require(!state.intro && !state.dragon_world, "Title screen read as Dragon World");
    guest.tasks({"DW_SEQ", "DW INTRO MAIN", "DWI_SYSTEM"});
    state = guest.read();
    require(state.intro && state.dragon_world, "Dragon World intro not seen");
    guest.tasks({"#[DW GOVER] UPDATE"});
    require(guest.read().game_over, "Game over not seen");
    guest.tasks({"#[DW STAFF] MAIN", "#[DW STAFF] BG"});
    require(guest.read().credits, "Credits not seen");
    guest.tasks({});
    require(guest.read().tasks.empty(), "Empty task list not empty");
    guest.put(task_first, 0x200000);
    guest.put(0x200000 + task_next, 0x200000);
    require(guest.read().tasks.size() == task_limit, "Looping task list was not bounded");
    guest.put(task_first, 0x7FFFFF0);
    require(read_guest(guest.memory.data(), guest.memory.size()).tasks.empty(), "Out of range task read");
}

void times_a_run() {
    Guest guest;
    Timer timer;
    guest.tasks({"TITLE"});
    require(timer.update(guest.read(), 1.0, 60).empty() && timer.phase() == Phase::waiting, "Timer started early");
    guest.tasks({"DW INTRO MAIN"});
    require(has(timer.update(guest.read(), 10.0, 600), Event::started), "Run did not start on the intro");
    require(timer.real(12.5) == 2.5, "Real time wrong");
    require(timer.game(600 + 120) > 2.0 && timer.game(720) < 2.01, "PS2 time wrong");
    guest.tasks({"[BTL] MAIN"});
    guest.battle(false, 0);
    timer.update(guest.read(), 20.0, 1000);
    guest.battle(true, -1);
    {
        const auto lost = timer.update(guest.read(), 21.0, 1060);
        require(!has(lost, Event::battle_won) && !has(lost, Event::battle_lost), "Fight result alone ended the run");
    }
    guest.battle(false, 0);
    guest.health(3000, 3000);
    timer.update(guest.read(), 22.0, 1120);
    guest.health(1200, 0);
    guest.battle(true, 1);
    require(has(timer.update(guest.read(), 30.0, 1500), Event::battle_won), "Win not counted");
    require(!has(timer.update(guest.read(), 31.0, 1560), Event::battle_won), "One win counted twice");
    guest.tasks({"#[DW END] MAIN"});
    timer.update(guest.read(), 50.0, 2700);
    guest.tasks({"#[DW STAFF] MAIN"});
    require(has(timer.update(guest.read(), 60.0, 3300), Event::finished), "Credits did not end the run");
    require(timer.phase() == Phase::finished && timer.result().real == 20.0, "Run did not end on the last hit");
    require(timer.real(500.0) == 20.0, "Finished timer kept counting");
    guest.tasks({"DW INTRO MAIN"});
    require(has(timer.update(guest.read(), 70.0, 4000), Event::restarted), "New Dragon World did not restart");
    require(timer.wins().empty() && timer.real(71.0) == 1.0, "Restart kept the old run");
}

void sees_death() {
    Guest guest;
    Timer timer;
    guest.tasks({"#[DW GOVER] MAIN"});
    require(!has(timer.update(guest.read(), 1.0, 0), Event::died), "Death counted before a run");
    guest.tasks({"DW INTRO MAIN"});
    timer.update(guest.read(), 2.0, 60);
    guest.tasks({"#[DW GOVER] ENMA"});
    require(has(timer.update(guest.read(), 3.0, 120), Event::died), "Game over not reported");
    require(!has(timer.update(guest.read(), 3.1, 126), Event::died), "Game over reported twice");
}

void counts_map_health_losses() {
    Guest guest;
    Timer timer;
    guest.unit(0, 0x00, 3, true);
    guest.unit(1, 0x1c, 2, true);
    guest.unit(2, 0x09, 9, false);
    guest.tasks({"DW INTRO MAIN"});
    timer.update(guest.read(), 1.0, 0);
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    timer.update(guest.read(), 2.0, 60);
    guest.tasks({"DWI_SYSTEM", "[BTL] MAIN"});
    guest.battle(false, 0, uint32_t(-1));
    timer.update(guest.read(), 3.0, 120);
    guest.battle(true, -1);
    require(!has(timer.update(guest.read(), 4.0, 180), Event::battle_lost), "Loss counted before map health dropped");
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    guest.battle(false, -1, uint32_t(-1));
    guest.unit(2, 0x09, 7, false);
    require(!has(timer.update(guest.read(), 5.0, 240), Event::battle_lost), "Enemy losing health counted");
    guest.unit(1, 0x1c, 1, true);
    require(has(timer.update(guest.read(), 6.0, 300), Event::battle_lost), "Lost map health not counted");
    require(!has(timer.update(guest.read(), 7.0, 360), Event::battle_lost), "One loss counted twice");
    require(timer.losses() == 1, "Losses miscounted");
}

void ignores_other_health_changes() {
    Guest guest;
    Timer timer;
    guest.unit(0, 0x00, 5, true);
    guest.tasks({"DW INTRO MAIN"});
    timer.update(guest.read(), 1.0, 0);
    guest.tasks({"DWI_SYSTEM", "[BTL] MAIN"});
    guest.battle(true, 1);
    timer.update(guest.read(), 2.0, 60);
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    guest.unit(0, 0x00, 3, true);
    require(!has(timer.update(guest.read(), 3.0, 120), Event::battle_lost), "New stage health counted after a win");
    guest.tasks({"DWI_SYSTEM", "[BTL] MAIN"});
    guest.battle(true, 0, 16u);
    timer.update(guest.read(), 4.0, 180);
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    guest.unit(0, 0x00, 2, true);
    require(!has(timer.update(guest.read(), 5.0, 240), Event::battle_lost), "Story fight counted");
    guest.battle(false, -1);
    guest.unit(0, 0x00, 1, true);
    require(!has(timer.update(guest.read(), 400.0, 9000), Event::battle_lost), "Health drop long after a fight counted");
    guest.unit(0, 0x07, 0, true);
    guest.tasks({"DWI_SYSTEM", "[BTL] MAIN"});
    timer.update(guest.read(), 401.0, 9060);
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    require(!has(timer.update(guest.read(), 402.0, 9120), Event::battle_lost), "A different character counted");
    require(timer.losses() == 0, "Losses miscounted");
}

void sees_losses_from_a_loaded_game() {
    Guest guest;
    Timer timer;
    guest.unit(0, 0x00, 4, true);
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    guest.battle(false, 0, uint32_t(-1));
    timer.update(guest.read(), 1.0, 0);
    guest.tasks({"DWI_SYSTEM", "[BTL] MAIN"});
    timer.update(guest.read(), 2.0, 60);
    guest.battle(false, -1, uint32_t(-1));
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    guest.unit(0, 0x00, 3, true);
    require(has(timer.update(guest.read(), 3.0, 120), Event::battle_lost), "Loss in a loaded Dragon World missed");
    guest.tasks({"MOKUJI"});
    guest.battle(false, 0, uint32_t(-1));
    timer.update(guest.read(), 4.0, 180);
    guest.tasks({"[BTL] MAIN"});
    timer.update(guest.read(), 5.0, 240);
    guest.battle(true, -1);
    guest.unit(0, 0x00, 2, true);
    require(!has(timer.update(guest.read(), 6.0, 300), Event::battle_lost), "Duel loss counted as a Dragon World loss");
}

void knockouts_decide_dragon_world_fights() {
    Guest guest;
    Timer timer;
    guest.unit(0, 0x00, 3, true);
    guest.tasks({"DW INTRO MAIN"});
    timer.update(guest.read(), 1.0, 0);
    guest.tasks({"DWI_SYSTEM", "[BTL] INIT"});
    guest.battle(false, 0, uint32_t(-1));
    guest.health(0, 0);
    std::memset(guest.memory.data() + 0x180000 + fighter_health, 0, fighter_stride + 16);
    require(timer.update(guest.read(), 2.0, 60).empty(), "Loading fight decided");
    guest.health(3000, 3000);
    timer.update(guest.read(), 3.0, 120);
    guest.health(900, 2500);
    require(timer.update(guest.read(), 4.0, 180).empty(), "Lower health mid fight decided");
    guest.health(0, 2500);
    guest.battle(true, 1);
    const auto ko = timer.update(guest.read(), 5.0, 240);
    require(has(ko, Event::battle_lost) && !has(ko, Event::battle_won), "Knockout loss read as a win");
    guest.tasks({"DWI_SYSTEM", "DW MAP"});
    guest.unit(0, 0x00, 2, true);
    require(timer.update(guest.read(), 6.0, 300).empty() && timer.losses() == 1, "One loss counted twice");
    guest.tasks({"DWI_SYSTEM", "[BTL] MAIN"});
    guest.battle(false, 0, uint32_t(-1));
    guest.health(3000, 3000);
    timer.update(guest.read(), 7.0, 360);
    guest.health(1500, 2000);
    guest.battle(true, 1, 16u);
    require(timer.update(guest.read(), 8.0, 420).empty(), "Scripted story ending decided");
    guest.tasks({"DWI_SYSTEM", "[BTL] MAIN"});
    guest.battle(false, 0, uint32_t(-1));
    guest.health(3000, 3000);
    timer.update(guest.read(), 9.0, 480);
    guest.health(1000, 2000);
    guest.battle(true, 1, timeout_reason);
    require(has(timer.update(guest.read(), 10.0, 540), Event::battle_lost), "Timeout loss missed");
    guest.battle(false, 0, uint32_t(-1));
    guest.health(3000, 3000);
    timer.update(guest.read(), 11.0, 600);
    guest.health(2000, 0);
    guest.battle(true, -1);
    require(has(timer.update(guest.read(), 12.0, 660), Event::battle_won), "Knockout win missed");
}

void categories() {
    std::vector<std::string> files;
    for (unsigned i = 1; i < category_count; ++i) {
        const auto category = Category(i);
        require(std::string(category_name(category)) != "Off", "A category has no label");
        require(std::strlen(category_rules(category)) > 30, "A category has no rules");
        const auto file = splits_name(category, false, 0);
        require(std::find(files.begin(), files.end(), file) == files.end(), "Categories share records");
        files.push_back(file);
        Timer timer(category);
        GuestState intro; intro.intro = true; intro.dragon_world = true;
        const auto events = timer.update(intro, 10, 100);
        if (tournament(category)) {
            require(events.empty() && timer.phase() == Phase::waiting, "Dragon World started a tournament timer");
            require(timer.finish(10,100) == Event::none && timer.split(10,100) == Event::none,
                    "A waiting tournament could record a result");
            require(timer.start(20,200) == Event::started, "Tournament did not start");
            require(timer.split(30.125,800) == Event::battle_won, "Tournament round split missing");
            require(timer.split(30.125,800) == Event::none, "Duplicate round split accepted");
            GuestState credits; credits.credits = true;
            timer.update(credits,31,850);
            require(timer.phase() == Phase::running, "Story credits completed a tournament");
            require(timer.finish(40.375,1400) == Event::finished && timer.phase() == Phase::finished &&
                        timer.result().real == 20.375 && timer.wins().size() == 2,
                    "Tournament finish or final split is wrong");
            require(timer.finish(50,2000) == Event::none && timer.real(50) == 20.375,
                    "Finished tournament kept changing");
            require(timer.start(60,3000) == Event::restarted && timer.wins().empty(),
                    "Tournament restart kept previous splits");
            timer.reset();
            require(timer.category() == category && timer.phase() == Phase::waiting, "Reset lost the category");
        } else {
            require(has(events, Event::started), "Dragon World category did not start");
            GuestState credits; credits.credits = true;
            require(has(timer.update(credits,100,6000), Event::finished), "Dragon World category did not finish");
        }
    }
    require(format_time(61.239,3u) == "1:01.239" && format_time(3599.999,3u) == "59:59.999" &&
                format_time(3600.125,3u) == "1:00:00.125", "Millisecond formatting is wrong");
}

void edits_splits() {
    double t = 0;
    require(parse_time("1:02.50", t) && std::abs(t - 62.5) < 1e-9, "m:ss.cc not parsed");
    require(parse_time("1:00:05", t) && t == 3605, "h:mm:ss not parsed");
    require(parse_time("95.25", t) && t == 95.25, "Seconds not parsed");
    require(!parse_time("1:75", t) && !parse_time("abc", t) && !parse_time("", t) && !parse_time("1::2", t),
            "Bad times accepted");
    require(format_time(61.239, 1u) == "1:01.2" && format_time(61.239, 0u) == "1:01" && format_time(61.239, 2u) == "1:01.23",
            "Precision formatting wrong");
    require(splits_name(Category::any, true, 3) == "any-permadeath-z3.txt" && splits_name(Category::new_game_plus, false, 0) == "ngplus.txt",
            "Splits file names wrong");
    require(category_label(Category::any, 2) == "Any% Z2" && category_label(Category::new_game_plus, 0) == "NG+",
            "Category labels wrong");
    SplitBook book;
    book.record({"day", true, 100, {30, 60, 100}});
    require(book.set_split(1, 55) && book.best()[1] == 55, "Split not changed");
    book.add_split(80);
    require(book.best().size() == 4 && book.best()[2] == 80 && book.golds().size() == 4, "Split not added");
    require(book.remove_split(0) && book.best().size() == 3 && book.best()[0] == 55, "Split not removed");
    require(!book.set_split(9, 1) && !book.remove_split(9), "Out of range edit accepted");
    book.clear_best();
    require(!book.has_best() && book.golds().empty(), "Best run not cleared");
}

void formats() {
    require(format_time(0) == "0:00.00", "Zero time");
    require(format_time(61.239) == "1:01.23", "Minutes");
    require(format_time(3725.5) == "1:02:05.50", "Hours");
    require(format_time(-3) == "0:00.00", "Negative time");
    require(format_time(59.999, false) == "0:59", "Whole seconds");
}

void checks() {
    SpeedCheck speed;
    require(!speed.sample(0, 0) && !speed.sample(10, 599), "Normal speed flagged");
    require(speed.sample(20, 599 + 700), "Fast game not flagged");
    require(!speed.sample(30, 1299 + 300), "Slow game flagged");
    ClockCheck clock;
    require(!clock.sample(0, 100) && !clock.sample(10, 110), "Matching clocks flagged");
    require(clock.sample(25, 120), "Fast game clock not flagged");
    require(!clock.sample(30, 3000), "Long sleep flagged");
    std::vector<DiscFileHash> files;
    for (const auto &known : retail_files)
        files.push_back({known.path, known.sha256});
    require(check_game_files(files).empty(), "Retail files flagged");
    files[1].path = "usr\\data_cmn.afs";
    require(check_game_files(files).empty(), "Path spelling flagged");
    files[1].sha256 = "00";
    require(check_game_files(files).size() == 1, "Changed file not flagged");
    files.pop_back();
    require(check_game_files(files).size() == 2, "Missing file not flagged");
    require(suspicious_process("cheatengine-x86_64-SSE4-AVX2.exe") && suspicious_process("x64dbg.exe"),
            "Cheat tools not recognised");
    require(!suspicious_process("explorer.exe") && !suspicious_process("LiveSplit.exe"), "Normal programs flagged");
}

void splits() {
    const auto file = std::filesystem::temp_directory_path() / "openkai2-splits-test" / "any.txt";
    std::filesystem::remove_all(file.parent_path());
    auto book = SplitBook::load(file);
    require(!book.has_best() && book.attempts() == 0, "New split book not empty");
    require(!book.record({"day 1", false, 50, {20, 45}}), "Reset counted as a best");
    require(book.golds().size() == 2 && book.golds()[1] == 25, "Reset segments not kept as golds");
    require(book.record({"day 1", true, 100, {30, 60, 100}}), "First finish not a best");
    require(book.golds()[0] == 20 && book.golds()[2] == 40, "Golds wrong");
    require(!book.record({"day 2", true, 110, {25, 70, 110}}), "Slower finish counted as a best");
    require(book.golds()[0] == 20 && book.best_time() == 100, "Slower run changed the best");
    double difference = 0;
    require(book.delta(1, 58, difference) && difference == -2, "Delta wrong");
    require(!book.delta(3, 58, difference), "Delta past the last split");
    require(book.gold(2, 39) && !book.gold(2, 41), "Gold segment check wrong");
    book.save(file);
    auto read = SplitBook::load(file);
    require(read.attempts() == 3 && read.completed() == 2 && read.best() == book.best() &&
                read.golds() == book.golds() && read.history().size() == 3,
            "Splits did not survive saving");
    require(read.history()[1].find("personal best") != std::string::npos, "History missed the best");
    std::filesystem::remove_all(file.parent_path());
}

void views() {
    SplitBook book;
    book.record({"day 1", true, 100, {30, 60, 100}});
    book.record({"day 2", false, 40, {25}});
    Guest guest;
    Timer timer;
    auto empty = make_view(timer, book, 0, 5);
    require(empty.rows.size() == 3 && empty.rows[2].name == "Kid Buu" && !empty.rows[0].done,
            "Waiting view does not list the best run");
    require(empty.has_sum_of_best && empty.sum_of_best == 25 + 30 + 40, "Sum of best wrong");
    require(empty.attempt == 2 && empty.finished == 1, "Attempts wrong");
    guest.tasks({"DW INTRO MAIN"});
    timer.update(guest.read(), 10, 0);
    auto view = make_view(timer, book, 30, 5);
    require(view.rows[0].current && view.pace == -1 && view.attempt == 3, "Fresh run not ahead");
    require(view.has_time_save && view.time_save == 5, "Possible time save wrong");
    require(view.has_best_possible && view.best_possible == 95, "Best possible time wrong");
    view = make_view(timer, book, 45, 5);
    require(view.pace == 1 && view.rows[0].has_delta && view.rows[0].delta == 5, "Live loss not shown");
    require(view.best_possible == 105, "Best possible time did not grow with a slow segment");
    guest.tasks({"[BTL] MAIN"});
    guest.health(3000, 3000);
    timer.update(guest.read(), 37, 0);
    guest.health(1000, 0);
    guest.battle(true, 1);
    timer.update(guest.read(), 38, 0);
    view = make_view(timer, book, 40, 5);
    require(view.rows[0].done && view.rows[0].delta == -2 && view.rows[1].current, "Split row wrong");
    require(view.has_previous && view.previous == -2 && view.segment == 2, "Previous segment wrong");
    require(view.pace == -1, "Ahead after a good split not shown");
    require(make_view(timer, book, 40, 2).rows.front().current, "Split window does not start at the current split");
    require(make_view(timer, book, 40, 0).rows.empty(), "Split rows shown when turned off");
}

void logs() {
    const auto file = std::filesystem::temp_directory_path() / "openkai2-speedrun-test" / "run.log";
    {
        RunLog log;
        log.open(file, "OpenKai2 speedrun log");
        log.write("run 1 started");
        log.write("run 1 finished real 1:23:45.67");
    }
    require(RunLog::verify(file), "Untouched log failed");
    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), {});
    }
    const auto at = text.find("1:23:45.67");
    text.replace(at, 10, "1:13:45.67");
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    }
    require(!RunLog::verify(file), "Edited log passed");
    std::filesystem::remove_all(file.parent_path());
}
}

int main() {
    try {
        reads_tasks();
        times_a_run();
        sees_death();
        counts_map_health_losses();
        ignores_other_health_changes();
        sees_losses_from_a_loaded_game();
        knockouts_decide_dragon_world_fights();
        formats();
        categories();
        edits_splits();
        checks();
        splits();
        views();
        logs();
    } catch (const std::exception &error) {
        std::cerr << "speedrun tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "speedrun tests passed\n";
    return 0;
}
