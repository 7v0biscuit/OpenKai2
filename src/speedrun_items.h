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
    {L"Run timer", L"Real time since Dragon World started"},
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
inline constexpr unsigned overlay_required = overlay_bit(show_status);
inline constexpr unsigned overlay_split_rows[] = {3, 5, 8, 12};

}
