// SPDX-License-Identifier: GPL-3.0-only
#include "mods/capsules.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace openkai2::mods {
namespace fs = std::filesystem;
namespace {
uint32_t active = capsule_original_count;
fs::path store;

uint32_t word(const std::vector<uint8_t> &bytes, size_t at) {
    return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | uint32_t(bytes[at + 3]) << 24;
}
}

std::optional<CapsuleTable> read_capsule_table(const std::vector<uint8_t> &file) {
    if (file.size() < capsule_head_bytes || !std::equal(file.begin(), file.begin() + 4, "#SKA") ||
        word(file, 0x04) != capsule_head_bytes || word(file, 0x14) != capsule_record_bytes)
        return std::nullopt;
    const auto count = word(file, 0x10);
    if (file.size() < capsule_head_bytes + size_t(count) * capsule_record_bytes)
        return std::nullopt;
    return CapsuleTable{count, file};
}

uint32_t capsule_count() { return active; }

uint32_t capsule_address(uint32_t original) {
    if (active <= capsule_original_count)
        return original;
    return capsule_moved_file + (original - capsule_file);
}

uint32_t capsule_list_bytes(uint32_t original) {
    return active <= capsule_original_count ? original : original / capsule_original_count * active;
}

uint32_t capsule_seen(uint32_t stack) {
    return active <= capsule_original_count ? stack : capsule_seen_buffer - 0x80u;
}

uint32_t capsule_backup_address(uint32_t original) {
    return active <= capsule_original_count ? original : capsule_backup_moved + (original - capsule_backup);
}

uint32_t capsule_inventory_bytes(uint32_t original) {
    return active <= capsule_original_count ? original : original - capsule_original_count + active;
}

void configure_capsules(uint32_t count) {
    const uint32_t room = (capsule_moved_limit - capsule_moved_file - capsule_head_bytes) / capsule_record_bytes;
    if (count > room || count > capsule_seen_limit - capsule_seen_buffer ||
        capsule_backup_moved + capsule_backup_bytes - capsule_original_count + count > capsule_seen_buffer - 0x80u || capsule_inventory + count > capsule_file + capsule_head_bytes + capsule_original_count * capsule_record_bytes)
        throw std::runtime_error("The capsule table holds " + std::to_string(count) + " capsules; at most " +
                                 std::to_string(room) + " fit");
    active = std::max(count, capsule_original_count);
}

std::optional<fs::path> capsule_replacement(const std::vector<fs::path> &mods) {
    std::optional<fs::path> found;
    for (const auto &mod : mods) {
        std::error_code error;
        for (fs::directory_iterator it(mod / "afs" / "USR" / "DATA_CMN.AFS", error), end; !error && it != end;
             it.increment(error)) {
            const auto name = it->path().filename().string();
            if (it->is_regular_file(error) && name.size() > 4 && std::all_of(name.begin(), name.begin() + 4, ::isdigit) &&
                std::stoul(name.substr(0, 4)) == capsule_entry)
                found = it->path();
        }
    }
    return found;
}

std::vector<std::string> install_capsules(const std::vector<fs::path> &mods) {
    configure_capsules(capsule_original_count);
    const auto path = capsule_replacement(mods);
    if (!path)
        return {};
    std::ifstream in(*path, std::ios::binary);
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto table = read_capsule_table(file);
    if (!table)
        return {"capsules: " + path->filename().string() + " is not a capsule table; capsules were not changed"};
    configure_capsules(table->count);
    return {"capsules: " + std::to_string(table->count) + " capsules from " + path->parent_path().parent_path().parent_path().parent_path().filename().string() +
            (table->count > capsule_original_count ? ", table moved to 0xB0000" : std::string())};
}

void set_capsule_store(const fs::path &file) { store = file; }

std::vector<uint8_t> read_capsule_store(const fs::path &file, uint32_t count) {
    std::vector<uint8_t> extra(count > capsule_original_count ? count - capsule_original_count : 0);
    std::ifstream in(file, std::ios::binary);
    char magic[8]{};
    uint8_t size[4]{};
    if (!in.read(magic, 8) || !std::equal(magic, magic + 8, capsule_store_magic) ||
        !in.read(reinterpret_cast<char *>(size), 4))
        return extra;
    const uint32_t stored = size[0] | size[1] << 8 | size[2] << 16 | uint32_t(size[3]) << 24;
    std::vector<uint8_t> counts(stored);
    in.read(reinterpret_cast<char *>(counts.data()), std::streamsize(counts.size()));
    counts.resize(size_t(in.gcount()));
    std::copy_n(counts.begin(), std::min(counts.size(), extra.size()), extra.begin());
    return extra;
}

void write_capsule_store(const fs::path &file, const std::vector<uint8_t> &extra) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    const auto temporary = fs::path(file).concat(".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        const uint32_t size = uint32_t(extra.size());
        const uint8_t bytes[4] = {uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16), uint8_t(size >> 24)};
        out.write(capsule_store_magic, 8);
        out.write(reinterpret_cast<const char *>(bytes), 4);
        out.write(reinterpret_cast<const char *>(extra.data()), std::streamsize(extra.size()));
        if (!out)
            throw std::runtime_error("Cannot write " + temporary.string());
    }
    fs::rename(temporary, file);
}

void capsule_extra_load(uint8_t *rdram, uint32_t progress) {
    if (active <= capsule_original_count || (progress & 0x1FFFFFFu) != capsule_progress || store.empty())
        return;
    const auto extra = read_capsule_store(store, active);
    std::cerr << "[mods] capsules: loaded " << std::count_if(extra.begin(), extra.end(), [](uint8_t c) { return c; })
              << " owned added capsules" << std::endl;
    std::copy(extra.begin(), extra.end(), rdram + capsule_inventory + capsule_original_count);
}

void capsule_extra_save(const uint8_t *rdram, uint32_t progress) {
    if (active <= capsule_original_count || (progress & 0x1FFFFFFu) != capsule_progress || store.empty())
        return;
    const uint8_t *first = rdram + capsule_inventory + capsule_original_count;
    try {
        write_capsule_store(store, std::vector<uint8_t>(first, first + (active - capsule_original_count)));
    } catch (const std::exception &) {
    }
}

}
