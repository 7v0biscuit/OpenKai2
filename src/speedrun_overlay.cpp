// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun_overlay.h"
#include "speedrun_host.h"
#include "speedrun_items.h"
#include "host_input.h"
#include "machine_state.h"
#include "netplay.h"
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include "ps2_runtime.h"
#include <raylib.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <set>
#ifdef _WIN32
#include "launcher/display_font.h"
#endif

namespace openkai2::speedrun {
namespace {
std::atomic<bool> reset{false};
std::atomic<bool> to_menu{false};
std::mutex toastMutex;
std::string toastText;
double toastUntil = 0;

double seconds();

void toast(const std::string &text) {
    std::lock_guard lock(toastMutex);
    toastText = text;
    toastUntil = seconds() + 2.5;
}

void save_slot(unsigned slot) {
    openkai2::request_machine_save([slot](openkai2::MachineStateResult result) {
        if (!result.ok) {
            toast("Could not save state: " + result.message);
            return;
        }
        std::thread([slot, data = std::move(result.data)]() {
            try {
                const auto file = openkai2::machine_state_file(slot);
                std::filesystem::create_directories(file.parent_path());
                const auto wrapped = openkai2::wrap_machine_state(data);
                auto temporary = file;
                temporary += ".tmp";
                {
                    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char *>(wrapped.data()), std::streamsize(wrapped.size()));
                    if (!out)
                        throw std::runtime_error("cannot write the file");
                }
                std::filesystem::rename(temporary, file);
                toast("State saved to slot " + std::to_string(slot));
            } catch (const std::exception &error) {
                toast(std::string("Could not save state: ") + error.what());
            }
        }).detach();
    });
}

void load_slot(unsigned slot) {
    std::ifstream in(openkai2::machine_state_file(slot), std::ios::binary);
    if (!in) {
        toast("Slot " + std::to_string(slot) + " is empty");
        return;
    }
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), {});
    std::vector<uint8_t> body;
    std::string problem;
    if (!openkai2::unwrap_machine_state(file, body, problem)) {
        toast("Cannot load slot " + std::to_string(slot) + ": " + problem);
        return;
    }
    openkai2::request_machine_load(std::move(body), [slot](openkai2::MachineStateResult result) {
        toast(result.ok ? "State loaded from slot " + std::to_string(slot) : "Could not load state: " + result.message);
    });
}

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string stamp() {
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    return text;
}

std::string file_stamp() {
    auto text = stamp();
    for (auto &c : text)
        if (c == ' ' || c == ':')
            c = c == ' ' ? '_' : '-';
    return text;
}

std::string format_delta(double difference) {
    const char sign = difference < 0 ? '-' : '+';
    const double size = std::fabs(difference);
    char text[32];
    if (size < 60)
        std::snprintf(text, sizeof text, "%c%.2f", sign, std::floor(size * 100) / 100);
    else
        std::snprintf(text, sizeof text, "%c%s", sign, format_time(size, false).c_str());
    return text;
}

struct Overlay {
    OverlayOptions options;
    bool speedrun = false;
    Timer timer;
    SpeedCheck speed;
    std::unique_ptr<Watchdog> watchdog;
    std::unique_ptr<LiveSplit> livesplit;
    RunLog log;
    SplitBook book;
    std::vector<std::string> flags;
    std::set<std::string> seen;
    unsigned runs = 0;
    bool recorded = true, personal_best = false;
    double died_at = -1, game_time_sent = 0;
    double fps_window = 0;
    unsigned host_frames = 0, host_fps = 0;
    Font font{}, label_font{};
    bool hidden = false;
    bool skipping = false;
    double skip_started = 0, last_press = 0;
    uint64_t last_fields = 0;
    bool own_font = false, own_label = false;

