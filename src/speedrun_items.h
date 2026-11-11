// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace openkai2::speedrun {

enum OverlayItem : unsigned {
    show_timer,
    show_splits,
    show_segment,
    show_previous,
    show_time_save,
    show_best_possible,
    show_sum_of_best,
    show_personal_best,
    show_ps2_time,
    show_attempts,
    show_status,
    overlay_item_count
};

struct OverlayItemText {
    const wchar_t *name;
    const wchar_t *about;
};
inline constexpr OverlayItemText overlay_item_text[overlay_item_count] = {
    {L"Run timer", L"Real time since the run started"},
    {L"Splits", L"Each battle won, with the difference to your best run"},
    {L"Segment timer", L"Time spent since the last battle"},
    {L"Previous segment", L"Last segment against the same segment of your best run"},
    {L"Possible time save", L"Your best run's segment against your best ever segment"},
    {L"Best possible time", L"Where the run ends if every segment left is your best"},
    {L"Sum of best", L"All your best segments added together"},
    {L"Personal best", L"Your fastest finished run"},
    {L"PS2 time", L"Time counted from the console's own frames"},
    {L"Attempts", L"Runs started and runs finished"},
    {L"Run checks", L"Category, permadeath and whether the run is verified"},
};

inline constexpr unsigned overlay_bit(OverlayItem item) { return 1u << unsigned(item); }
inline constexpr unsigned overlay_all = (1u << unsigned(overlay_item_count)) - 1u;
inline constexpr unsigned overlay_defaults =
    overlay_bit(show_timer) | overlay_bit(show_splits) | overlay_bit(show_segment) | overlay_bit(show_previous) |
    overlay_bit(show_sum_of_best) | overlay_bit(show_personal_best) | overlay_bit(show_attempts) |
    overlay_bit(show_status);
inline constexpr unsigned overlay_required = overlay_bit(show_status) | overlay_bit(show_ps2_time);
inline constexpr unsigned overlay_split_rows[] = {3, 5, 8, 12};

struct OverlayColor {
    const wchar_t *name;
    unsigned char r, g, b;
};
inline constexpr OverlayColor overlay_colors[] = {
    {L"White", 245, 245, 240}, {L"Grey", 190, 192, 196},  {L"Green", 105, 210, 120}, {L"Red", 232, 102, 88},
    {L"Gold", 250, 196, 70},   {L"Orange", 255, 150, 40}, {L"Blue", 90, 160, 255},   {L"Cyan", 80, 220, 230},
    {L"Purple", 180, 120, 255}, {L"Pink", 255, 120, 200}, {L"Yellow", 250, 235, 90},
};
inline constexpr unsigned overlay_color_count = sizeof(overlay_colors) / sizeof(overlay_colors[0]);
enum OverlayColorRole : unsigned { color_text, color_ahead, color_behind, color_gold, overlay_color_roles };
inline constexpr unsigned overlay_color_defaults[overlay_color_roles] = {0, 2, 3, 4};
inline constexpr const wchar_t *overlay_decimal_names[] = {L"Whole seconds", L"Tenths", L"Hundredths", L"Milliseconds"};

}
