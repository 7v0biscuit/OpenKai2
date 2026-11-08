// SPDX-License-Identifier: GPL-3.0-only
#include "hd_textures.h"
#include <atomic>
#include "raylib.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace openkai2::hd {
namespace {

int distance(uint32_t a, uint32_t b) {
    int total = 0;
    for (unsigned shift = 0; shift < 32u; shift += 8u)
        total += std::abs(int((a >> shift) & 255u) - int((b >> shift) & 255u));
    return total;
}

bool same(uint32_t a, uint32_t b) { return distance(a, b) < 48; }

Image double_size(const Image &source) {
    Image out;
    out.width = source.width * 2u;
    out.height = source.height * 2u;
    out.texels.resize(size_t(out.width) * out.height);
    const auto at = [&](int x, int y) {
        x = std::clamp(x, 0, int(source.width) - 1);
        y = std::clamp(y, 0, int(source.height) - 1);
        return source.texels[size_t(y) * source.width + unsigned(x)];
    };
    for (int y = 0; y < int(source.height); ++y)
        for (int x = 0; x < int(source.width); ++x) {
            const uint32_t p = at(x, y), a = at(x, y - 1), b = at(x + 1, y), c = at(x - 1, y), d = at(x, y + 1);
            uint32_t e0 = p, e1 = p, e2 = p, e3 = p;
            if (!same(a, d) && !same(c, b)) {
                if (same(c, a))
                    e0 = a;
                if (same(a, b))
                    e1 = b;
                if (same(d, c))
                    e2 = c;
                if (same(b, d))
                    e3 = d;
            }
            const size_t row = size_t(y) * 2u * out.width + size_t(x) * 2u;
            out.texels[row] = e0;
            out.texels[row + 1u] = e1;
            out.texels[row + out.width] = e2;
            out.texels[row + out.width + 1u] = e3;
        }
    return out;
}

bool read_file(const std::filesystem::path &path, std::vector<unsigned char> &bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    bytes.assign(std::istreambuf_iterator<char>(in), {});
    return !bytes.empty();
}

bool hex_name(const std::filesystem::path &path, uint64_t &hash) {
    const auto stem = path.stem().string();
    if (stem.size() != 16u)
        return false;
    hash = 0;
    for (char c : stem) {
        const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0)
            return false;
        hash = (hash << 4u) | uint64_t(v);
    }
    return true;
}

}

uint64_t texture_hash(const uint32_t *texels, unsigned width, unsigned height) {
    uint64_t hash = 0xcbf29ce484222325ull ^ (uint64_t(width) << 32u) ^ height;
    const size_t count = size_t(width) * height;
    for (size_t i = 0; i < count; ++i) {
        hash ^= texels[i];
        hash *= 0x100000001b3ull;
        hash ^= hash >> 29u;
    }
    return hash;
}

std::string hash_name(uint64_t hash) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

Image upscale(const Image &source, unsigned factor) {
    if (!source.width || !source.height || source.texels.size() != size_t(source.width) * source.height)
        return {};
    Image image = source;
    for (unsigned scale = 1u; scale < factor; scale *= 2u)
        image = double_size(image);
    return image;
}

