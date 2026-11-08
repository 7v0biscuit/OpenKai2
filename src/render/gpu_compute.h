// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace openkai2::gpu {

struct Range {
    uint32_t offset = 0, size = 0;
};

class Compute {
  public:
    virtual ~Compute() = default;
    virtual const std::string &description() const = 0;
    virtual void create(const std::vector<uint32_t> &tables) = 0;
    virtual void upload(uint32_t offset, const void *data, size_t size) = 0;
    virtual void raster(const std::vector<uint32_t> &commands, const std::vector<uint32_t> &bins,
                        const std::vector<uint32_t> &references, const std::vector<uint32_t> &palette) = 0;
    virtual void transfer(const std::vector<uint32_t> &job, unsigned pixels) = 0;
    virtual void read(const std::vector<Range> &ranges, uint8_t *destination) = 0;
    virtual void finish() = 0;
    virtual bool hires_supported() const { return false; }
    virtual size_t storage_word_limit() const { return std::numeric_limits<size_t>::max() / 4u; }
    virtual void hires_reserve(size_t colorWords, size_t depthWords) { (void)colorWords, (void)depthWords; }
    virtual void hires_raster(const std::vector<uint32_t> &params, unsigned groups) { (void)params, (void)groups; }
    virtual void hires_seed(const std::vector<uint32_t> &params, unsigned groupsX, unsigned groupsY) {
        (void)params, (void)groupsX, (void)groupsY;
    }
    virtual bool hires_resolve(const std::vector<uint32_t> &params, unsigned groupsX, unsigned groupsY) {
        (void)params, (void)groupsX, (void)groupsY;
        return false;
    }
    virtual void hires_read(size_t offsetWords, size_t words, uint32_t *destination) {
        (void)offsetWords, (void)words, (void)destination;
    }
    virtual void hires_upload(size_t offsetWords, const uint32_t *source, size_t words) {
        (void)offsetWords, (void)source, (void)words;
    }
    virtual void destroy() = 0;
};

std::unique_ptr<Compute> make_vulkan_compute(std::string &error);
bool vulkan_available() noexcept;

}
