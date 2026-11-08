// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

namespace openkai2::launcher {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    constexpr bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};

struct ThemeChoice {
    const wchar_t *name;
    float r, g, b;
    Rgb accent;
    bool light;
};
inline constexpr ThemeChoice themes[] = {
    {L"Ember", 0.47f, 0.58f, 1.0f, {255, 136, 28}, false},     {L"Ocean", 0.36f, 0.62f, 1.0f, {64, 156, 255}, false},
    {L"Forest", 0.55f, 1.0f, 0.72f, {86, 200, 120}, false},    {L"Violet", 0.78f, 0.55f, 1.0f, {168, 112, 255}, false},
    {L"Crimson", 1.0f, 0.5f, 0.62f, {236, 72, 84}, false},     {L"Graphite", 0.9f, 0.93f, 1.0f, {214, 218, 228}, false},
    {L"Saiyan Gold", 1.0f, 0.82f, 0.5f, {255, 196, 40}, false}, {L"Namek", 0.45f, 1.0f, 0.85f, {70, 220, 170}, false},
    {L"Frieza", 0.9f, 0.55f, 1.0f, {214, 90, 230}, false},     {L"Midnight", 0.3f, 0.42f, 1.0f, {70, 200, 255}, false},
    {L"Sakura", 1.0f, 0.62f, 0.8f, {255, 120, 170}, false},    {L"Daylight", 0.95f, 0.97f, 1.0f, {232, 108, 18}, true},
    {L"Ultra Instinct", 0.82f, 0.88f, 1.0f, {196, 212, 255}, false}, {L"Super Saiyan Blue", 0.32f, 0.72f, 1.0f, {40, 196, 255}, false},
    {L"Kaioken", 1.0f, 0.42f, 0.38f, {255, 64, 52}, false},    {L"Majin", 1.0f, 0.55f, 0.85f, {255, 104, 188}, false},
    {L"Shenron", 0.5f, 1.0f, 0.6f, {255, 208, 60}, false},     {L"Ginyu Force", 0.78f, 0.6f, 1.0f, {150, 92, 255}, false},
    {L"Capsule Corp", 0.92f, 0.96f, 1.0f, {28, 118, 228}, true}, {L"Kami's Lookout", 1.0f, 0.97f, 0.9f, {40, 150, 100}, true},
};
inline constexpr unsigned theme_count = sizeof(themes) / sizeof(themes[0]);

struct AccentChoice {
    const wchar_t *name;
    Rgb color;
};
inline constexpr AccentChoice accents[] = {
    {L"Theme color", {0, 0, 0}},    {L"Orange", {255, 136, 28}}, {L"Gold", {246, 196, 64}},   {L"Red", {236, 72, 84}},
    {L"Pink", {244, 110, 190}},     {L"Purple", {168, 112, 255}}, {L"Blue", {64, 156, 255}},  {L"Cyan", {52, 206, 220}},
    {L"Green", {86, 200, 120}},     {L"White", {226, 230, 238}}, {L"Teal", {40, 190, 170}},   {L"Lime", {170, 220, 60}},
    {L"Magenta", {220, 70, 200}},   {L"Coral", {255, 120, 90}},  {L"Sky", {110, 190, 255}},   {L"Indigo", {100, 110, 240}},
    {L"Silver", {170, 176, 190}},   {L"Amber", {255, 170, 0}},   {L"Mint", {120, 230, 180}},  {L"Peach", {255, 180, 140}},
    {L"Lavender", {190, 160, 255}}, {L"Turquoise", {40, 210, 200}}, {L"Rose", {230, 90, 130}}, {L"Olive", {150, 170, 60}},
    {L"Navy", {60, 96, 200}},       {L"Crimson", {205, 36, 56}},  {L"Ice", {170, 220, 255}},   {L"Bronze", {205, 127, 50}},
};
inline constexpr unsigned accent_count = sizeof(accents) / sizeof(accents[0]);

inline constexpr unsigned tab_count = 7;
inline constexpr const wchar_t *tab_names[tab_count] = {L"Play", L"Online", L"Speedrun", L"Mods", L"Saves", L"Settings", L"Updates"};
inline constexpr unsigned fixed_tabs = (1u << 0u) | (1u << 5u);

