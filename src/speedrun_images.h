// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace openkai2::speedrun {

struct ImageFrame {
    std::vector<uint8_t> rgba;
    unsigned delay_ms = 100;
};

struct SplitImage {
    unsigned width = 0, height = 0;
    std::vector<ImageFrame> frames;
    size_t frame_at(uint64_t milliseconds) const;
};

struct ImportedImage {
    std::string file, name;
    SplitImage image;
};

bool valid_image_file(const std::string& file);
SplitImage load_split_image(const std::filesystem::path& file);
ImportedImage import_split_image(const std::filesystem::path& source, const std::filesystem::path& directory);
SplitImage decode_image_bytes(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> thumbnail_pixels(const SplitImage& image, unsigned side, bool fit, uint32_t background);
std::vector<uint8_t> encode_thumbnail(const SplitImage& image, bool fit, uint32_t background, size_t limit);

}
