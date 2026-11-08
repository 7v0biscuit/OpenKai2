// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace openkai2::launcher {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    constexpr bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};

struct ThemeChoice {
    const wchar_t *name;
    float r, g, b;
    Rgb accent;
};
inline constexpr ThemeChoice themes[] = {
    {L"Ember", 0.47f, 0.58f, 1.0f, {255, 136, 28}},   {L"Ocean", 0.36f, 0.62f, 1.0f, {64, 156, 255}},
    {L"Forest", 0.55f, 1.0f, 0.72f, {86, 200, 120}},  {L"Violet", 0.78f, 0.55f, 1.0f, {168, 112, 255}},
    {L"Crimson", 1.0f, 0.5f, 0.62f, {236, 72, 84}},   {L"Graphite", 0.9f, 0.93f, 1.0f, {214, 218, 228}},
};
inline constexpr unsigned theme_count = sizeof(themes) / sizeof(themes[0]);

struct AccentChoice {
    const wchar_t *name;
    Rgb color;
};
inline constexpr AccentChoice accents[] = {
    {L"Theme color", {0, 0, 0}},    {L"Orange", {255, 136, 28}}, {L"Gold", {246, 196, 64}},
    {L"Red", {236, 72, 84}},        {L"Pink", {244, 110, 190}},  {L"Purple", {168, 112, 255}},
    {L"Blue", {64, 156, 255}},      {L"Cyan", {52, 206, 220}},   {L"Green", {86, 200, 120}},
    {L"White", {226, 230, 238}},
};
inline constexpr unsigned accent_count = sizeof(accents) / sizeof(accents[0]);

inline constexpr unsigned tab_count = 7;
inline constexpr const wchar_t *tab_names[tab_count] = {L"Play", L"Online", L"Speedrun", L"Mods", L"Saves", L"Settings", L"Updates"};
inline constexpr unsigned fixed_tabs = (1u << 0u) | (1u << 5u);

struct Theme {
    Rgb background, sidebar, panel, field, raised, hover, border, ink, muted, faint;
    Rgb accent, highlight, glow, heroFrom, heroTo;
    Rgb buttonTop, buttonBottom, buttonHotTop, buttonHotBottom, buttonPressedTop, buttonPressedBottom, buttonShine, buttonInk;
};

constexpr uint8_t channel(float value) { return uint8_t(std::clamp(value + 0.5f, 0.0f, 255.0f)); }
constexpr Rgb blend(Rgb a, Rgb b, unsigned amount) {
    const auto part = [&](uint8_t x, uint8_t y) { return uint8_t((x * (255u - amount) + y * amount) / 255u); };
    return {part(a.r, b.r), part(a.g, b.g), part(a.b, b.b)};
}
constexpr unsigned luminance(Rgb c) { return (c.r * 299u + c.g * 587u + c.b * 114u) / 1000u; }

constexpr Theme make_theme(unsigned theme, unsigned accent) {
    const auto &t = themes[std::min(theme, theme_count - 1u)];
    const auto tone = [&](float level, float grey) {
        const float r = t.r + (1.0f - t.r) * grey, g = t.g + (1.0f - t.g) * grey, b = t.b + (1.0f - t.b) * grey;
        return Rgb{channel(level * r), channel(level * g), channel(level * b)};
    };
    Theme out{};
    out.background = tone(19, 0.0f);
    out.sidebar = tone(24, 0.0f);
    out.panel = tone(33, 0.0f);
    out.field = tone(42, 0.0f);
    out.raised = tone(53, 0.0f);
    out.hover = tone(69, 0.0f);
    out.border = tone(61, 0.0f);
    out.ink = {242, 238, 230};
    out.muted = tone(174, 0.7f);
    out.faint = tone(117, 0.55f);
    const Rgb white{255, 255, 255}, black{0, 0, 0};
    out.accent = accent && accent < accent_count ? accents[accent].color : t.accent;
    out.highlight = blend(out.accent, Rgb{255, 236, 170}, 120);
    out.glow = blend(out.accent, black, 20);
    out.heroFrom = blend(black, out.accent, 64);
    out.heroTo = tone(31, 0.0f);
    out.buttonTop = blend(out.accent, white, 80);
    out.buttonBottom = blend(out.accent, black, 12);
    out.buttonHotTop = blend(out.accent, white, 130);
    out.buttonHotBottom = blend(out.accent, white, 40);
    out.buttonPressedTop = blend(out.accent, black, 25);
    out.buttonPressedBottom = blend(out.accent, black, 55);
    out.buttonShine = blend(out.buttonTop, Rgb{255, 245, 214}, 150);
    out.buttonInk = luminance(out.accent) > 110u ? blend(out.accent, black, 220) : white;
    if (std::min(theme, theme_count - 1u) == 0u) {
        out.background = {9, 11, 19};
        out.sidebar = {12, 14, 24};
        out.panel = {17, 20, 33};
        out.field = {22, 26, 42};
        out.raised = {29, 34, 53};
        out.hover = {39, 45, 69};
        out.border = {35, 41, 61};
        out.muted = {148, 153, 174};
        out.faint = {86, 93, 117};
        out.heroTo = {15, 17, 31};
        if (accent <= 1u) {
            out.highlight = {255, 204, 86};
            out.glow = {255, 112, 24};
            out.heroFrom = {64, 24, 8};
            out.buttonTop = {255, 186, 70};
            out.buttonBottom = {250, 126, 22};
            out.buttonHotTop = {255, 214, 110};
            out.buttonHotBottom = {255, 150, 44};
            out.buttonPressedTop = {236, 120, 22};
            out.buttonPressedBottom = {214, 96, 14};
            out.buttonShine = blend(out.buttonTop, Rgb{255, 245, 214}, 150);
            out.buttonInk = {36, 16, 4};
        }
    }
    return out;
}

constexpr bool valid_tab_order(const std::array<unsigned, tab_count> &order, unsigned hidden) {
    unsigned seen = 0;
    for (unsigned page : order) {
        if (page >= tab_count || (seen & (1u << page)))
            return false;
        seen |= 1u << page;
    }
    return (hidden & fixed_tabs) == 0u && hidden < (1u << tab_count);
}

}
