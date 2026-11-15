// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace openkai2::mods {

inline constexpr uint32_t capsule_entry = 851u;
inline constexpr uint32_t capsule_file = 0x694A80u, capsule_head_bytes = 0x20u, capsule_record_bytes = 0x20u;
inline constexpr uint32_t capsule_original_count = 520u;
inline constexpr uint32_t capsule_moved_file = 0x000B0000u, capsule_moved_limit = 0x000E8000u;
inline constexpr uint32_t capsule_backup = 0x699850u, capsule_backup_moved = 0x000E8000u, capsule_backup_bytes = 0x210u;
inline constexpr uint32_t capsule_seen_buffer = 0x000F0000u, capsule_seen_limit = 0x00100000u;
inline constexpr uint32_t capsule_progress = 0x693EF0u, capsule_progress_inventory = 0x984u;
inline constexpr uint32_t capsule_inventory = capsule_progress + capsule_progress_inventory;
inline constexpr char capsule_store_magic[8] = {'O', 'K', 'C', 'A', 'P', 'S', '1', 0};
inline constexpr const char *capsule_store_name = "openkai2-capsules.bin";

struct CapsuleTable {
    uint32_t count = 0;
    std::vector<uint8_t> bytes;
};

std::optional<CapsuleTable> read_capsule_table(const std::vector<uint8_t> &file);
uint32_t capsule_count();
uint32_t capsule_address(uint32_t original);
uint32_t capsule_list_bytes(uint32_t original);
uint32_t capsule_seen(uint32_t stack);
uint32_t capsule_backup_address(uint32_t original);
uint32_t capsule_inventory_bytes(uint32_t original);
void configure_capsules(uint32_t count);
std::optional<std::filesystem::path> capsule_replacement(const std::vector<std::filesystem::path> &mods);
std::vector<std::string> install_capsules(const std::vector<std::filesystem::path> &mods);
void set_capsule_store(const std::filesystem::path &file);
std::vector<uint8_t> read_capsule_store(const std::filesystem::path &file, uint32_t count);
void write_capsule_store(const std::filesystem::path &file, const std::vector<uint8_t> &extra);
void capsule_extra_load(uint8_t *rdram, uint32_t progress);
void capsule_extra_save(const uint8_t *rdram, uint32_t progress);

}
