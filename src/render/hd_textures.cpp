// SPDX-License-Identifier: GPL-3.0-only
#include "hd_textures.h"
#include <atomic>
#include "raylib.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace openkai2::hd {
namespace {

struct Plane {
    int width = 0, height = 0;
    std::vector<std::array<float, 4>> values;
    std::vector<float> light;
    std::array<float, 4> &at(int x, int y) { return values[size_t(y) * size_t(width) + size_t(x)]; }
    float &level(int x, int y) { return light[size_t(y) * size_t(width) + size_t(x)]; }
};

std::array<float, 4> cubic(const std::array<float, 4> &a, const std::array<float, 4> &b, const std::array<float, 4> &c,
                           const std::array<float, 4> &d) {
    std::array<float, 4> out;
    for (unsigned k = 0; k < 4u; ++k) {
        const float value = (9.0f * (b[k] + c[k]) - a[k] - d[k]) / 16.0f;
        out[k] = std::clamp(value, std::min(b[k], c[k]), std::max(b[k], c[k]));
    }
    return out;
}

std::array<float, 4> directional(const std::array<float, 4> &along, const std::array<float, 4> &across, float alongChange,
                                 float acrossChange) {
    if ((1.0f + acrossChange) > 1.15f * (1.0f + alongChange))
        return along;
    if ((1.0f + alongChange) > 1.15f * (1.0f + acrossChange))
        return across;
    const auto fifth = [](float x) { x /= 64.0f; return x * x * x * x * x; };
    const float a = 1.0f / (1.0f + fifth(alongChange)), b = 1.0f / (1.0f + fifth(acrossChange));
    std::array<float, 4> out;
    for (unsigned k = 0; k < 4u; ++k)
        out[k] = (a * along[k] + b * across[k]) / (a + b);
    return out;
}

float brightness(const std::array<float, 4> &v) { return 0.299f * v[0] + 0.587f * v[1] + 0.114f * v[2] + v[3]; }

Image double_size(const Image &source) {
    const int w = int(source.width), h = int(source.height), pad = 4;
    Plane grid;
    grid.width = 2 * (w + 2 * pad);
    grid.height = 2 * (h + 2 * pad);
    grid.values.assign(size_t(grid.width) * size_t(grid.height), {});
    grid.light.assign(grid.values.size(), 0.0f);
    for (int y = 0; y < h + 2 * pad; ++y)
        for (int x = 0; x < w + 2 * pad; ++x) {
            const uint32_t texel = source.texels[size_t(std::clamp(y - pad, 0, h - 1)) * size_t(w) + size_t(std::clamp(x - pad, 0, w - 1))];
            const float alpha = float(texel >> 24u);
            auto &v = grid.at(2 * x, 2 * y);
            for (unsigned k = 0; k < 3u; ++k)
                v[k] = float((texel >> (8u * k)) & 255u) * alpha / 255.0f;
            v[3] = alpha;
            grid.level(2 * x, 2 * y) = brightness(v);
        }
    const auto change = [&](int x, int y, int dx, int dy) { return std::abs(grid.level(x, y) - grid.level(x + dx, y + dy)); };
    for (int y = 3; y + 3 < grid.height; y += 2)
        for (int x = 3; x + 3 < grid.width; x += 2) {
            float rising = 0.0f, falling = 0.0f;
            for (int j = -3; j <= 1; j += 2)
                for (int i = -3; i <= 1; i += 2) {
                    rising += change(x + i, y + j + 2, 2, -2);
                    falling += change(x + i, y + j, 2, 2);
                }
            const auto alongFalling = cubic(grid.at(x - 3, y - 3), grid.at(x - 1, y - 1), grid.at(x + 1, y + 1), grid.at(x + 3, y + 3));
            const auto alongRising = cubic(grid.at(x - 3, y + 3), grid.at(x - 1, y + 1), grid.at(x + 1, y - 1), grid.at(x + 3, y - 3));
            grid.at(x, y) = directional(alongFalling, alongRising, falling, rising);
            grid.level(x, y) = brightness(grid.at(x, y));
        }
    for (int y = 6; y + 6 < grid.height; ++y)
        for (int x = 6 + ((y + 1) & 1); x + 6 < grid.width; x += 2) {
            float across = change(x - 3, y, 2, 0) + change(x - 1, y, 2, 0) + change(x + 1, y, 2, 0) + change(x - 2, y - 1, 2, 0) +
                           change(x, y - 1, 2, 0) + change(x - 2, y + 1, 2, 0) + change(x, y + 1, 2, 0) + change(x - 1, y - 2, 2, 0) +
                           change(x - 1, y + 2, 2, 0);
            float down = change(x, y - 3, 0, 2) + change(x, y - 1, 0, 2) + change(x, y + 1, 0, 2) + change(x - 1, y - 2, 0, 2) +
                         change(x - 1, y, 0, 2) + change(x + 1, y - 2, 0, 2) + change(x + 1, y, 0, 2) + change(x - 2, y - 1, 0, 2) +
                         change(x + 2, y - 1, 0, 2);
            const auto horizontal = cubic(grid.at(x - 3, y), grid.at(x - 1, y), grid.at(x + 1, y), grid.at(x + 3, y));
            const auto vertical = cubic(grid.at(x, y - 3), grid.at(x, y - 1), grid.at(x, y + 1), grid.at(x, y + 3));
            grid.at(x, y) = directional(horizontal, vertical, across, down);
        }
    Image out;
    out.width = source.width * 2u;
    out.height = source.height * 2u;
    out.texels.resize(size_t(out.width) * out.height);
    const int rows = int(out.height) + 3;
    std::vector<std::array<float, 4>> across(size_t(rows) * out.width);
    for (int r = 0; r < rows; ++r) {
        const int gy = r + 2 * pad - 2;
        for (int x = 0; x < int(out.width); ++x) {
            const int gx = x + 2 * pad;
            across[size_t(r) * out.width + size_t(x)] = cubic(grid.at(gx - 2, gy), grid.at(gx - 1, gy), grid.at(gx, gy), grid.at(gx + 1, gy));
        }
    }
    for (int y = 0; y < int(out.height); ++y)
        for (int x = 0; x < int(out.width); ++x) {
            const auto row = [&](int r) -> const std::array<float, 4> & { return across[size_t(y + r) * out.width + size_t(x)]; };
            const auto v = cubic(row(0), row(1), row(2), row(3));
            const float alpha = std::clamp(v[3], 0.0f, 255.0f);
            uint32_t texel = uint32_t(alpha + 0.5f) << 24u;
            for (unsigned k = 0; k < 3u; ++k) {
                const float color = alpha > 0.0f ? v[k] * 255.0f / alpha : 0.0f;
                texel |= uint32_t(std::clamp(color, 0.0f, 255.0f) + 0.5f) << (8u * k);
            }
            out.texels[size_t(y) * out.width + size_t(x)] = texel;
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
    if (size_t(source.width) * source.height > 256u * 256u)
        factor = std::min(factor, 2u);
    for (unsigned scale = 1u; scale < factor; scale *= 2u)
        image = double_size(image);
    image.generated = true;
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
    for (auto &worker : workers_)
        if (worker.joinable())
            worker.join();
}

void Library::configure(unsigned mode, const std::filesystem::path &folder, unsigned factor) {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&] { return queue_.empty() && busy_ == 0u; });
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
    if (mode_ != off && workers_.empty()) {
        const unsigned count = std::clamp(std::thread::hardware_concurrency() / 4u, 1u, 3u);
        for (unsigned i = 0; i < count; ++i)
            workers_.emplace_back([this] { work(); });
    }
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
    idle_.wait(lock, [&] { return queue_.empty() && busy_ == 0u; });
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
    bool working = false;
    for (;;) {
        std::pair<uint64_t, Image> job;
        unsigned mode, factor;
        std::filesystem::path folder, file;
        {
            std::unique_lock lock(mutex_);
            if (working) {
                --busy_;
                working = false;
            }
            idle_.notify_all();
            wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (stop_)
                return;
            job = std::move(queue_.front());
            queue_.pop_front();
            ++busy_;
            working = true;
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
