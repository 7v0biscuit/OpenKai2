// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "dev_identity.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace openkai2::name_style {

struct Rgb {
    int r = 0, g = 0, b = 0;
};

inline Rgb rgb(int r, int g, int b) { return {r, g, b}; }

inline Rgb blend(Rgb a, Rgb b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {int(std::lround(a.r + (b.r - a.r) * t)), int(std::lround(a.g + (b.g - a.g) * t)),
            int(std::lround(a.b + (b.b - a.b) * t))};
}

inline Rgb blend3(Rgb a, Rgb b, Rgb c, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t < 0.5 ? blend(a, b, t * 2.0) : blend(b, c, t * 2.0 - 1.0);
}

inline double smooth(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double wave(double phase) { return 0.5 + 0.5 * std::sin(phase); }

inline Rgb hue(double degrees, double saturation, double value = 1.0) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0)
        degrees += 360.0;
    const double c = value * saturation, x = c * (1 - std::fabs(std::fmod(degrees / 60.0, 2.0) - 1)), m = value - c;
    double r = 0, g = 0, b = 0;
    if (degrees < 60) { r = c; g = x; }
    else if (degrees < 120) { r = x; g = c; }
    else if (degrees < 180) { g = c; b = x; }
    else if (degrees < 240) { g = x; b = c; }
    else if (degrees < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return {int((r + m) * 255), int((g + m) * 255), int((b + m) * 255)};
}

inline double letter_seed(size_t index) {
    const double v = std::sin(double(index) * 12.9898 + 4.1414) * 43758.5453;
    return v - std::floor(v);
}

inline double sweep(double t, size_t index, size_t count, double speed, double width) {
    const double span = double(count) + width * 4.0;
    const double band = std::fmod(t * speed, 1.0) * span - width * 2.0;
    const double d = (double(index) - band) / width;
    return std::exp(-d * d);
}

