// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace openkai2::mods {

struct ModInfo {
    std::filesystem::path folder;
    std::string id;
    std::string name, author, version, description;
    bool enabled = true;
    size_t replaced_files = 0, archive_entries = 0;
};

std::filesystem::path default_mods_directory();
std::vector<ModInfo> scan_mods(const std::filesystem::path &directory);
void save_load_order(const std::filesystem::path &directory, const std::vector<ModInfo> &mods);
std::vector<std::filesystem::path> enabled_mod_folders(const std::filesystem::path &directory);
std::filesystem::path create_mod(const std::filesystem::path &directory, const std::string &name,
                                 const std::string &author, const std::string &description);
void rename_mod(const std::filesystem::path &folder, const std::string &name);
void delete_mod(const std::filesystem::path &directory, const std::string &id);
inline constexpr const char *reports_folder = "reports";

}
