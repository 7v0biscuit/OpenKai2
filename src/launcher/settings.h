// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "input_bindings.h"
#include "speedrun_items.h"
#include "launcher/theme.h"

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
    unsigned internalScale = 0;
    unsigned hdTextures = 0;
    bool music = true;
    bool vsync = true, autoBackups = true;
    bool audio = true;
    unsigned volume = 100;
    unsigned cpuDifficulty = 0;
    bool autoUpdates = true;
    bool discordPresence = true;
    bool reducedMotion = false, helpHints = true;
    unsigned updateChannel = 1;
    std::string renderer = "auto";
    int windowLeft = 0, windowTop = 0, windowWidth = 0, windowHeight = 0;
    bool windowMaximized = false;
    unsigned speedrun = 0;
    bool permadeath = false;
    bool fpsCounter = false;
    bool randomizer = false;
    bool ps2LoadTimes = true;
    unsigned runAhead = 0;
    unsigned imageEffect = 0;
    unsigned brightness = 1;
    input::Bindings controls;
    unsigned overlayItems = speedrun::overlay_defaults;
    unsigned overlayCorner = 0, overlaySize = 1, overlayBackground = 1, overlaySplits = 1;
    std::array<unsigned, speedrun::overlay_color_roles> overlayColors{0, 2, 3, 4};
    unsigned overlayDecimals = 2;
    unsigned overlayMargin = 1, overlayShape = 0;
    std::string overlayTitle, overlaySplitNames;
    struct SplitStyle {
        std::string run;
        unsigned index = 0;
        std::string name, image, imageName;
    };
    std::vector<SplitStyle> splitStyles;
    std::string onlineName = "Player";
    std::string onlineAddress;
    unsigned onlinePort = 7350, onlineDelay = 0;
    std::vector<std::string> onlineProfiles;
    std::vector<std::string> onlineKeys;
    unsigned onlineVisibility = 0;
    unsigned onlineRoomFilter = 0, onlinePlayerFilter = 0;
    struct Friend {
        std::string owner, id, name;
    };
    std::vector<Friend> onlineFriends;
    struct ProfileStyle {
        std::string id, initials, tagline;
        unsigned accent = 0;
        std::string image, imageName;
        unsigned pictureFit = 0, availability = 0;
        bool notifications = true;
    };
    std::vector<ProfileStyle> profileStyles;
    std::string onlineRoom, onlinePassword, onlineLobby;
    bool onlineListed = true;
    unsigned launcherTheme = 0, launcherAccent = 0, startPage = 0, lastPage = 0;
    std::array<unsigned, tab_count> tabOrder = default_tab_order;
    unsigned hiddenTabs = 0;
    CustomColors launcherColors = no_custom_colors();
};
struct SplitAppearance {
    std::vector<std::string> names;
    std::vector<std::filesystem::path> images;
};
SplitAppearance split_appearance(const Settings& settings, const std::string& run, const std::filesystem::path& image_directory);

std::filesystem::path settings_path();
std::filesystem::path textures_directory();
Settings read_settings(const std::filesystem::path &file);
void write_settings(const std::filesystem::path &file, const Settings &settings);
void validate_settings(const Settings &settings);
Settings profile_recovery(const Settings& settings, size_t index, const std::string& id);
void restore_profile(Settings& settings, const Settings& recovery);
std::wstring quote_argument(const std::wstring &value);
}