bool load_image(const std::filesystem::path &path, Image &image) {
    std::vector<unsigned char> bytes;
    if (!read_file(path, bytes))
        return false;
    ::Image loaded = LoadImageFromMemory(".png", bytes.data(), int(bytes.size()));
    if (!loaded.data || loaded.width <= 0 || loaded.height <= 0 || loaded.width > 8192 || loaded.height > 8192) {
        UnloadImage(loaded);
        return false;
    }
    ImageFormat(&loaded, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    image.width = unsigned(loaded.width);
    image.height = unsigned(loaded.height);
    image.texels.resize(size_t(image.width) * image.height);
    std::memcpy(image.texels.data(), loaded.data, image.texels.size() * 4u);
    UnloadImage(loaded);
    return true;
}

bool save_image(const std::filesystem::path &path, const Image &image) {
    if (!image.width || !image.height)
        return false;
    std::vector<uint32_t> copy = image.texels;
    ::Image raw{copy.data(), int(image.width), int(image.height), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    int size = 0;
    unsigned char *png = ExportImageToMemory(raw, ".png", &size);
    if (!png || size <= 0)
        return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(png), size);
    MemFree(png);
    return bool(out);
}

Library::~Library() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void Library::configure(unsigned mode, const std::filesystem::path &folder, unsigned factor) {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&] { return queue_.empty() && !busy_; });
    mode_ = mode < mode_count ? mode : off;
    folder_ = folder;
    factor_ = factor >= 4u ? 4u : 2u;
    ready_.clear();
    seen_.clear();
    files_.clear();
    counts_ = {};
    if (mode_ == packs || mode_ == packs_upscaled) {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(folder_, error), end; !error && it != end; it.increment(error)) {
            if (it->is_directory(error) && it->path().filename() == "dump") {
                it.disable_recursion_pending();
                continue;
            }
            uint64_t hash = 0;
            auto extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (it->is_regular_file(error) && extension == ".png" && hex_name(it->path(), hash))
                files_.emplace(hash, it->path());
        }
        std::cout << "[hd-textures] mode=packs files=" << files_.size() << " folder=" << folder_.string() << std::endl;
    } else if (mode_ != off) {
        std::cout << "[hd-textures] mode=" << (mode_ == upscaled ? "upscaled" : "dump") << " factor=" << factor_ << std::endl;
    }
    if (mode_ != off && !worker_.joinable())
        worker_ = std::thread([this] { work(); });
}

std::shared_ptr<const Image> Library::find(uint64_t hash) const {
    std::lock_guard lock(mutex_);
    const auto found = ready_.find(hash);
    return found == ready_.end() ? nullptr : found->second;
}

bool Library::known(uint64_t hash) const {
    std::lock_guard lock(mutex_);
    return seen_.count(hash) != 0;
}

void Library::submit(uint64_t hash, Image source) {
    {
        std::lock_guard lock(mutex_);
        if (mode_ == off || !seen_.insert(hash).second)
            return;
        if (mode_ == packs && !files_.count(hash)) {
            ++counts_.missing;
            return;
        }
        queue_.emplace_back(hash, std::move(source));
    }
    wake_.notify_one();
}

void Library::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&] { return queue_.empty() && !busy_; });
}

bool Library::has_packs() const {
    std::lock_guard lock(mutex_);
    return !files_.empty();
}

Library::Counts Library::counts() const {
    std::lock_guard lock(mutex_);
    return counts_;
}

void Library::work() {
    for (;;) {
        std::pair<uint64_t, Image> job;
        unsigned mode, factor;
        std::filesystem::path folder, file;
        {
            std::unique_lock lock(mutex_);
            busy_ = false;
            idle_.notify_all();
            wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (stop_)
                return;
            job = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
            mode = mode_;
            factor = factor_;
            folder = folder_;
            if (const auto found = files_.find(job.first); found != files_.end())
                file = found->second;
        }
        std::shared_ptr<Image> result;
        bool packed = false, dumped = false;
        try {
            if (mode == dump) {
                const auto path = folder / "dump" / (hash_name(job.first) + ".png");
                std::error_code error;
                if (!std::filesystem::exists(path, error))
                    dumped = save_image(path, job.second);
            } else {
                Image image;
                if (!file.empty() && load_image(file, image)) {
                    result = std::make_shared<Image>(std::move(image));
                    packed = true;
                } else if (mode == upscaled || mode == packs_upscaled) {
                    result = std::make_shared<Image>(upscale(job.second, factor));
                }
            }
        } catch (const std::exception &error) {
            std::cerr << "[hd-textures] " << hash_name(job.first) << ": " << error.what() << std::endl;
            result.reset();
        }
        std::lock_guard lock(mutex_);
        if (result && result->width && result->height)
            ready_[job.first] = std::move(result);
        if (packed)
            ++counts_.packed;
        else if (dumped)
            ++counts_.dumped;
        else if (mode == upscaled || mode == packs_upscaled)
            ++counts_.upscaled;
        else
            ++counts_.missing;
    }
}

Library &library() {
    static Library value;
    return value;
}

namespace {
std::atomic<bool> showing{true};
}

void set_shown(bool value) { showing.store(value, std::memory_order_relaxed); }

bool shown() { return showing.load(std::memory_order_relaxed); }

}