inline Rgb effect_color(unsigned effect, size_t index, size_t count, double ms, Rgb fallback) {
    const double t = ms / 1000.0, i = double(index);
    switch (effect) {
    case 1: return hue(t * 64.0 - i * 16.0, 0.55);
    case 2: return blend3(rgb(255, 150, 48), rgb(255, 226, 150), rgb(72, 170, 255), wave(t * 2.0 - i * 0.42));
    case 3: {
        const double band = std::fmod(t * 0.55, 1.0) * (double(count) + 8.0) - 4.0;
        const int k = std::clamp(int(std::exp(-(i - band) * (i - band) / 3.0) * 255.0), 0, 255);
        const auto channel = [k](int x, int y) { return x + (y - x) * k / 255; };
        return {channel(255, 255), channel(190, 252), channel(40, 228)};
    }
    case 4: return blend(rgb(255, 150, 36), rgb(255, 244, 196), smooth(wave(t * 3.0 - i * 0.45)));
    case 5: return blend3(rgb(150, 160, 255), rgb(206, 220, 255), rgb(250, 252, 255), sweep(t, index, count, 0.38, 2.6));
    case 6: return blend(rgb(132, 72, 255), rgb(232, 216, 255), smooth(wave(t * 2.4 - i * 0.38)));
    case 7: {
        const double beat = std::fmod(t * 1.2, 1.0);
        const double pulse = std::exp(-std::pow((beat - 0.12) * 14.0, 2.0)) + 0.7 * std::exp(-std::pow((beat - 0.32) * 14.0, 2.0));
        return blend(rgb(255, 112, 182), rgb(255, 232, 246), std::clamp(pulse, 0.0, 1.0) * (0.75 + 0.25 * wave(i * 0.6)));
    }
    case 8: return blend(hue(282.0 + 22.0 * std::sin(t * 1.8 - i * 0.45), 0.62), rgb(255, 220, 255), 0.25 * wave(t * 3.1 + i * 0.7));
    case 9: return blend3(rgb(214, 28, 40), rgb(255, 88, 76), rgb(255, 214, 196), smooth(wave(t * 2.6 - i * 0.5)));
    case 10: return blend3(rgb(176, 14, 28), rgb(255, 60, 60), rgb(255, 244, 236), sweep(t, index, count, 0.9, 1.6) * 0.7 + 0.3 * wave(t * 5.0 - i * 0.6));
    case 11: return blend(rgb(244, 240, 236), rgb(232, 40, 52), smooth(wave(t * 2.0 - i * 0.9)));
    case 12: {
        const double flame = 0.5 + 0.25 * std::sin(t * 6.1 + i * 1.3) + 0.25 * std::sin(t * 9.7 - i * 0.7);
        return blend3(rgb(232, 56, 20), rgb(255, 148, 36), rgb(255, 238, 170), smooth(flame));
    }
    case 13: return blend3(rgb(52, 120, 255), rgb(118, 196, 255), rgb(255, 255, 255), sweep(t, index, count, 0.6, 2.0) * 0.85 + 0.15 * wave(t * 4.0));
    case 14: return blend3(rgb(64, 168, 84), rgb(132, 222, 120), rgb(220, 255, 196), smooth(wave(t * 1.9 - i * 0.48)));
    case 15: {
        const double spark = std::pow(wave(t * 7.0 + letter_seed(index) * 6.283), 18.0);
        return blend(blend(rgb(255, 196, 30), rgb(255, 246, 190), smooth(wave(t * 2.4 - i * 0.5))), rgb(176, 226, 255), spark);
    }
    case 16: {
        const double glint = std::pow(wave(t * 2.2 - i * 0.9), 14.0);
        return blend(blend(rgb(255, 128, 20), rgb(255, 210, 90), smooth(wave(t * 1.6 - i * 0.4))), rgb(255, 56, 48), glint * 0.85);
    }
    case 17: {
        const double twinkle = std::pow(wave(t * 3.0 + letter_seed(index) * 6.283), 16.0);
        return blend(hue(212.0 + 22.0 * std::sin(t * 0.9 - i * 0.32), 0.6), rgb(255, 255, 255), twinkle);
    }
    case 18: {
        const double sparkle = std::pow(wave(t * 2.6 + letter_seed(index) * 6.283), 10.0);
        return blend(blend(rgb(128, 64, 240), rgb(178, 120, 255), wave(t * 1.4 - i * 0.3)), rgb(255, 150, 222), sparkle);
    }
    case 19: return blend3(rgb(150, 76, 255), rgb(236, 120, 220), rgb(255, 176, 214), smooth(wave(t * 2.2 - i * 0.4)));
    case 20: {
        const double shift = smooth(wave(t * 1.1 - i * 0.22));
        const double blink = std::exp(-std::pow(std::fmod(t * 0.45 + i * 0.03, 1.0) * 30.0 - 1.5, 2.0));
        return blend(blend(rgb(40, 226, 255), rgb(250, 90, 226), shift), rgb(40, 36, 64), blink * 0.55);
    }
    case 21: return blend(rgb(255, 206, 72), rgb(226, 40, 48), smooth(wave(t * 2.6 - i * 0.85)));
    case 22: return blend3(rgb(150, 18, 32), rgb(232, 120, 96), rgb(255, 242, 224), sweep(t, index, count, 0.32, 2.8));
    case 23: return blend3(rgb(230, 40, 28), rgb(255, 140, 30), rgb(255, 222, 96), smooth(wave(t * 3.2 - i * 0.55)));
    case 24: return blend3(rgb(140, 24, 10), rgb(232, 90, 24), rgb(255, 170, 60), smooth(wave(t * 1.1 - i * 0.42)));
    case 25: {
        const double centre = (double(count) - 1.0) / 2.0, ring = std::fmod(t * 0.8, 1.0) * (centre + 4.0);
        const double d = std::fabs(i - centre) - ring;
        return blend(rgb(70, 118, 236), rgb(255, 255, 255), std::exp(-d * d / 1.6));
    }
    case 26: return blend3(rgb(64, 196, 92), rgb(120, 150, 170), rgb(156, 80, 220), smooth(wave(t * 1.7 - i * 0.6)));
    case 27: return blend(rgb(255, 170, 20), rgb(255, 250, 222), smooth(wave(t * 5.0 - i * 0.6)));
    case 28: return blend(hue(268.0 + 26.0 * std::sin(t * 4.0 - i * 0.7), 0.62), rgb(236, 220, 255), 0.2 * wave(t * 4.0 - i * 0.7));
    case 29: return blend(rgb(236, 36, 40), rgb(255, 244, 240), std::fabs(std::sin(t * 3.4 - i * 0.45)) * 0.7);
    case 30: return blend3(rgb(186, 96, 52), rgb(255, 150, 196), rgb(255, 222, 170), smooth(wave(t * 4.6 + i * 1.9)));
    default: return fallback;
    }
}