    void note(const std::string &line) {
        std::cout << "[speedrun] " << line << std::endl;
        log.write(stamp() + "  " + line);
    }
    void flag(const std::string &reason) {
        if (std::find(flags.begin(), flags.end(), reason) != flags.end())
            return;
        flags.push_back(reason);
        note("integrity: " + reason);
    }
    std::string times(double now, uint64_t fields) const {
        return "real " + format_time(timer.real(now)) + "  ps2 " + format_time(timer.game(fields));
    }
    void save(bool finished, double real, const std::vector<Split> &wins) {
        if (recorded)
            return;
        recorded = true;
        Attempt attempt;
        attempt.date = stamp();
        attempt.finished = finished;
        attempt.real = real;
        for (const auto &win : wins)
            attempt.splits.push_back(win.real);
        personal_best = book.record(attempt);
        try {
            book.save(options.splits);
        } catch (const std::exception &error) {
            std::cout << "[speedrun] " << error.what() << std::endl;
        }
        if (personal_best)
            note("personal best " + format_time(real));
    }

    void update(PS2Runtime &runtime, double now, uint64_t fields) {
        const auto state = read_guest(runtime.memory().getRDRAM(), PS2_RAM_SIZE);
        if (skipping)
            skip_to_menu(state, now);
        for (const auto &name : state.tasks)
            if (seen.size() < 4096 && seen.insert(name).second)
                std::cout << "[speedrun:task] " << name << std::endl;
        const auto previous_wins = timer.wins();
        const double previous_real = timer.real(now);
        for (const auto event : timer.update(state, now, fields)) {
            switch (event) {
            case Event::started:
            case Event::restarted:
                save(false, previous_real, previous_wins);
                ++runs;
                recorded = false;
                personal_best = false;
                died_at = -1;
                note("run " + std::to_string(runs) + " started (" + category_name(options.category) +
                     (options.permadeath ? ", permadeath" : "") + ")");
                if (livesplit) {
                    livesplit->send("reset");
                    livesplit->send("starttimer");
                    livesplit->send("initgametime");
                    livesplit->send("pausegametime");
                }
                break;
            case Event::battle_won:
                note("battle " + std::to_string(timer.wins().size()) + " won  real " +
                     format_time(timer.wins().back().real) + "  ps2 " + format_time(timer.wins().back().game));
                break;
            case Event::finished: {
                const auto result = timer.result();
                note("run " + std::to_string(runs) + " finished  real " + format_time(result.real) + "  ps2 " +
                     format_time(result.game) + "  battles " + std::to_string(timer.wins().size()) +
                     (flags.empty() ? "  verified" : "  flagged"));
                save(true, result.real, timer.wins());
                if (livesplit) {
                    livesplit->send("setgametime " + format_time(result.game));
                    livesplit->send("split");
                }
                break;
            }
            case Event::battle_lost:
            case Event::died:
                note(std::string(event == Event::died ? "game over" : "fight lost") + " at " + times(now, fields));
                if (options.permadeath && died_at < 0) {
                    died_at = now;
                    save(false, timer.real(now), timer.wins());
                    if (livesplit)
                        livesplit->send("reset");
                }
                break;
            default:
                break;
            }
        }
        if (speed.sample(now, fields))
            flag("the game ran faster than a PS2 (" + std::to_string(int(speed.rate() + 0.5)) + " fields a second)");
        if (watchdog)
            for (const auto &reason : watchdog->flags())
                flag(reason);
        if (timer.phase() == Phase::running && livesplit && now - game_time_sent >= 0.25) {
            game_time_sent = now;
            livesplit->send("setgametime " + format_time(timer.game(fields)));
        }
        if (IsKeyPressed(KEY_F8) && !reset) {
            note("reset by player at " + times(now, fields));
            if (timer.phase() == Phase::running)
                save(false, timer.real(now), timer.wins());
            reset = true;
        }
        if (died_at >= 0 && now - died_at > 2.5 && !reset) {
            note("permadeath: run over, back to the main menu");
            to_menu = true;
            reset = true;
        }
        if (reset)
            runtime.requestStop();
    }

