// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "speedrun.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

class PS2Runtime;

namespace openkai2::speedrun {

struct OverlayOptions {
    Category category = Category::off;
    bool permadeath = false;
    bool fps_counter = false;
    bool randomizer = false;
    bool skip_to_menu = false;
    unsigned items = 0, corner = 0, size = 1, background = 1, split_rows = 5;
    unsigned cpu = 0, decimals = 2, margin = 1, shape = 0;
    std::string title;
    std::vector<std::string> split_names;
    std::array<std::array<unsigned char, 3>, 4> colors{{{245, 245, 240}, {105, 210, 120}, {232, 102, 88}, {250, 196, 70}}};
    std::filesystem::path logs;
    std::filesystem::path splits;
    std::vector<std::string> header;
    std::vector<std::string> problems;
};

void install_overlay(PS2Runtime &runtime, OverlayOptions options);
bool reset_requested();
bool restart_to_menu();

}