inline double effect_lift(unsigned effect, size_t index, double ms) {
    const double t = ms / 1000.0, i = double(index);
    switch (effect) {
    case 27: return std::sin(t * 5.0 - i * 0.6) * 1.5;
    case 28: return std::sin(t * 4.0 - i * 0.7) * 2.5;
    case 29: return -std::fabs(std::sin(t * 3.4 - i * 0.45)) * 3.0;
    case 30: return std::sin(t * 7.0 + i * 2.3) * 1.2 + std::sin(t * 11.0 - i * 1.1) * 0.5;
    default: return 0.0;
    }
}

inline bool effect_moves(unsigned effect) { return effect >= 27 && effect <= 30; }

struct TagTheme {
    Rgb top, bottom, edge, glow, label;
    const wchar_t *name;
};

inline TagTheme tag_theme(identity::Tag tag) {
    switch (tag) {
    case identity::Tag::developer:
        return {rgb(64, 30, 8), rgb(26, 12, 6), rgb(255, 150, 40), rgb(255, 196, 64), rgb(255, 140, 40), L"SAIYAN ELITE THEME"};
    case identity::Tag::twitch:
        return {rgb(44, 22, 78), rgb(18, 10, 36), rgb(170, 116, 255), rgb(196, 160, 255), rgb(178, 128, 255), L"TWITCH THEME"};
    case identity::Tag::youtube:
        return {rgb(70, 14, 20), rgb(28, 8, 10), rgb(255, 72, 80), rgb(255, 150, 140), rgb(255, 86, 86), L"YOUTUBE THEME"};
    case identity::Tag::tob:
        return {rgb(10, 56, 56), rgb(6, 22, 26), rgb(60, 214, 186), rgb(150, 255, 226), rgb(76, 226, 196), L"TOB THEME"};
    }
    return {rgb(64, 30, 8), rgb(26, 12, 6), rgb(255, 150, 40), rgb(255, 196, 64), rgb(255, 140, 40), L""};
}

inline bool tag_animates(identity::Tag tag) { return tag != identity::Tag::developer; }

inline Rgb tag_color(identity::Tag tag, size_t index, size_t count, double ms) {
    const double t = ms / 1000.0, i = double(index);
    switch (tag) {
    case identity::Tag::developer: return rgb(255, 140, 40);
    case identity::Tag::twitch: return blend3(rgb(145, 70, 255), rgb(190, 140, 255), rgb(236, 222, 255), sweep(t, index, count, 0.4, 1.4) * 0.8 + 0.2 * wave(t * 2.0 - i * 0.6));
    case identity::Tag::youtube: return blend3(rgb(240, 32, 44), rgb(255, 92, 92), rgb(255, 226, 222), sweep(t, index, count, 0.4, 1.4) * 0.8 + 0.2 * wave(t * 2.0 - i * 0.6));
    case identity::Tag::tob: return blend3(rgb(28, 190, 166), rgb(96, 236, 204), rgb(224, 255, 244), sweep(t, index, count, 0.4, 1.4) * 0.8 + 0.2 * wave(t * 2.0 - i * 0.6));
    }
    return rgb(255, 140, 40);
}

