// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openkai2::mods {

inline constexpr uint32_t afs_alignment = 2048u;
inline constexpr uint32_t afs_name_bytes = 48u;

struct AfsEntry {
    uint64_t offset = 0;
    uint32_t size = 0;
    std::string name;
    std::array<uint16_t, 6> date{};
};

struct AfsArchive {
    std::vector<AfsEntry> entries;
    bool named = false;
    uint64_t name_table_offset = 0;
    uint32_t name_table_size = 0;
    uint64_t name_pointer_offset = 0;
};

using ReadFn = std::function<void(uint64_t offset, uint8_t *destination, size_t bytes)>;

bool is_afs(const ReadFn &read, uint64_t size);
AfsArchive parse_afs(const ReadFn &read, uint64_t size);

struct AfsLayout {
    std::vector<uint8_t> header;
    std::vector<uint64_t> offsets;
    uint64_t name_table_offset = 0;
    std::vector<uint8_t> name_table;
    uint64_t size = 0;
};

AfsLayout layout_afs(const std::vector<AfsEntry> &entries, bool named);

}