struct Theme {
    Rgb background, sidebar, panel, field, raised, hover, border, ink, muted, faint;
    Rgb accent, highlight, glow, heroFrom, heroTo;
    Rgb buttonTop, buttonBottom, buttonHotTop, buttonHotBottom, buttonPressedTop, buttonPressedBottom, buttonShine, buttonInk;
    Rgb bannerTitle, bannerText, success, error, info, ball, ballStars, shadow;
};

struct ColorRole {
    const wchar_t *name, *about;
    Rgb Theme::*member;
};
inline constexpr ColorRole color_roles[] = {
    {L"Background", L"Behind every page", &Theme::background},
    {L"Sidebar", L"The tab column on the left", &Theme::sidebar},
    {L"Panels", L"Boxes that hold each group of settings", &Theme::panel},
    {L"Fields", L"Dropdowns, lists and text boxes", &Theme::field},
    {L"Buttons", L"Ordinary buttons", &Theme::raised},
    {L"Button hover", L"Buttons under the mouse", &Theme::hover},
    {L"Borders", L"Outlines and dividing lines", &Theme::border},
    {L"Text", L"Headings, values and labels", &Theme::ink},
    {L"Secondary text", L"Notes and descriptions", &Theme::muted},
    {L"Faint text", L"Hints, disabled items and the version", &Theme::faint},
    {L"Accent", L"Selected tab, selection marks and focus", &Theme::accent},
    {L"Highlight", L"Gold details and best times", &Theme::highlight},
    {L"Glow", L"Light behind the logo and main buttons", &Theme::glow},
    {L"Main buttons", L"Play, Import and other main actions", &Theme::buttonTop},
    {L"Main button text", L"Text on main buttons", &Theme::buttonInk},
    {L"Banner left", L"Left side of the Play banner", &Theme::heroFrom},
    {L"Banner right", L"Right side of the Play banner", &Theme::heroTo},
    {L"Banner title", L"The Budokai 2 title on Play", &Theme::bannerTitle},
    {L"Banner details", L"Console, region and ID on Play", &Theme::bannerText},
    {L"Success", L"Online, verified and finished states", &Theme::success},
    {L"Errors", L"Problems and warnings", &Theme::error},
    {L"Links", L"Information marks", &Theme::info},
    {L"Dragon Ball", L"The ball in the corner", &Theme::ball},
    {L"Dragon Ball stars", L"The stars on the ball", &Theme::ballStars},
};
inline constexpr unsigned color_role_count = sizeof(color_roles) / sizeof(color_roles[0]);
inline constexpr unsigned main_button_role = 13;
using CustomColors = std::array<int32_t, color_role_count>;
constexpr CustomColors no_custom_colors() {
    CustomColors out{};
    for (auto &value : out)
        value = -1;
    return out;
}

constexpr uint8_t channel(float value) { return uint8_t(std::clamp(value + 0.5f, 0.0f, 255.0f)); }
constexpr Rgb blend(Rgb a, Rgb b, unsigned amount) {
    const auto part = [&](uint8_t x, uint8_t y) { return uint8_t((x * (255u - amount) + y * amount) / 255u); };
    return {part(a.r, b.r), part(a.g, b.g), part(a.b, b.b)};
}
constexpr unsigned luminance(Rgb c) { return (c.r * 299u + c.g * 587u + c.b * 114u) / 1000u; }
constexpr Rgb from_hex(int32_t value) { return {uint8_t(value >> 16 & 255), uint8_t(value >> 8 & 255), uint8_t(value & 255)}; }
constexpr int32_t to_hex(Rgb c) { return int32_t(c.r) << 16 | int32_t(c.g) << 8 | int32_t(c.b); }

constexpr void shade_main_buttons(Theme &out, Rgb base) {
    const Rgb white{255, 255, 255}, black{0, 0, 0};
    out.buttonTop = blend(base, white, 80);
    out.buttonBottom = blend(base, black, 12);
    out.buttonHotTop = blend(base, white, 130);
    out.buttonHotBottom = blend(base, white, 40);
    out.buttonPressedTop = blend(base, black, 25);
    out.buttonPressedBottom = blend(base, black, 55);
    out.buttonShine = blend(out.buttonTop, Rgb{255, 245, 214}, 150);
}

