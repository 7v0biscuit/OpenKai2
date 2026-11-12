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
    {L"Dragon Balls", {16, 20, 27}, {21, 27, 36}, {28, 37, 49}, {245, 161, 82}, {255, 211, 145}, {21, 25, 33}, {48, 37, 28}, {221, 195, 164}, false, 2},
    {L"Capsule", {237, 242, 248}, {247, 250, 253}, {255, 255, 255}, {36, 100, 202}, {22, 63, 122}, {17, 43, 75}, {30, 75, 120}, {211, 229, 250}, true, 1},
    {L"Dragon Radar", {12, 23, 20}, {16, 33, 27}, {23, 43, 35}, {117, 217, 166}, {219, 196, 118}, {14, 32, 25}, {37, 77, 55}, {194, 223, 205}, false, 3},
    {L"Scouter", {27, 16, 22}, {36, 21, 30}, {48, 29, 41}, {245, 112, 121}, {255, 208, 174}, {28, 17, 25}, {76, 38, 57}, {238, 203, 208}, false, 4},
    {L"Budokai Black", {14, 16, 19}, {20, 23, 28}, {27, 32, 39}, {230, 231, 233}, {242, 155, 75}, {21, 24, 29}, {40, 46, 54}, {200, 205, 213}, false, 5},
    {L"Budokai Black (board)", {16, 16, 18}, {23, 23, 26}, {32, 32, 36}, {242, 103, 82}, {245, 215, 178}, {21, 21, 25}, {51, 35, 33}, {215, 201, 191}, false, 6},
    {L"Majin Buu", {29, 21, 32}, {40, 27, 42}, {53, 36, 58}, {247, 159, 203}, {245, 204, 117}, {33, 23, 37}, {79, 44, 70}, {233, 198, 220}, false, 7},
    {L"Babidi", {23, 20, 35}, {32, 27, 48}, {44, 37, 64}, {179, 217, 122}, {240, 198, 119}, {27, 23, 48}, {53, 40, 73}, {208, 198, 230}, false, 8},
    {L"Majin Vegeta", {24, 22, 18}, {34, 32, 26}, {48, 44, 34}, {230, 187, 92}, {231, 127, 89}, {33, 27, 17}, {76, 52, 36}, {227, 209, 178}, false, 9},
    {L"Saiyan Pod", {20, 23, 34}, {27, 33, 48}, {37, 45, 64}, {176, 154, 222}, {227, 188, 183}, {24, 29, 42}, {48, 57, 76}, {201, 210, 226}, false, 10},
    {L"Spirit Bomb", {16, 28, 38}, {22, 39, 53}, {31, 52, 70}, {139, 203, 229}, {208, 236, 248}, {19, 35, 48}, {40, 81, 100}, {195, 223, 237}, false, 11},
    {L"Flying Nimbus", {33, 27, 33}, {44, 37, 44}, {58, 48, 56}, {242, 185, 113}, {244, 219, 176}, {36, 28, 37}, {84, 59, 66}, {231, 209, 189}, false, 12},
    {L"Potara", {16, 30, 33}, {23, 41, 45}, {33, 56, 60}, {119, 205, 196}, {225, 196, 122}, {18, 39, 42}, {43, 74, 73}, {190, 222, 216}, false, 13},
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
    unsigned banner = 0;
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
    {L"Banner left", L"Left side of the Play banner", &Theme::heroFrom},
    {L"Banner right", L"Right side of the Play banner", &Theme::heroTo},
    {L"Banner title", L"The Budokai 2 title on Play", &Theme::bannerTitle},
    {L"Banner details", L"Disc status and details on Play", &Theme::bannerText},
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
        out.field = {243, 247, 252};
        out.raised = {232, 238, 246};
        out.hover = blend(out.raised, out.accent, 18);
        out.border = {191, 202, 218};
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
