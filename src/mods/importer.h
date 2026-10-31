// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/afs.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace openkai2::mods {

enum class ContentKind { Character, Moveset, Stage, Other };
const char *content_name(ContentKind kind);

struct SourceFile {
    std::string path;
    uint64_t size = 0;
    bool archive = false;
    size_t entries = 0;
};

using Progress = std::function<void(uint64_t bytes)>;

class SourceDisc {
public:
    struct Range {
        uint64_t offset = 0, size = 0;
    };
    explicit SourceDisc(const std::filesystem::path &image);
    const std::filesystem::path &image() const { return image_; }
    const std::vector<SourceFile> &files() const { return files_; }
    const std::vector<AfsEntry> &entries(const std::string &path);
    std::vector<uint8_t> read_file(const std::string &path);
    std::vector<uint8_t> read_entry(const std::string &path, size_t index);
    Range range(const std::string &path, std::optional<size_t> entry = std::nullopt);
    void copy(const Range &range, std::ostream &out, const Progress &progress = {});

private:
    struct Location {
        uint64_t offset, size;
    };
    Location locate(const std::string &path) const;
    void read(uint64_t offset, uint8_t *destination, size_t bytes);

    std::filesystem::path image_;
    uint64_t image_size_ = 0;
    std::ifstream stream_;
    std::vector<SourceFile> files_;
    std::map<std::string, Location> locations_;
    std::map<std::string, std::vector<AfsEntry>> archives_;
};

struct ImportTarget {
    enum class Mode { File, Entry, NewEntry } mode = Mode::NewEntry;
    std::string file;
    size_t entry = 0;
    std::string name;
    ContentKind kind = ContentKind::Other;
    std::string source;
};

std::filesystem::path import_bytes(const std::filesystem::path &mod, const ImportTarget &target,
                                   const std::vector<uint8_t> &bytes);
std::filesystem::path import_host_file(const std::filesystem::path &mod, const ImportTarget &target,
                                       const std::filesystem::path &source, const Progress &progress = {});
std::filesystem::path import_from_disc(SourceDisc &disc, const std::string &path, std::optional<size_t> entry,
                                       const std::filesystem::path &mod, const ImportTarget &target,
                                       const Progress &progress = {});

struct ImportRecord {
    std::string kind, target, source;
};
std::vector<ImportRecord> read_imports(const std::filesystem::path &mod);

}
