// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// CRI AFS archives, which hold the game's characters, stages, voices and
// music. An archive starts with "AFS\0", an entry count and an
// (offset, size) pair per entry, followed by the location of an optional
// table of 48-byte names. Entries start on 2048-byte boundaries.
namespace openkai2::mods {

inline constexpr uint32_t afs_alignment = 2048u;
inline constexpr uint32_t afs_name_bytes = 48u;

struct AfsEntry {
    uint64_t offset = 0; // from the start of the archive
    uint32_t size = 0;
    std::string name;
    // Year, month, day, hour, minute and second, as stored in the name table.
    std::array<uint16_t, 6> date{};
};

struct AfsArchive {
    std::vector<AfsEntry> entries;
    bool named = false;
    uint64_t name_table_offset = 0;
    uint32_t name_table_size = 0;
    // Where the name table location is stored, for archives that keep it
    // just before the first entry rather than after the entry table.
    uint64_t name_pointer_offset = 0;
};

using ReadFn = std::function<void(uint64_t offset, uint8_t *destination, size_t bytes)>;

bool is_afs(const ReadFn &read, uint64_t size);
// Throws std::runtime_error if the bytes are not a well-formed archive.
AfsArchive parse_afs(const ReadFn &read, uint64_t size);

struct AfsLayout {
    std::vector<uint8_t> header;     // signature, entry table and name pointer, padded
    std::vector<uint64_t> offsets;   // where each entry starts
    uint64_t name_table_offset = 0;  // zero when no names are written
    std::vector<uint8_t> name_table; // padded to the alignment
    uint64_t size = 0;               // the whole archive
};

// Places entries of the given sizes back to back on 2048-byte boundaries.
// Names are written when `named` is set; each is cut to 31 characters.
AfsLayout layout_afs(const std::vector<AfsEntry> &entries, bool named);

} // namespace openkai2::mods
