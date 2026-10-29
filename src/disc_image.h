// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace openkai2 {

inline constexpr const char* retail_elf_sha256 =
    "431568221bb744a6affc47dc91144b24892839d4d1ab8236bee1e03d83e6b898";

struct DiscFile {
    std::filesystem::path path;
    uint64_t size;
    std::string sha256;
};

struct DiscMount {
    std::filesystem::path root;
    std::filesystem::path executable;
    std::string image_sha256;
    std::vector<DiscFile> files;
    uint64_t bytes_verified = 0;
    size_t files_repaired = 0;
};

struct DiscOptions {
    std::string expected_executable_sha256 = retail_elf_sha256;
    std::function<void(const std::string&)> progress;
};

struct IsoEntry {
    std::filesystem::path path;
    uint32_t sector;
    uint64_t size;
    bool directory;
    std::array<uint8_t,7> recording_date{};
    uint8_t flags=0;
    std::string identifier;
    // Byte offset of this entry's directory record in the image; zero for
    // the root, whose record lives in the volume descriptor.
    uint64_t record_offset=0;
    bool multi_extent=false;
};

// Directory index of an ISO image. Lookups see the files mods replace or
// relocate when a disc overlay is configured for the same image.
class IsoIndex {
    std::filesystem::path image_;
    std::vector<IsoEntry> entries_;
    std::map<std::string, size_t> lookup_;
public:
    explicit IsoIndex(const std::filesystem::path& image);
    const IsoEntry* find(std::string relative_path) const;
    const std::vector<IsoEntry>& entries() const { return entries_; }
    // Upper-case, '/'-separated key without version suffixes; empty for the
    // root. Nothing when the path is invalid or not on the CD/DVD device.
    static std::optional<std::string> key(std::string relative_path);
};

// Reads every ISO file and validates/repairs the extracted copy on every call.
// A cached extraction is never accepted as a substitute for the ISO.
DiscMount prepare_disc(const std::filesystem::path& image,
                       const std::filesystem::path& cache,
                       const DiscOptions& options = {});

std::filesystem::path default_cache_directory();
std::string sha256_file(const std::filesystem::path& file);

} // namespace openkai2