inline Rgb plain_tag_color(identity::Tag tag) {
    switch (tag) {
    case identity::Tag::developer: return rgb(255, 140, 40);
    case identity::Tag::twitch: return rgb(178, 128, 255);
    case identity::Tag::youtube: return rgb(255, 86, 86);
    case identity::Tag::tob: return rgb(76, 226, 196);
    }
    return rgb(255, 140, 40);
}

struct NameColor {
    const wchar_t *name;
    Rgb base, light;
};

inline constexpr NameColor name_colors[] = {
    {L"Super Saiyan gold", {255, 204, 64}, {255, 204, 64}},
    {L"Kamehameha blue", {84, 170, 255}, {184, 228, 255}},
    {L"Namekian green", {96, 214, 120}, {196, 250, 176}},
    {L"Frieza purple", {178, 120, 255}, {232, 196, 255}},
    {L"Kaio-ken red", {255, 82, 82}, {255, 184, 160}},
    {L"Goku gi orange", {255, 140, 50}, {255, 214, 140}},
    {L"Majin pink", {255, 120, 196}, {255, 206, 236}},
    {L"Kami white", {226, 236, 255}, {255, 255, 255}},
    {L"Vegeta blue", {96, 124, 255}, {176, 200, 255}},
    {L"Senzu green", {170, 220, 72}, {226, 252, 170}},
    {L"Android teal", {58, 210, 200}, {176, 250, 238}},
    {L"Ultra Instinct silver", {176, 192, 222}, {244, 248, 255}},
};
inline constexpr size_t name_color_count = sizeof(name_colors) / sizeof(name_colors[0]);

inline uint32_t pack(Rgb c) { return 0x1000000u | (uint32_t(c.r) << 16u) | (uint32_t(c.g) << 8u) | uint32_t(c.b); }
inline Rgb unpack(uint32_t stored) { return {int((stored >> 16u) & 255u), int((stored >> 8u) & 255u), int(stored & 255u)}; }

inline uint32_t refresh_name_color(uint32_t stored) {
    static constexpr uint32_t legacy[] = {0x1ffcc40u, 0x15cb0ffu, 0x170d67cu, 0x1b680ffu, 0x1f25858u, 0x1ff923cu, 0x1ff7ec4u, 0x1e8f0ffu};
    for (size_t i = 0; i < sizeof(legacy) / sizeof(legacy[0]); ++i)
        if (stored == legacy[i])
            return pack(name_colors[i].base);
    return stored;
}

inline const NameColor *palette_entry(uint32_t stored) {
    stored = refresh_name_color(stored);
    for (const auto &entry : name_colors)
        if (pack(entry.base) == stored)
            return &entry;
    return nullptr;
}

inline Rgb plain_color(uint32_t stored, size_t index, size_t count) {
    stored = refresh_name_color(stored);
    const Rgb base = unpack(stored);
    const auto *entry = palette_entry(stored);
    const Rgb light = entry ? entry->light : blend(base, rgb(255, 255, 255), 0.45);
    if (count < 2)
        return base;
    const double t = double(index) / double(count - 1);
    return blend(base, light, 0.55 * smooth(t));
}

inline Rgb readable(Rgb c, Rgb backdrop, Rgb ink) {
    const auto channel = [](int value) {
        const double v = value / 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    const auto lum = [&](Rgb x) { return 0.2126 * channel(x.r) + 0.7152 * channel(x.g) + 0.0722 * channel(x.b); };
    for (int step = 0; step < 4; ++step) {
        const double a = lum(c), b = lum(backdrop), ratio = (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
        if (ratio >= 3.0)
            break;
        c = blend(c, ink, 110.0 / 255.0);
    }
    return c;
}

}
