// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "speedrun_items.h"

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
    unsigned cpuDifficulty = 0;
    bool autoUpdates = true;
    bool discordPresence = true;
    unsigned updateChannel = 1;
    std::string renderer = "auto";
    int windowLeft = 0, windowTop = 0, windowWidth = 0, windowHeight = 0;
    bool windowMaximized = false;
    unsigned speedrun = 0;
    bool permadeath = false;
    bool fpsCounter = false;
    unsigned overlayItems = speedrun::overlay_defaults;
    unsigned overlayCorner = 0, overlaySize = 1, overlayBackground = 1, overlaySplits = 1;
    std::string onlineName = "Player";
    std::string onlineAddress;
    unsigned onlinePort = 7350, onlineDelay = 0;
    std::vector<std::string> onlineProfiles;
    std::vector<std::string> onlineKeys;
    unsigned onlineVisibility = 0;
    struct Friend {
        std::string owner, id, name;
    };
    std::vector<Friend> onlineFriends;
    std::string onlineRoom, onlinePassword, onlineLobby;
    bool onlineListed = true;
};
std::filesystem::path settings_path();
Settings read_settings(const std::filesystem::path &file);
void write_settings(const std::filesystem::path &file, const Settings &settings);
void validate_settings(const Settings &settings);
std::wstring quote_argument(const std::wstring &value);
}