    void skip_to_menu(const GuestState &state, double now) {
        if (skip_started == 0)
            skip_started = now;
        if (state.main_menu || now - skip_started > 240.0) {
            skipping = false;
            note(state.main_menu ? "back at the main menu" : "stopped returning to the main menu");
            return;
        }
        if (now - last_press < 0.45)
            return;
        last_press = now;
        const bool title = !state.title_menu &&
                           (state.press_start ||
                            std::find(state.tasks.begin(), state.tasks.end(), "# OPENING #") != state.tasks.end() ||
                            std::find(state.tasks.begin(), state.tasks.end(), "LOGO") != state.tasks.end());
        auto packet = LatchedPadState::neutral();
        const uint16_t buttons = uint16_t(~(title ? 0x0008u : 0x4000u));
        packet[2] = uint8_t(buttons);
        packet[3] = uint8_t(buttons >> 8);
        publish_host_input(0, packet);
    }

    void notice(const std::string &message) {
        const float s = std::max(0.75f, float(GetScreenHeight()) / 1080.0f), size = 18 * s;
        const float width = label_width(message, size), x = (float(GetScreenWidth()) - width) / 2 - 12 * s;
        const float y = float(GetScreenHeight()) - 70 * s;
        DrawRectangleRounded({x, y, width + 24 * s, size + 12 * s}, 0.3f, 6, Color{10, 12, 18, 170});
        label(message, x + 12 * s, y + 6 * s, size, Color{245, 245, 240, 235});
    }

    void online(const openkai2::netplay::Status &status) {
        namespace net = openkai2::netplay;
        std::string text;
        Color color{245, 245, 240, 235};
        if (status.phase == net::Phase::playing) {
            text = "Online  vs " + status.opponent + "   " + std::to_string(status.ping_ms) + " ms   delay " +
                   std::to_string(status.delay);
            if (status.waiting)
                text += "   waiting for " + status.opponent;
        } else {
            text = status.message;
        }
        if (status.desynced) {
            text += "   out of sync";
            color = Color{255, 120, 110, 240};
        }
        if (text.empty())
            return;
        const float s = std::max(0.75f, float(GetScreenHeight()) / 1080.0f), size = 16 * s;
        const float width = label_width(text, size), x = (float(GetScreenWidth()) - width) / 2 - 10 * s;
        const float y = 14 * s;
        DrawRectangleRounded({x, y, width + 20 * s, size + 10 * s}, 0.35f, 6, Color{10, 12, 18, 160});
        label(text, x + 10 * s, y + 5 * s, size, color);
    }

    void count(double now) {
        ++host_frames;
        if (fps_window == 0)
            fps_window = now;
        if (now - fps_window >= 0.5) {
            host_fps = unsigned(host_frames / (now - fps_window) + 0.5);
            host_frames = 0;
            fps_window = now;
        }
    }

    void load_font() {
        if (const char *windows = std::getenv("WINDIR"))
            for (const char *name : {"seguisb.ttf", "segoeuib.ttf", "segoeui.ttf", "tahomabd.ttf", "arialbd.ttf"}) {
                const std::string path = std::string(windows) + "\\Fonts\\" + name;
                if (FileExists(path.c_str())) {
                    font = LoadFontEx(path.c_str(), 72, nullptr, 0);
                    break;
                }
            }
        own_label = font.texture.id != 0;
        label_font = own_label ? font : GetFontDefault();
#ifdef _WIN32
        if (font.texture.id == 0)
            font = LoadFontFromMemory(".ttf", launcher::display_font, int(launcher::display_font_size), 72,
                                      nullptr, 0);
#endif
        own_font = font.texture.id != 0;
        if (own_font)
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        else
            font = GetFontDefault();
    }

