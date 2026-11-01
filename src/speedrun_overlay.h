// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "speedrun.h"
#include <filesystem>
#include <string>
#include <vector>

class PS2Runtime;

namespace openkai2::speedrun {

struct OverlayOptions {
    Category category = Category::off;
    bool permadeath = false;
    bool fps_counter = false;
    unsigned items = 0, corner = 0, size = 1, background = 1, split_rows = 5;
    std::filesystem::path logs;
    std::filesystem::path splits;
    std::vector<std::string> header;
    std::vector<std::string> problems;
};

void install_overlay(PS2Runtime &runtime, OverlayOptions options);
bool reset_requested();

}
