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
};
inline constexpr ThemeChoice themes[] = {
    {L"Budokai Orange", {17,15,14}, {23,20,18}, {31,26,23}, {247,159,76}, {255,207,148}, {29,22,17}, {65,38,22}, {225,200,179}, false},
    {L"Capsule White", {237,242,248}, {247,250,253}, {255,255,255}, {36,100,202}, {22,63,122}, {243,248,255}, {216,232,253}, {57,80,113}, true},
    {L"Radar Green", {10,20,15}, {14,28,21}, {20,38,28}, {105,217,143}, {188,237,202}, {12,32,20}, {25,62,37}, {185,220,194}, false},
    {L"Scouter Red", {23,12,16}, {32,17,22}, {43,24,30}, {244,112,131}, {255,187,192}, {32,14,22}, {75,28,40}, {230,192,200}, false},
    {L"Obsidian Black", {9,10,12}, {15,16,19}, {23,24,28}, {231,233,237}, {193,199,211}, {15,16,18}, {34,36,43}, {204,209,219}, false},
    {L"Graphite", {23,27,33}, {31,36,44}, {39,45,55}, {161,181,210}, {219,228,242}, {30,38,50}, {46,60,79}, {201,216,236}, false},
    {L"Majin Pink", {24,14,23}, {33,20,32}, {44,28,42}, {239,160,210}, {251,210,236}, {33,17,29}, {71,34,62}, {232,195,223}, false},
    {L"Royal Purple", {15,12,25}, {23,18,37}, {31,25,48}, {184,157,245}, {224,211,255}, {25,18,43}, {50,35,79}, {209,197,237}, false},
    {L"Saiyan Gold", {25,20,11}, {34,28,17}, {44,36,21}, {237,198,109}, {253,224,164}, {38,28,13}, {78,56,25}, {228,207,166}, false},
    {L"Midnight Blue", {10,16,29}, {15,23,40}, {22,32,52}, {111,153,238}, {185,209,255}, {14,25,49}, {27,47,85}, {189,206,237}, false},
    {L"Ice Blue", {12,25,31}, {17,34,43}, {23,44,54}, {151,219,241}, {211,244,255}, {16,37,48}, {30,69,85}, {193,228,238}, false},
    {L"Nimbus Yellow", {250,246,231}, {255,252,241}, {255,253,247}, {135,91,0}, {117,75,0}, {255,250,226}, {248,229,167}, {88,70,32}, true},
    {L"Emerald", {9,22,23}, {13,31,33}, {19,42,44}, {65,212,191}, {160,237,220}, {12,31,34}, {22,64,65}, {177,223,218}, false},
};
inline constexpr unsigned theme_count = sizeof(themes) / sizeof(themes[0]);
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
    {L"Theme color", {0, 0, 0}}, {L"Orange", {245, 155, 76}}, {L"Gold", {239, 211, 112}}, {L"Red", {244, 120, 133}},
    {L"Pink", {231, 151, 191}}, {L"Purple", {169, 141, 226}}, {L"Blue", {94, 148, 240}}, {L"Cyan", {85, 209, 211}},
    {L"Green", {126, 203, 151}}, {L"White", {230, 235, 242}}, {L"RGB (cycling)", {255, 64, 64}},
};
inline constexpr unsigned accent_count = sizeof(accents) / sizeof(accents[0]);
inline constexpr unsigned rgb_accent = accent_count - 1u;
inline constexpr unsigned retired_accents[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 7, 8, 4, 3, 6, 5, 9, 2, 8, 1, 5, 7, 4, 8, 6, 3, 7, 1, 10};
constexpr unsigned retired_accent(unsigned old) { return old < sizeof(retired_accents) / sizeof(retired_accents[0]) ? retired_accents[old] : 0u; }
inline constexpr unsigned rgb_cycle_ms = 60000u, rgb_frame_ms = 16u;