    float draw_text(const Font &face, bool smooth, const std::string &value, float x, float y, float size,
                    Color color) const {
        if (!smooth)
            size = std::max(10.0f, std::round(size / 10.0f) * 10.0f);
        const float spacing = smooth ? 0.0f : size / 10.0f;
        const Vector2 at{std::round(x), std::round(y)};
        DrawTextEx(face, value.c_str(), {at.x + 1.0f, at.y + 1.0f}, size, spacing,
                   Color{0, 0, 0, uint8_t(color.a * 0.7f)});
        DrawTextEx(face, value.c_str(), at, size, spacing, color);
        return MeasureTextEx(face, value.c_str(), size, spacing).x;
    }
    float text(const std::string &value, float x, float y, float size, Color color) const {
        return draw_text(font, own_font, value, x, y, size, color);
    }
    float label(const std::string &value, float x, float y, float size, Color color) const {
        return draw_text(label_font, own_label, value, x, y, size, color);
    }
    float text_width(const std::string &value, float size) const {
        return MeasureTextEx(font, value.c_str(), size, own_font ? 0.0f : size / 10.0f).x;
    }
    float label_width(const std::string &value, float size) const {
        if (!own_label)
            size = std::max(10.0f, std::round(size / 10.0f) * 10.0f);
        return MeasureTextEx(label_font, value.c_str(), size, own_label ? 0.0f : size / 10.0f).x;
    }

    struct Row {
        std::string left, middle, right;
        Color left_color{}, middle_color{}, right_color{};
        float size = 14;
        bool timer = false, highlight = false, rule = false;
    };

