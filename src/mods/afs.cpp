// SPDX-License-Identifier: GPL-3.0-only
#include "mods/afs.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace openkai2::mods {
namespace {
uint32_t le32(const uint8_t *bytes) {
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
uint16_t le16(const uint8_t *bytes) { return uint16_t(bytes[0] | bytes[1] << 8); }
void put32(std::vector<uint8_t> &bytes, size_t at, uint64_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = uint8_t(value >> (8 * i));
}
void put16(std::vector<uint8_t> &bytes, size_t at, uint16_t value) {
    bytes[at] = uint8_t(value);
    bytes[at + 1] = uint8_t(value >> 8);
}
uint64_t align(uint64_t value) { return (value + afs_alignment - 1u) / afs_alignment * afs_alignment; }
[[noreturn]] void fail(const std::string &message) { throw std::runtime_error("AFS archive: " + message); }

bool plausible_names(uint64_t offset, uint32_t size, size_t count, uint64_t archive) {
    return offset && size >= count * afs_name_bytes && offset <= archive && size <= archive - offset;
}
}

bool is_afs(const ReadFn &read, uint64_t size) {
    if (size < 16u)
        return false;
    uint8_t header[8];
    read(0, header, sizeof(header));
    return std::memcmp(header, "AFS\0", 4) == 0 && le32(header + 4) < 0x1000000u &&
           8u + uint64_t(le32(header + 4)) * 8u + 8u <= size;
}

AfsArchive parse_afs(const ReadFn &read, uint64_t size) {
    if (!is_afs(read, size))
        fail("missing AFS signature");
    uint8_t header[8];
    read(0, header, sizeof(header));
    const uint32_t count = le32(header + 4);
    std::vector<uint8_t> table(size_t(count) * 8u + 8u);
    read(8, table.data(), table.size());
    AfsArchive archive;
    archive.entries.resize(count);
    uint64_t first = size;
    for (uint32_t i = 0; i < count; ++i) {
        auto &entry = archive.entries[i];
        entry.offset = le32(table.data() + i * 8u);
        entry.size = le32(table.data() + i * 8u + 4u);
        if (entry.size && (entry.offset > size || entry.size > size - entry.offset))
            fail("entry " + std::to_string(i) + " lies outside the archive");
        if (entry.size)
            first = std::min(first, entry.offset);
    }
    uint64_t pointer = 8u + uint64_t(count) * 8u;
    uint64_t names = le32(table.data() + count * 8u);
    uint32_t namesSize = le32(table.data() + count * 8u + 4u);
    if (!plausible_names(names, namesSize, count, size) && first >= pointer + 16u && first <= size) {
        uint8_t alternate[8];
        read(first - 8u, alternate, sizeof(alternate));
        if (plausible_names(le32(alternate), le32(alternate + 4), count, size)) {
            pointer = first - 8u;
            names = le32(alternate);
            namesSize = le32(alternate + 4);
        }
    }
    archive.name_pointer_offset = pointer;
    if (plausible_names(names, namesSize, count, size)) {
        archive.named = true;
        archive.name_table_offset = names;
        archive.name_table_size = namesSize;
        std::vector<uint8_t> records(size_t(count) * afs_name_bytes);
        read(names, records.data(), records.size());
        for (uint32_t i = 0; i < count; ++i) {
            const auto *record = records.data() + i * afs_name_bytes;
            auto &entry = archive.entries[i];
            entry.name.assign(reinterpret_cast<const char *>(record),
                              strnlen(reinterpret_cast<const char *>(record), 32u));
            for (unsigned d = 0; d < 6; ++d)
                entry.date[d] = le16(record + 32u + d * 2u);
        }
    }
    return archive;
}

AfsLayout layout_afs(const std::vector<AfsEntry> &entries, bool named) {
    if (entries.size() >= 0x1000000u)
        fail("too many entries");
    AfsLayout layout;
    const uint64_t headerBytes = 8u + uint64_t(entries.size()) * 8u + 8u;
    layout.header.assign(size_t(align(headerBytes)), 0u);
    std::memcpy(layout.header.data(), "AFS\0", 4);
    put32(layout.header, 4, entries.size());
    uint64_t at = layout.header.size();
    layout.offsets.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        layout.offsets.push_back(at);
        put32(layout.header, 8u + i * 8u, at);
        put32(layout.header, 12u + i * 8u, entries[i].size);
        at = align(at + entries[i].size);
    }
    if (at > 0xffffffffull)
        fail("archive would exceed 4 GiB");
    if (named) {
        layout.name_table_offset = at;
        const uint64_t tableBytes = uint64_t(entries.size()) * afs_name_bytes;
        layout.name_table.assign(size_t(align(tableBytes)), 0u);
        for (size_t i = 0; i < entries.size(); ++i) {
            const size_t record = i * afs_name_bytes;
            const auto name = entries[i].name.substr(0, 31u);
            std::memcpy(layout.name_table.data() + record, name.data(), name.size());
            for (unsigned d = 0; d < 6; ++d)
                put16(layout.name_table, record + 32u + d * 2u, entries[i].date[d]);
            put32(layout.name_table, record + 44u, entries[i].size);
        }
        put32(layout.header, 8u + entries.size() * 8u, layout.name_table_offset);
        put32(layout.header, 12u + entries.size() * 8u, tableBytes);
        at += layout.name_table.size();
    }
    layout.size = at;
    return layout;
}

}
