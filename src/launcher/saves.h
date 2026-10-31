// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace openkai2::launcher {
struct SaveFile {
    std::string name;
    std::vector<uint8_t> data;
};
using SaveFiles = std::vector<SaveFile>;
SaveFiles read_save(const std::filesystem::path &source);
SaveFiles read_card(const std::filesystem::path &card);
std::filesystem::path backup_card(const std::filesystem::path &card,
                                  const std::filesystem::path &backups);
std::vector<std::filesystem::path> list_backups(const std::filesystem::path &backups);
std::vector<std::string> list_card_saves(const std::filesystem::path &card);
void delete_card_saves(const std::filesystem::path &card, const std::filesystem::path &backups,
                       const std::vector<std::string> &names);
void delete_backup_files(const std::filesystem::path &backups,
                         const std::vector<std::filesystem::path> &files);
void import_save(const std::filesystem::path &card, const std::filesystem::path &source,
                 const std::filesystem::path &backups, bool restore = false);
bool unlock_card_content(const std::filesystem::path& card,
                         const std::filesystem::path& backups);
}
