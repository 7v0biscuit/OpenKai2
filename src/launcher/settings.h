// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>

namespace openkai2::launcher {
struct Settings {
    std::filesystem::path iso;
    unsigned fps = 60, width = 1280, height = 720;
    unsigned windowMode = 0;    // windowed, borderless
    unsigned antialiasing = 0;  // off, FXAA
    unsigned textureFilter = 0; // game, nearest, bilinear
    unsigned upscaler = 1;      // nearest, bilinear, bicubic, FSR 1
    unsigned aspect = 0;        // 4:3, 16:9, stretch, native pixels
    bool vsync = true, autoBackups = true;
    bool audio = true;
    unsigned volume = 100;      // percent
    bool autoUpdates = true;
    unsigned updateChannel = 1; // stable, stable and prerelease
    std::string renderer = "auto";
};
std::filesystem::path settings_path();
Settings read_settings(const std::filesystem::path &file);
void write_settings(const std::filesystem::path &file, const Settings &settings);
void validate_settings(const Settings &settings);
// Windows command-line quoting, including trailing slashes and embedded quotes.
std::wstring quote_argument(const std::wstring &value);
} // namespace openkai2::launcher
