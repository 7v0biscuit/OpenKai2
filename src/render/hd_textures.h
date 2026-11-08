// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openkai2::hd {

enum Mode : unsigned { off = 0, upscaled = 1, packs = 2, packs_upscaled = 3, dump = 4, mode_count = 5 };

struct Image {
    unsigned width = 0, height = 0;
    std::vector<uint32_t> texels;
};

uint64_t texture_hash(const uint32_t *texels, unsigned width, unsigned height);
std::string hash_name(uint64_t hash);
Image upscale(const Image &source, unsigned factor);
bool load_image(const std::filesystem::path &path, Image &image);
bool save_image(const std::filesystem::path &path, const Image &image);

class Library {
  public:
    Library() = default;
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    ~Library();
    void configure(unsigned mode, const std::filesystem::path &folder, unsigned factor);
    unsigned mode() const { return mode_; }
    bool replacing() const { return mode_ == upscaled || mode_ == packs || mode_ == packs_upscaled; }
    std::shared_ptr<const Image> find(uint64_t hash) const;
    bool known(uint64_t hash) const;
    bool has_packs() const;
    void submit(uint64_t hash, Image source);
    void wait_idle();
    struct Counts {
        uint64_t packed = 0, upscaled = 0, dumped = 0, missing = 0;
    };
    Counts counts() const;

  private:
    void work();
    unsigned mode_ = off, factor_ = 4;
    std::filesystem::path folder_;
    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::deque<std::pair<uint64_t, Image>> queue_;
    std::unordered_map<uint64_t, std::shared_ptr<const Image>> ready_;
    std::unordered_set<uint64_t> seen_;
    std::unordered_map<uint64_t, std::filesystem::path> files_;
    Counts counts_;
    bool stop_ = false, busy_ = false;
    std::thread worker_;
};

Library &library();

}
