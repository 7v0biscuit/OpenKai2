// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "disc_image.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace openkai2::mods {

class DiscOverlay {
public:
    struct Summary {
        size_t replaced_files = 0, replaced_entries = 0, added_entries = 0, relocated_files = 0;
        std::vector<std::string> notes;
        std::map<std::string, std::map<std::string, uint32_t>> added;
    };

    static DiscOverlay &global();

    Summary configure(const std::filesystem::path &image, const std::vector<std::filesystem::path> &mods);
    void clear();

    bool active_for(const std::filesystem::path &image) const;
    uint64_t size() const { return size_; }
    bool read(uint64_t offset, void *destination, size_t bytes) const;
    const IsoEntry *find(const std::filesystem::path &image, const std::string &key) const;

private:
    struct Piece {
        enum class Kind { Zero, Image, File, Memory } kind = Kind::Zero;
        uint64_t source = 0;
        size_t file = 0;
        std::shared_ptr<const std::vector<uint8_t>> memory;
    };
    struct Segment {
        uint64_t length = 0;
        Piece piece;
    };
    struct Content;

    void assign(uint64_t start, uint64_t length, Piece piece);
    void patch(uint64_t offset, std::vector<uint8_t> bytes);
    void patch_record(const IsoEntry &entry, uint32_t sector, uint64_t size);
    bool read_piece(const Piece &piece, uint64_t offset, uint8_t *destination, size_t bytes) const;
    size_t add_file(const std::filesystem::path &file);

    std::filesystem::path image_;
    uint64_t image_size_ = 0, size_ = 0;
    std::map<uint64_t, Segment> segments_;
    std::map<std::string, IsoEntry> entries_;
    std::vector<std::filesystem::path> files_;
    mutable std::mutex mutex_;
    mutable std::ifstream image_stream_;
    mutable std::vector<std::unique_ptr<std::ifstream>> file_streams_;
    mutable std::filesystem::path checked_;
    mutable bool checked_matches_ = false;
    bool active_ = false;
};

}