constexpr Rgb cycling_accent(unsigned long long milliseconds) {
    const unsigned position = unsigned(milliseconds % rgb_cycle_ms) * 1536u / rgb_cycle_ms;
    const unsigned rise = position % 256u, fall = 255u - rise, low = 96u;
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
// Page identifiers remain stable for saved preferences and keyboard routes.
// Saves and Controls are tools on Play, rather than independent dock tabs.
inline constexpr unsigned dock_tab_count = 6;
constexpr bool dock_tab(unsigned page) { return page < tab_count && page != 4u && page != 7u; }
inline constexpr std::array<unsigned, tab_count> default_tab_order{0, 1, 2, 3, 5, 6, 4, 7};
constexpr std::array<unsigned, tab_count> normalize_dock_order(const std::array<unsigned, tab_count>& order) {
    std::array<unsigned, tab_count> result{};
    unsigned index = 0;
    for (unsigned page : order) if (dock_tab(page)) result[index++] = page;
    for (unsigned page : order) if (!dock_tab(page)) result[index++] = page;
    return result;
}

struct Theme {
    Rgb background, sidebar, panel, field, raised, hover, border, ink, muted, faint;
    Rgb accent, highlight, glow, heroFrom, heroTo;
    Rgb buttonTop, buttonBottom, buttonHotTop, buttonHotBottom, buttonPressedTop, buttonPressedBottom, buttonShine, buttonInk;
    Rgb bannerTitle, bannerText, success, error, info, ball, ballStars, shadow, bannerLabel;
};

struct ColorRole {
    const wchar_t *name, *about;
    Rgb Theme::*member;
};
inline constexpr ColorRole color_roles[] = {
    {L"Background", L"Behind every page", &Theme::background},
    {L"Navigation", L"Header, dock and navigation buttons", &Theme::sidebar},
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
    {L"Game panel left", L"Left side of the game panel", &Theme::heroFrom},
    {L"Game panel right", L"Right side of the game panel", &Theme::heroTo},
    {L"Game title", L"The Budokai 2 title on Play", &Theme::bannerTitle},
    {L"Game details", L"Disc status and details on Play", &Theme::bannerText},
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
    out.buttonTop = base;
    out.buttonInk = luminance(base) > 110u ? blend(base, black, 220) : white;
    out.buttonBottom = blend(base, black, 12);
    out.buttonHotTop = blend(base, white, 28);
    out.buttonHotBottom = blend(base, white, 16);
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
        const unsigned brightness = luminance(out.accent);
        if (brightness > 100u)
            out.accent = blend(out.accent, black, (brightness - 100u) * 255u / brightness);
        out.field = blend(t.panel, t.background, 96);
        out.raised = blend(t.sidebar, out.accent, 14);
        out.hover = blend(out.raised, out.accent, 18);
        out.border = blend(t.panel, out.accent, 80);
        out.ink = {24, 35, 51};
        out.muted = {77, 94, 117};
        out.faint = {101, 117, 139};
        out.highlight = own ? t.highlight : blend(out.accent, black, 24);
        out.success = {27, 120, 81};
        out.error = {180, 51, 58};
        out.info = out.accent;
        out.shadow = blend(t.background, white, 200);
    } else {
        out.field = blend(out.panel, black, 22);
        out.raised = blend(out.panel, white, 17);
        out.hover = blend(blend(out.panel, white, 24), out.accent, 12);
        out.border = blend(out.panel, white, 34);
        out.ink = {239, 242, 247};
        out.muted = {177, 187, 201};
        out.faint = {148, 160, 178};
        out.highlight = own ? t.highlight : blend(out.accent, white, 54);
        out.success = {120, 214, 172};
        out.error = {247, 143, 140};
        out.info = blend(Rgb{135, 183, 242}, out.accent, 28);
        out.shadow = blend(t.background, black, 160);
    }
    out.heroFrom = own ? t.heroFrom : blend(t.heroFrom, out.accent, 90);
    out.heroTo = t.heroTo;
    out.glow = blend(out.accent, black, 20);
    shade_main_buttons(out, out.accent);
    out.bannerTitle = out.ink;
    out.bannerText = t.bannerText;
    out.info = blend(out.info, out.accent, t.light ? 0 : 100);
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
