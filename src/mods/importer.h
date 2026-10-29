// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/afs.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// Reads files and archive entries from any PS2 ISO, such as Budokai 2 or
// Budokai 3, and writes them into a mod folder in the layout described in
// mod_library.h. Characters, movesets and stages are archive entries, so
// importing one is copying its bytes into the matching place in a mod.
namespace openkai2::mods {

enum class ContentKind { Character, Moveset, Stage, Other };
const char *content_name(ContentKind kind);

struct SourceFile {
    std::string path; // disc path, e.g. "USR/DATA.AFS"
    uint64_t size = 0;
    bool archive = false;
    size_t entries = 0;
};

class SourceDisc {
public:
    // Throws std::runtime_error if the image is not a readable ISO.
    explicit SourceDisc(const std::filesystem::path &image);
    const std::filesystem::path &image() const { return image_; }
    const std::vector<SourceFile> &files() const { return files_; }
    // The entries of an archive; empty when the file is not an AFS archive.
    const std::vector<AfsEntry> &entries(const std::string &path);
    std::vector<uint8_t> read_file(const std::string &path);
    std::vector<uint8_t> read_entry(const std::string &path, size_t index);

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
    // Replace a whole file, replace entries from `entry` on, or add new entries.
    enum class Mode { File, Entry, NewEntry } mode = Mode::NewEntry;
    std::string file;  // disc path in the game being modded
    size_t entry = 0;  // for Mode::Entry
    std::string name;  // file name to keep; for added entries it becomes the AFS name
    ContentKind kind = ContentKind::Other;
    std::string source; // where the bytes came from, for imports.txt
};

// Writes bytes into the mod and returns the file written. Replacing an entry
// removes any earlier file this mod held for the same entry; added entries
// get a free name, so there is no limit on how many are added.
std::filesystem::path import_bytes(const std::filesystem::path &mod, const ImportTarget &target,
                                   const std::vector<uint8_t> &bytes);
std::filesystem::path import_host_file(const std::filesystem::path &mod, const ImportTarget &target,
                                       const std::filesystem::path &source);

// One line per import in the mod's imports.txt, so a mod records what it holds.
struct ImportRecord {
    std::string kind, target, source;
};
std::vector<ImportRecord> read_imports(const std::filesystem::path &mod);

} // namespace openkai2::mods
