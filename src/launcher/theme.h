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
    Rgb background, sidebar, panel, accent, highlight, heroFrom, heroTo, bannerText;
    bool light;
    unsigned banner = 0;
};
inline constexpr ThemeChoice themes[] = {
    {L"Dragon Balls", {20, 16, 25}, {27, 21, 33}, {38, 29, 45}, {240, 163, 90}, {255, 210, 138}, {20, 16, 25}, {58, 42, 60}, {230, 206, 186}, false, 2},
    {L"Capsule", {243, 246, 250}, {232, 238, 246}, {255, 255, 255}, {47, 111, 220}, {24, 74, 160}, {14, 34, 74}, {31, 79, 156}, {214, 228, 250}, true, 1},
    {L"Dragon Radar", {12, 27, 19}, {15, 33, 23}, {21, 45, 31}, {123, 224, 143}, {255, 210, 90}, {13, 26, 18}, {47, 106, 62}, {176, 220, 190}, false, 3},
    {L"Scouter", {30, 12, 17}, {38, 15, 21}, {52, 21, 29}, {255, 111, 97}, {255, 179, 168}, {27, 11, 16}, {91, 31, 42}, {236, 190, 184}, false, 4},
    {L"Budokai Black", {10, 10, 11}, {14, 14, 16}, {21, 21, 24}, {236, 234, 228}, {224, 64, 46}, {12, 12, 14}, {30, 30, 34}, {205, 203, 198}, false, 5},
    {L"Budokai Black (board)", {10, 10, 11}, {14, 14, 16}, {21, 21, 24}, {224, 64, 46}, {236, 234, 228}, {12, 12, 14}, {30, 30, 34}, {205, 203, 198}, false, 6},
    {L"Majin Buu", {34, 16, 29}, {42, 20, 36}, {58, 28, 50}, {255, 140, 198}, {255, 210, 90}, {26, 12, 22}, {92, 39, 73}, {245, 200, 225}, false, 7},
    {L"Babidi", {22, 11, 34}, {28, 14, 42}, {40, 20, 60}, {140, 230, 120}, {255, 200, 90}, {18, 9, 28}, {58, 28, 86}, {214, 196, 255}, false, 8},
    {L"Majin Vegeta", {20, 10, 7}, {26, 13, 9}, {38, 19, 13}, {255, 196, 58}, {214, 58, 42}, {16, 8, 5}, {58, 14, 14}, {240, 200, 160}, false, 9},
    {L"Saiyan Pod", {18, 14, 34}, {23, 18, 42}, {33, 27, 58}, {180, 150, 255}, {255, 140, 140}, {14, 11, 26}, {52, 40, 92}, {205, 195, 240}, false, 10},
    {L"Spirit Bomb", {11, 19, 48}, {14, 24, 58}, {21, 34, 76}, {124, 196, 255}, {220, 240, 255}, {9, 14, 36}, {30, 60, 110}, {180, 210, 245}, false, 11},
    {L"Flying Nimbus", {34, 20, 44}, {42, 25, 52}, {58, 36, 66}, {255, 183, 77}, {255, 230, 170}, {26, 16, 32}, {120, 62, 58}, {245, 210, 180}, false, 12},
    {L"Potara", {11, 34, 36}, {14, 41, 43}, {20, 55, 57}, {77, 208, 200}, {255, 214, 90}, {9, 26, 28}, {28, 84, 86}, {175, 225, 220}, false, 13},
};
inline constexpr unsigned theme_count = sizeof(themes) / sizeof(themes[0]);
inline constexpr unsigned banner_picture_count = 13u;
inline constexpr unsigned legacy_themes[] = {0, 1, 2, 3, 4, 7, 6, 2, 3, 8, 5, 9, 7, 1, 4, 5, 2, 3, 10, 9};
constexpr unsigned legacy_theme(unsigned old) { return old < sizeof(legacy_themes) / sizeof(legacy_themes[0]) ? legacy_themes[old] : 0u; }
inline constexpr unsigned retired_themes[] = {0, 1, 2, 0, 4, 4, 3, 0, 0, 1, 1, 1, 0, 2, 3, 4};
constexpr unsigned retired_theme(unsigned old) { return old < sizeof(retired_themes) / sizeof(retired_themes[0]) ? retired_themes[old] : 0u; }
inline constexpr unsigned picture_themes[] = {0, 1, 2, 8, 3};
constexpr unsigned picture_theme(unsigned old) { return old < sizeof(picture_themes) / sizeof(picture_themes[0]) ? picture_themes[old] : 0u; }

