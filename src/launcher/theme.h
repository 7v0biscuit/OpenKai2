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
    {L"Ember", {20, 22, 29}, {24, 27, 35}, {29, 33, 43}, {255, 136, 28}, {255, 204, 86}, {64, 24, 8}, {24, 26, 36}, {206, 176, 150}, false},
    {L"Super Saiyan Blue", {6, 13, 23}, {8, 17, 30}, {12, 25, 41}, {38, 196, 255}, {150, 232, 255}, {8, 58, 96}, {8, 17, 33}, {150, 198, 228}, false},
    {L"Namek", {6, 16, 13}, {8, 21, 17}, {12, 30, 25}, {80, 212, 138}, {204, 240, 140}, {12, 66, 42}, {8, 21, 18}, {158, 212, 178}, false},
    {L"Frieza", {13, 8, 21}, {17, 10, 28}, {26, 16, 41}, {192, 104, 255}, {255, 172, 240}, {66, 20, 92}, {18, 11, 30}, {204, 170, 230}, false},
    {L"Kaioken", {19, 7, 8}, {25, 9, 11}, {36, 14, 16}, {255, 74, 58}, {255, 192, 120}, {104, 18, 12}, {26, 9, 12}, {236, 172, 160}, false},
    {L"Majin", {20, 8, 16}, {26, 10, 21}, {37, 15, 30}, {255, 108, 188}, {255, 204, 232}, {96, 20, 68}, {26, 10, 22}, {236, 176, 212}, false},
    {L"Saiyan Gold", {17, 13, 7}, {22, 17, 9}, {32, 25, 14}, {255, 194, 44}, {255, 232, 150}, {88, 60, 10}, {23, 18, 10}, {226, 200, 150}, false},
    {L"Graphite", {15, 16, 18}, {19, 20, 23}, {27, 28, 32}, {222, 226, 234}, {255, 214, 120}, {60, 62, 70}, {21, 22, 25}, {190, 193, 202}, false},
    {L"Midnight", {0, 0, 0}, {6, 7, 10}, {12, 13, 19}, {92, 142, 255}, {156, 204, 255}, {16, 32, 78}, {5, 6, 11}, {170, 182, 214}, false},
    {L"Daylight", {241, 239, 235}, {229, 226, 220}, {252, 251, 249}, {226, 100, 16}, {164, 84, 8}, {128, 54, 12}, {40, 33, 29}, {238, 206, 172}, true},
    {L"Capsule Corp", {232, 238, 246}, {219, 228, 240}, {250, 252, 255}, {28, 116, 226}, {18, 78, 168}, {18, 72, 156}, {22, 31, 52}, {190, 214, 246}, true},
    {L"Capsule", {243, 246, 250}, {232, 238, 246}, {255, 255, 255}, {47, 111, 220}, {24, 74, 160}, {14, 34, 74}, {31, 79, 156}, {214, 228, 250}, true, 1},
    {L"Dragon Balls", {20, 16, 25}, {27, 21, 33}, {38, 29, 45}, {240, 163, 90}, {255, 210, 138}, {20, 16, 25}, {58, 42, 60}, {230, 206, 186}, false, 2},
    {L"Dragon Radar", {12, 27, 19}, {15, 33, 23}, {21, 45, 31}, {123, 224, 143}, {255, 210, 90}, {13, 26, 18}, {47, 106, 62}, {176, 220, 190}, false, 3},
    {L"Ki", {26, 18, 14}, {32, 22, 16}, {44, 30, 20}, {255, 201, 74}, {255, 243, 196}, {20, 13, 10}, {58, 36, 16}, {232, 210, 170}, false, 4},
    {L"Scouter", {30, 12, 17}, {38, 15, 21}, {52, 21, 29}, {255, 111, 97}, {255, 179, 168}, {27, 11, 16}, {91, 31, 42}, {236, 190, 184}, false, 5},
};
inline constexpr unsigned theme_count = sizeof(themes) / sizeof(themes[0]);
inline constexpr unsigned banner_picture_count = 5u;
inline constexpr unsigned legacy_themes[] = {0, 1, 2, 3, 4, 7, 6, 2, 3, 8, 5, 9, 7, 1, 4, 5, 2, 3, 10, 9};
constexpr unsigned legacy_theme(unsigned old) { return old < sizeof(legacy_themes) / sizeof(legacy_themes[0]) ? legacy_themes[old] : 0u; }

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
    {L"RGB (cycling)", {255, 64, 64}},
};
inline constexpr unsigned accent_count = sizeof(accents) / sizeof(accents[0]);
inline constexpr unsigned rgb_accent = accent_count - 1u;
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

inline constexpr unsigned tab_count = 7;
inline constexpr const wchar_t *tab_names[tab_count] = {L"Play", L"Online", L"Speedrun", L"Mods", L"Saves", L"Settings", L"Updates"};
inline constexpr unsigned fixed_tabs = (1u << 0u) | (1u << 5u);

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
    if (index == 0u) {
        out.field = {34, 38, 50};
        out.raised = {40, 45, 58};
        out.hover = {50, 56, 72};
        out.border = {44, 49, 62};
        out.muted = {163, 169, 184};
        out.faint = {115, 122, 140};
        out.bannerTitle = {255, 240, 222};
        out.info = {98, 128, 220};
        out.shadow = {12, 13, 18};
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
    std::string code = "OPENKAI2-LOOK 2 " + std::to_string(theme) + ' ' + std::to_string(accent);
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
    if (!(in >> magic >> version >> chosenTheme >> chosenAccent) || magic != "OPENKAI2-LOOK" || (version != "1" && version != "2"))
        return false;
    if (version == "1") {
        if (chosenTheme >= sizeof(legacy_themes) / sizeof(legacy_themes[0]))
            return false;
        chosenTheme = legacy_theme(chosenTheme);
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
