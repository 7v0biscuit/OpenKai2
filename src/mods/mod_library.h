// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Mods are folders under the mods directory. Each may hold:
//
//   mod.ini                    name=, author=, version=, description=
//   files/<disc path>          replaces that file on the disc
//   afs/<disc path>/<n>...     replaces entry n of that AFS archive; any text
//                              may follow the number, e.g. 0412_goku.bin
//   afs/<disc path>/add/<name> appends a new entry to the archive, in name
//                              order; there is no limit on how many
//
// Disc paths match case-insensitively, e.g. files/USR/DATA_US.AFS.
// load-order.txt lists folder names, one per line, prefixed with + when the
// mod is enabled or - when it is not; later mods win over earlier ones.
namespace openkai2::mods {

struct ModInfo {
    std::filesystem::path folder;
    std::string id; // folder name
    std::string name, author, version, description;
    bool enabled = true;
    size_t replaced_files = 0, archive_entries = 0;
};

std::filesystem::path default_mods_directory();
// Every mod folder, in load order. Folders missing from load-order.txt are
// appended, enabled, in name order.
std::vector<ModInfo> scan_mods(const std::filesystem::path &directory);
void save_load_order(const std::filesystem::path &directory, const std::vector<ModInfo> &mods);
// Enabled mod folders in load order.
std::vector<std::filesystem::path> enabled_mod_folders(const std::filesystem::path &directory);
// Creates directory/<id> with a mod.ini, choosing a free folder name based on
// `name`, and appends it to the load order. Returns the folder.
std::filesystem::path create_mod(const std::filesystem::path &directory, const std::string &name,
                                 const std::string &author, const std::string &description);
// Gives a mod a new name in its mod.ini; its folder keeps its name, so the
// load order and other mods' references stay valid.
void rename_mod(const std::filesystem::path &folder, const std::string &name);
// Deletes a mod's folder and drops it from the load order.
void delete_mod(const std::filesystem::path &directory, const std::string &id);
// The folder under the mods directory that holds disc reports, not a mod.
inline constexpr const char *reports_folder = "reports";

} // namespace openkai2::mods