struct AccentChoice {
    const wchar_t *name;
    Rgb color;
};
inline constexpr AccentChoice accents[] = {
    {L"Theme color", {0, 0, 0}}, {L"Orange", {255, 136, 28}}, {L"Gold", {246, 196, 64}}, {L"Red", {236, 72, 84}},
    {L"Pink", {244, 110, 190}},  {L"Purple", {168, 112, 255}}, {L"Blue", {64, 156, 255}}, {L"Cyan", {52, 206, 220}},
    {L"Green", {86, 200, 120}},  {L"White", {226, 230, 238}}, {L"RGB (cycling)", {255, 64, 64}},
};
inline constexpr unsigned accent_count = sizeof(accents) / sizeof(accents[0]);
inline constexpr unsigned rgb_accent = accent_count - 1u;
inline constexpr unsigned retired_accents[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 7, 8, 4, 3, 6, 5, 9, 2, 8, 1, 5, 7, 4, 8, 6, 3, 7, 1, 10};
constexpr unsigned retired_accent(unsigned old) { return old < sizeof(retired_accents) / sizeof(retired_accents[0]) ? retired_accents[old] : 0u; }
inline constexpr unsigned rgb_cycle_ms = 60000u, rgb_frame_ms = 16u;

constexpr Rgb cycling_accent(unsigned long long milliseconds) {
    const unsigned position = unsigned(milliseconds % rgb_cycle_ms) * 1536u / rgb_cycle_ms;
    const unsigned rise = position % 256u, fall = 255u - rise, low = 56u;
    const auto level = [&](unsigned value) { return uint8_t(low + value * (255u - low) / 255u); };
    switch (position / 256u) {
    case 0: return {level(255), level(rise), level(0)};
    case 1: return {level(fall), level(255), level(0)};
    case 2: return {level(0), level(255), level(rise)};
    case 3: return {level(0), level(fall), level(255)};
    case 4: return {level(rise), level(0), level(255)};
    default: return {level(255), level(0), level(fall)};
    }
}

inline constexpr unsigned tab_count = 8;
inline constexpr const wchar_t *tab_names[tab_count] = {L"Play", L"Online", L"Speedrun", L"Mods", L"Saves", L"Settings", L"Updates", L"Controls"};
inline constexpr unsigned fixed_tabs = (1u << 0u) | (1u << 5u);
inline constexpr std::array<unsigned, tab_count> default_tab_order{0, 1, 2, 3, 4, 7, 5, 6};

