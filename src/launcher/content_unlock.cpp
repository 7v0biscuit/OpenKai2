// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/content_unlock.h"
#include "launcher/capsule_catalog.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace openkai2::launcher {
namespace {
constexpr size_t saveBytes = 0xab0u;
constexpr std::array<uint8_t, 8> signature = {'#', 'S', 'P', 'F', ' ', '1', '.', '0'};
constexpr std::array<unsigned, 29> characterBits = {0, 2,  28, 26, 3,  27, 5,  6,  4,  7,  8,  24, 21, 33, 35,
                                                    9, 10, 12, 11, 15, 16, 17, 18, 25, 20, 32, 29, 30, 31};
void validate(const std::vector<uint8_t> &data) {
    if (data.size() != saveBytes || !std::equal(signature.begin(), signature.end(), data.begin()))
        throw std::runtime_error("This is not a supported Budokai 2 USA progress save");
}
struct SaveRandom {
    uint32_t state;
    uint32_t next(unsigned bits) {
        for (unsigned i = 0; i < bits; ++i)
            state = (state << 1u) | ((state >> 31u) ^ ((state >> 4u) & 1u));
        return state - 1u;
    }
};
SaveRandom random_for(const std::vector<uint8_t> &b) {
    SaveRandom r{uint32_t(b[12]) | (uint32_t(b[13]) << 8u) | (uint32_t(b[14]) << 16u) |
                 (uint32_t(b[15]) << 24u)};
    r.next(32u);
    return r;
}
std::array<uint8_t, 16> sum(const std::vector<uint8_t> &b, size_t start) {
    std::array<uint8_t, 16> result{};
    for (size_t i = start; i < b.size(); ++i)
        result[(i - start) & 15u] = uint8_t(result[(i - start) & 15u] + b[i]);
    return result;
}
}
std::vector<uint8_t> decode_progress_save(const std::vector<uint8_t> &encrypted) {
    validate(encrypted);
    const auto outer = sum(encrypted, 0x20u);
    if (!std::equal(outer.begin(), outer.end(), encrypted.begin() + 0x10u))
        throw std::runtime_error("Save checksum failed; the original save was kept");
    auto data = encrypted;
    auto random = random_for(data);
    uint8_t previous = 0;
    for (size_t i = 0x20u; i < data.size(); ++i) {
        const auto noise = uint8_t(random.next((previous & 15u) + 8u));
        previous = data[i] = uint8_t(data[i] + previous - noise);
    }
    const auto inner = sum(data, 0x30u);
    if (!std::equal(inner.begin(), inner.end(), data.begin() + 0x20u))
        throw std::runtime_error("Save integrity check failed; the original save was kept");
    return data;
}
std::vector<uint8_t> encode_progress_save(const std::vector<uint8_t> &decoded) {
    validate(decoded);
    auto data = decoded;
    const auto inner = sum(data, 0x30u);
    std::copy(inner.begin(), inner.end(), data.begin() + 0x20u);
    auto random = random_for(data);
    uint8_t previous = 0;
    for (size_t i = 0x20u; i < data.size(); ++i) {
        const auto noise = uint8_t(random.next((previous & 15u) + 8u)), plain = data[i];
        data[i] = uint8_t(plain - previous + noise);
        previous = plain;
    }
    const auto outer = sum(data, 0x20u);
    std::copy(outer.begin(), outer.end(), data.begin() + 0x10u);
    return data;
}
bool unlock_progress_content(std::vector<uint8_t> &data) {
    validate(data);
    bool changed = false;
    for (const auto &range : capsule_ranges)
        for (unsigned item = range.first; item <= range.last; ++item) {
            auto &count = data[0x89eu + item];
            if (count < range.copies) { count = uint8_t(range.copies); changed = true; }
        }
    if (data[0xb0u] < 2u) { data[0xb0u] = 2u; changed = true; }
    for (unsigned i = 0; i < characterBits.size(); ++i) {
        const auto bit = characterBits[i];
        auto &flags = data[0x90u + bit / 8u];
        const auto mask = uint8_t(1u << (bit & 7u));
        if (!(flags & mask)) {
            flags |= mask;
            changed = true;
        }
    }
    for (unsigned bit = 0; bit < 9u; ++bit) {
        auto &flags = data[0xb4u + bit / 8u];
        const auto mask = uint8_t(1u << (bit & 7u));
        if (!(flags & mask)) {
            flags |= mask;
            changed = true;
        }
    }
    return changed;
}
}