    void draw(double now) {
        if (IsKeyPressed(KEY_F7))
            hidden = !hidden;
        if (hidden)
            return;
        const float scale_choice[] = {0.85f, 1.0f, 1.25f};
        const float s = std::max(0.75f, float(GetScreenHeight()) / 1080.0f) * scale_choice[std::min(options.size, 2u)];
        const Color white{245, 245, 240, 235}, dim{245, 245, 240, 110}, grey{190, 192, 196, 190},
            faint{190, 192, 196, 120}, ahead{105, 210, 120, 240}, behind{232, 102, 88, 240},
            gold{250, 196, 70, 245};
        const unsigned corner = std::min(options.corner, 3u);
        if (options.fps_counter) {
            const std::string value = std::to_string(host_fps) + " FPS";
            const float size = 20 * s, width = label_width(value, size), padX = 9 * s, padY = 4 * s;
            const bool left = speedrun && corner % 2 == 1;
            const float x = left ? 14 * s : float(GetScreenWidth()) - width - 14 * s - padX * 2, y = 10 * s;
            DrawRectangleRounded({x, y, width + padX * 2, size + padY * 2}, 0.35f, 6, Color{10, 12, 18, 150});
            label(value, x + padX, y + padY, size, Color{245, 245, 240, 230});
        }
        if (skipping) {
            const std::string value = "Returning to the main menu";
            const float size = 18 * s, width = label_width(value, size);
            label(value, (float(GetScreenWidth()) - width) / 2, float(GetScreenHeight()) - 60 * s, size, grey);
        }
        if (!speedrun)
            return;
        const auto shows = [&](OverlayItem item) { return (options.items & overlay_bit(item)) != 0; };
        const auto phase = timer.phase();
        const auto view = make_view(timer, book, now, shows(show_splits) ? options.split_rows : 0);
        const auto short_time = [](double value) {
            char text[32];
            if (value < 60) {
                std::snprintf(text, sizeof text, "%.2f", std::floor(std::max(0.0, value) * 100) / 100);
                return std::string(text);
            }
            return format_time(value);
        };
        const auto tone = [&](double difference, bool best_segment) {
            return best_segment ? gold : difference < 0 ? ahead : behind;
        };

        std::vector<Row> rows;
        if (shows(show_status)) {
            Row row;
            row.left = std::string(category_name(options.category)) + (options.permadeath ? "   Permadeath" : "");
            row.left_color = grey;
            row.right = died_at >= 0 ? "Run over" : flags.empty() ? "Verified" : "Flagged";
            row.right_color = died_at >= 0 || !flags.empty() ? behind : ahead;
            row.size = 12;
            rows.push_back(row);
        }
        if (!view.rows.empty()) {
            if (!rows.empty())
                rows.push_back(Row{{}, {}, {}, {}, {}, {}, 6, false, false, true});
            for (const auto &split : view.rows) {
                Row row;
                row.left = split.name;
                row.left_color = split.done || split.current ? white : grey;
                if (split.has_delta) {
                    row.middle = format_delta(split.delta);
                    row.middle_color = split.done ? tone(split.delta, split.gold) : behind;
                }
                row.right = split.has_time ? format_time(split.time) : "-";
                row.right_color = split.done ? white : faint;
                row.highlight = split.current;
                row.size = 14;
                rows.push_back(row);
            }
        }
        if (shows(show_timer)) {
            Row row;
            row.timer = true;
            row.right = format_time(view.timer);
            row.right_color = phase == Phase::waiting ? dim
                              : phase == Phase::finished && personal_best ? gold
                              : view.pace < 0 ? ahead
                              : view.pace > 0 ? behind
                                              : white;
            row.size = 40;
            if (phase == Phase::waiting) {
                row.left = "Start Dragon World";
                row.left_color = faint;
            }
            rows.push_back(row);
        }
        auto info = [&](const char *name, const std::string &value, Color color) {
            Row row;
            row.left = name;
            row.left_color = grey;
            row.right = value;
            row.right_color = color;
            row.size = 13;
            rows.push_back(row);
        };
        if (shows(show_segment) && phase == Phase::running)
            info("Segment", short_time(view.segment), white);
        if (shows(show_previous))
            info("Previous segment", view.has_previous ? format_delta(view.previous) : "-",
                 view.has_previous ? tone(view.previous, false) : faint);
        if (shows(show_time_save))
            info("Possible time save", view.has_time_save ? short_time(view.time_save) : "-", view.has_time_save ? white : faint);
        if (shows(show_best_possible))
            info("Best possible time", view.has_best_possible ? format_time(view.best_possible) : "-",
                 view.has_best_possible ? white : faint);
        if (shows(show_sum_of_best))
            info("Sum of best", view.has_sum_of_best ? format_time(view.sum_of_best) : "-",
                 view.has_sum_of_best ? white : faint);
        if (shows(show_personal_best))
            info("Personal best", book.has_best() ? format_time(book.best_time()) : "-",
                 book.has_best() ? white : faint);
        if (shows(show_ps2_time))
            info("PS2 time", format_time(timer.game(last_fields)), white);
        if (shows(show_attempts))
            info("Attempts", std::to_string(view.attempt) + "  (" + std::to_string(view.finished) + " finished)", white);
        if (rows.empty())
            return;

        const float pad = 11 * s, gap = 14 * s;
        float column = 0;
        for (const auto &row : rows)
            if (!row.middle.empty() || (!row.rule && !row.timer && !row.right.empty() && row.size == 14))
                column = std::max(column, label_width(row.right, row.size * s));
        float width = 210 * s;
        for (const auto &row : rows) {
            if (row.rule)
                continue;
            const float size = row.size * s;
            float need = row.left.empty() ? 0 : label_width(row.left, row.timer ? 12 * s : size) + gap;
            if (row.timer)
                need += text_width(row.right, size);
            else if (!row.middle.empty() || row.size == 14)
                need += label_width(row.middle, size) + gap + column;
            else
                need += label_width(row.right, size);
            width = std::max(width, need + pad * 2);
        }
        width = std::ceil(width);
        float height = pad * 2;
        for (const auto &row : rows)
            height += row.rule ? row.size * s : row.timer ? row.size * s * 1.1f : row.size * s * 1.6f;
        const float x = corner % 2 == 0 ? 14 * s : float(GetScreenWidth()) - width - 14 * s;
        float y = corner < 2 ? 12 * s : float(GetScreenHeight()) - height - 12 * s;
        const unsigned char shade[] = {0, 105, 180};
        if (const auto alpha = shade[std::min(options.background, 2u)])
            DrawRectangleRounded({x, y, width, height}, 0.06f, 6, Color{10, 12, 18, alpha});
        y += pad;
        const float left = x + pad, right = x + width - pad;
        for (const auto &row : rows) {
            const float size = row.size * s;
            if (row.rule) {
                DrawRectangle(int(left), int(y + size / 2), int(right - left), std::max(1, int(s)),
                              Color{255, 255, 255, 28});
                y += size;
                continue;
            }
            const float line = row.timer ? size * 1.1f : size * 1.6f, top = y + (line - size) / 2;
            if (row.highlight)
                DrawRectangleRounded({x + 3 * s, y, width - 6 * s, line}, 0.3f, 4, Color{255, 255, 255, 24});
            if (row.timer) {
                if (!row.left.empty())
                    label(row.left, left, y + line - 17 * s, 12 * s, row.left_color);
                text(row.right, right - text_width(row.right, size), top, size, row.right_color);
            } else {
                if (!row.left.empty())
                    label(row.left, left, top, size, row.left_color);
                if (!row.right.empty())
                    label(row.right, right - label_width(row.right, size), top, size, row.right_color);
                if (!row.middle.empty())
                    label(row.middle, right - column - gap - label_width(row.middle, size), top, size, row.middle_color);
            }
            y += line;
        }
    }
};

