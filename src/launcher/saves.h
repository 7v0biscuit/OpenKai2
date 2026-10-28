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
// Always snapshots the current card first; an invalid import changes nothing.
// Imports merge file names; restores replace the card with the selected backup.
void import_save(const std::filesystem::path &card, const std::filesystem::path &source,
                 const std::filesystem::path &backups, bool restore = false);
// Validates the USA progress file, backs up the card, then atomically installs
// its character/stage unlocks. A missing or damaged save changes nothing.
bool unlock_card_content(const std::filesystem::path& card,
                         const std::filesystem::path& backups);
} // namespace openkai2::launcher