constexpr Theme make_theme(unsigned theme, unsigned accent, const CustomColors &custom = no_custom_colors()) {
    const auto &t = themes[std::min(theme, theme_count - 1u)];
    const auto tone = [&](float level, float grey) {
        const float r = t.r + (1.0f - t.r) * grey, g = t.g + (1.0f - t.g) * grey, b = t.b + (1.0f - t.b) * grey;
        return Rgb{channel(level * r), channel(level * g), channel(level * b)};
    };
    Theme out{};
    const Rgb white{255, 255, 255}, black{0, 0, 0};
    out.accent = accent && accent < accent_count ? accents[accent].color : t.accent;
    if (t.light) {
        out.background = tone(238, 0.0f);
        out.sidebar = tone(226, 0.0f);
        out.panel = tone(250, 0.0f);
        out.field = {255, 255, 255};
        out.raised = tone(232, 0.0f);
        out.hover = tone(216, 0.0f);
        out.border = tone(204, 0.0f);
        out.ink = {26, 28, 36};
        out.muted = {84, 90, 108};
        out.faint = {132, 138, 156};
        out.highlight = blend(out.accent, black, 60);
        out.heroFrom = blend(Rgb{20, 22, 30}, out.accent, 90);
        out.heroTo = {32, 36, 52};
        out.shadow = blend(out.background, white, 200);
    } else {
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
        out.highlight = blend(out.accent, Rgb{255, 236, 170}, 120);
        out.heroFrom = blend(black, out.accent, 64);
        out.heroTo = tone(31, 0.0f);
        out.shadow = {6, 7, 12};
    }
    out.glow = blend(out.accent, black, 20);
    shade_main_buttons(out, out.accent);
    out.buttonInk = luminance(out.accent) > 110u ? blend(out.accent, black, 220) : white;
    out.bannerTitle = {255, 240, 222};
    out.bannerText = {206, 176, 150};
    out.success = t.light ? Rgb{24, 150, 90} : Rgb{98, 214, 152};
    out.error = t.light ? Rgb{200, 50, 44} : Rgb{255, 112, 102};
    out.info = {70, 110, 255};
    out.ball = {255, 145, 26};
    out.ballStars = {220, 40, 28};
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
    for (unsigned role = 0; role < color_role_count; ++role) {
        if (custom[role] < 0)
            continue;
        const Rgb chosen = from_hex(custom[role]);
        if (role == main_button_role)
            shade_main_buttons(out, chosen);
        out.*(color_roles[role].member) = chosen;
    }
    return out;
}

constexpr bool valid_custom_colors(const CustomColors &custom) {
    for (int32_t value : custom)
        if (value < -1 || value > 0xffffff)
            return false;
    return true;
}

inline std::string look_code(unsigned theme, unsigned accent, const CustomColors &custom) {
    std::string code = "OPENKAI2-LOOK 1 " + std::to_string(theme) + ' ' + std::to_string(accent);
    for (int32_t value : custom) {
        if (value < 0) {
            code += " -";
            continue;
        }
        char hex[12];
        std::snprintf(hex, sizeof hex, " %06X", unsigned(value) & 0xffffffu);
        code += hex;
    }
    return code;
}

inline bool parse_look_code(const std::string &text, unsigned &theme, unsigned &accent, CustomColors &custom) {
    std::istringstream in(text);
    std::string magic, version, value;
    unsigned chosenTheme = 0, chosenAccent = 0;
    if (!(in >> magic >> version >> chosenTheme >> chosenAccent) || magic != "OPENKAI2-LOOK" || version != "1" ||
        chosenTheme >= theme_count || chosenAccent >= accent_count)
        return false;
    CustomColors colors = no_custom_colors();
    for (auto &color : colors) {
        if (!(in >> value))
            return false;
        if (value == "-")
            continue;
        if (value.size() != 6 || value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            return false;
        color = int32_t(std::stoul(value, nullptr, 16));
    }
    theme = chosenTheme;
    accent = chosenAccent;
    custom = colors;
    return true;
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
