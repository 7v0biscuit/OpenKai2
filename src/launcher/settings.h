// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>

namespace openkai2::launcher {
struct Settings {
    std::filesystem::path iso;
    std::filesystem::path importIso;
    unsigned fps = 240, width = 1280, height = 720;
    unsigned windowMode = 0;
    unsigned antialiasing = 0;
    unsigned textureFilter = 0;
    unsigned upscaler = 1;
    unsigned aspect = 0;
    bool vsync = true, autoBackups = true;
    bool audio = true;
    unsigned volume = 100;
    bool autoUpdates = true;
    bool discordPresence = true;
    unsigned updateChannel = 1;
    std::string renderer = "auto";
    int windowLeft = 0, windowTop = 0, windowWidth = 0, windowHeight = 0;
    bool windowMaximized = false;
};
std::filesystem::path settings_path();
Settings read_settings(const std::filesystem::path &file);
void write_settings(const std::filesystem::path &file, const Settings &settings);
void validate_settings(const Settings &settings);
std::wstring quote_argument(const std::wstring &value);
}
