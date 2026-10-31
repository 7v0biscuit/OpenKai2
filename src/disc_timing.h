// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openkai2 {
class DiscTiming {
public:
    static constexpr uint64_t clock = 36'864'000;
    static constexpr uint32_t layer_sectors = 2'295'104;
    static constexpr uint32_t edge_sectors_per_second = 2'700;
    static constexpr double inner_radius = 24.0, outer_radius = 58.0;
    static constexpr uint32_t contiguous_distance = 16, full_seek_distance = 14'764;
    static constexpr uint64_t short_seek = clock * 30 / 1000, full_seek = clock * 100 / 1000;

    static double speed(uint32_t lsn) {
        const double position = double(std::min(lsn, layer_sectors)) / layer_sectors;
        const double r2 = inner_radius * inner_radius, R2 = outer_radius * outer_radius;
        return std::sqrt(r2 + (R2 - r2) * position) / outer_radius;
    }
    static uint64_t sector_cycles(uint32_t lsn) {
        return uint64_t(std::llround(double(clock) / (edge_sectors_per_second * speed(lsn))));
    }
    static uint64_t transfer(uint32_t lsn, uint32_t sectors) {
        return sectors ? uint64_t(sectors) * sector_cycles(lsn + sectors / 2) : 0;
    }
    static uint64_t seek(uint32_t from, uint32_t to) {
        const uint32_t distance = from > to ? from - to : to - from;
        if (distance == 0)
            return 0;
        if (distance < contiguous_distance)
            return distance * sector_cycles(to);
        return distance < full_seek_distance ? short_seek : full_seek;
    }

    void reset() { *this = DiscTiming{}; }
    bool busy(uint64_t now) const { return now < ready_; }
    uint64_t ready() const { return ready_; }
    uint32_t head() const { return head_; }
    uint64_t read(uint64_t now, uint32_t lsn, uint32_t sectors) {
        const uint64_t start = std::max(now, ready_);
        ready_ = start + seek(head_, lsn) + transfer(lsn, sectors);
        head_ = lsn + sectors;
        return ready_;
    }
    uint64_t seek_to(uint64_t now, uint32_t lsn) {
        const uint64_t start = std::max(now, ready_);
        ready_ = start + seek(head_, lsn);
        head_ = lsn;
        return ready_;
    }

private:
    uint64_t ready_ = 0;
    uint32_t head_ = 0;
};
}