struct Theme {
    Rgb background, sidebar, panel, field, raised, hover, border, ink, muted, faint;
    Rgb accent, highlight, glow, heroFrom, heroTo;
    Rgb buttonTop, buttonBottom, buttonHotTop, buttonHotBottom, buttonPressedTop, buttonPressedBottom, buttonShine, buttonInk;
    Rgb bannerTitle, bannerText, success, error, info, ball, ballStars, shadow, bannerLabel;
    unsigned banner = 0;
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
inline constexpr unsigned main_button_role = 13, highlight_role = 11;
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

constexpr Theme make_theme(unsigned theme, unsigned accent, const CustomColors &custom = no_custom_colors(),
                           unsigned long long milliseconds = 0) {
    const unsigned index = std::min(theme, theme_count - 1u);
    const auto &t = themes[index];
    const bool own = !accent || accent >= accent_count;
    Theme out{};
    const Rgb white{255, 255, 255}, black{0, 0, 0};
    out.accent = own ? t.accent : accent == rgb_accent ? cycling_accent(milliseconds) : accents[accent].color;
    out.background = t.background;
    out.sidebar = t.sidebar;
    out.panel = t.panel;
    if (t.light) {
        out.field = white;
        out.raised = blend(t.sidebar, black, 10);
        out.hover = blend(blend(t.sidebar, black, 22), out.accent, 34);
        out.border = blend(blend(t.sidebar, black, 34), out.accent, 18);
        out.ink = {24, 26, 34};
        out.muted = blend(Rgb{82, 88, 106}, t.accent, 24);
        out.faint = blend(Rgb{134, 140, 158}, t.accent, 20);
        out.highlight = own ? t.highlight : blend(out.accent, black, 70);
        out.success = {22, 146, 88};
        out.error = {198, 46, 40};
        out.info = blend(Rgb{36, 92, 220}, t.accent, 40);
        out.shadow = blend(t.background, white, 200);
    } else {
        out.field = blend(blend(t.panel, white, 12), out.accent, 8);
        out.raised = blend(blend(t.panel, white, 26), out.accent, 14);
        out.hover = blend(blend(t.panel, white, 44), out.accent, 28);
        out.border = blend(blend(t.panel, white, 32), out.accent, 30);
        out.ink = {242, 238, 230};
        out.muted = blend(Rgb{164, 168, 184}, t.accent, 28);
        out.faint = blend(Rgb{98, 104, 124}, t.accent, 30);
        out.highlight = own ? t.highlight : blend(out.accent, Rgb{255, 236, 170}, 120);
        out.success = {98, 214, 152};
        out.error = {255, 112, 102};
        out.info = blend(Rgb{90, 130, 255}, t.accent, 50);
        out.shadow = blend(t.background, black, 160);
    }
    out.heroFrom = own ? t.heroFrom : blend(t.heroFrom, out.accent, 90);
    out.heroTo = t.heroTo;
    out.glow = blend(out.accent, black, 20);
    shade_main_buttons(out, out.accent);
    out.buttonInk = luminance(out.accent) > 110u ? blend(out.accent, black, 220) : white;
    out.bannerTitle = blend(Rgb{255, 246, 236}, t.bannerText, 40);
    out.bannerText = t.bannerText;
    out.banner = t.banner;
    if (t.banner && !t.light)
        out.info = blend(out.info, out.accent, 150);
    out.ball = {255, 145, 26};
    out.ballStars = {220, 40, 28};
    for (unsigned role = 0; role < color_role_count; ++role) {
        if (custom[role] < 0)
            continue;
        const Rgb chosen = from_hex(custom[role]);
        if (role == main_button_role)
            shade_main_buttons(out, chosen);
        out.*(color_roles[role].member) = chosen;
    }
    out.bannerLabel = t.light && custom[highlight_role] < 0 ? blend(out.accent, white, 150) : out.highlight;
    return out;
}

constexpr bool valid_custom_colors(const CustomColors &custom) {
    for (int32_t value : custom)
        if (value < -1 || value > 0xffffff)
            return false;
    return true;
}

inline std::string look_code(unsigned theme, unsigned accent, const CustomColors &custom) {
    std::string code = "OPENKAI2-LOOK 5 " + std::to_string(theme) + ' ' + std::to_string(accent);
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
    if (!(in >> magic >> version >> chosenTheme >> chosenAccent) || magic != "OPENKAI2-LOOK" || (version != "1" && version != "2" && version != "3" && version != "4" && version != "5"))
        return false;
    if (version == "1") {
        if (chosenTheme >= sizeof(legacy_themes) / sizeof(legacy_themes[0]))
            return false;
        chosenTheme = retired_theme(legacy_theme(chosenTheme));
    } else if (version == "2") {
        if (chosenTheme >= sizeof(retired_themes) / sizeof(retired_themes[0]))
            return false;
        chosenTheme = retired_theme(chosenTheme);
    }
    if (version != "5") {
        if (chosenTheme >= sizeof(picture_themes) / sizeof(picture_themes[0]))
            return false;
        chosenTheme = picture_theme(chosenTheme);
    }
    if (version != "4" && version != "5") {
        if (chosenAccent >= sizeof(retired_accents) / sizeof(retired_accents[0]))
            return false;
        chosenAccent = retired_accent(chosenAccent);
    }
    if (chosenTheme >= theme_count || chosenAccent >= accent_count)
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
