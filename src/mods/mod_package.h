// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/mod_library.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace openkai2::mods {

struct ModConflict {
    std::string earlier, later, what;
    size_t count = 0;
};

std::vector<ModConflict> mod_conflicts(const std::vector<ModInfo> &mods);
void export_mod(const ModInfo &mod, const std::filesystem::path &package);
std::filesystem::path install_mod(const std::filesystem::path &directory, const std::filesystem::path &package);
std::vector<uint8_t> inflate_raw(const uint8_t *data, size_t size, size_t expected);
uint32_t crc32_of(const uint8_t *data, size_t size, uint32_t crc = 0);
inline constexpr const char *package_extension = ".okmod";

}