std::unique_ptr<Overlay> overlay;

void start(PS2Runtime &, void *) {
    if (overlay)
        overlay->load_font();
}

void draw(PS2Runtime &runtime, void *) {
    if (!overlay)
        return;
    const double now = seconds();
    const uint64_t fields = runtime.memory().gs().vsyncTick.load(std::memory_order_acquire);
    if (overlay->speedrun)
        overlay->update(runtime, now, fields);
    overlay->last_fields = fields;
    auto *session = openkai2::netplay::active();
    if (session && (IsKeyPressed(KEY_F5) || IsKeyPressed(KEY_F9)))
        toast("Save states are off in online play");
    else if (!overlay->speedrun && !openkai2::machine_state_pending()) {
        if (IsKeyPressed(KEY_F5))
            save_slot(1);
        else if (IsKeyPressed(KEY_F9))
            load_slot(1);
    }
    if (overlay->options.fps_counter)
        overlay->count(now);
    overlay->draw(now);
    std::string message;
    {
        std::lock_guard lock(toastMutex);
        if (now < toastUntil)
            message = toastText;
    }
    if (!message.empty())
        overlay->notice(message);
    if (session)
        overlay->online(session->status());
}

void shutdown(PS2Runtime &, void *) {
    if (!overlay)
        return;
    if (overlay->speedrun) {
        const double now = seconds();
        if (overlay->timer.phase() == Phase::running) {
            overlay->note("session ended during run " + std::to_string(overlay->runs));
            overlay->save(false, overlay->timer.real(now), overlay->timer.wins());
        }
        overlay->note("session closed");
    }
    if (overlay->own_font)
        UnloadFont(overlay->font);
    overlay->own_font = overlay->own_label = false;
}
}

void install_overlay(PS2Runtime &runtime, OverlayOptions options) {
    overlay = std::make_unique<Overlay>();
    overlay->speedrun = options.category != Category::off;
    overlay->skipping = options.skip_to_menu;
    overlay->options = std::move(options);
    if (overlay->speedrun) {
        overlay->book = SplitBook::load(overlay->options.splits);
        overlay->log.open(overlay->options.logs / ("run-" + file_stamp() + ".log"), "OpenKai2 speedrun log");
        for (const auto &line : overlay->options.header)
            overlay->log.write(line);
        for (const auto &problem : overlay->options.problems)
            overlay->flag(problem);
        overlay->watchdog = std::make_unique<Watchdog>();
        overlay->livesplit = std::make_unique<LiveSplit>();
        std::cout << "[speedrun] log=" << overlay->log.path().string()
                  << " splits=" << overlay->options.splits.string() << std::endl;
    }
    runtime.setDebugUiCallbacks(start, draw, shutdown, nullptr);
}

bool reset_requested() { return reset; }
bool restart_to_menu() { return to_menu; }

}
