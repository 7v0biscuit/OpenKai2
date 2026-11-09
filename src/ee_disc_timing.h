// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "disc_timing.h"
#include <cstdint>

namespace openkai2 {
class EeDiscClock {
public:
    static constexpr uint64_t field_numerator = 60'000, field_denominator = 1'001;
    static constexpr uint64_t stale_ticks = 600;

    static uint64_t cycles_at(uint64_t tick) { return tick * DiscTiming::clock * field_denominator / field_numerator; }
    static uint64_t tick_at(uint64_t cycles) {
        const uint64_t divisor = DiscTiming::clock * field_denominator;
        return (cycles * field_numerator + divisor - 1) / divisor;
    }

    void enable(bool on) {
        enabled_ = on;
        reset();
    }
    bool enabled() const { return enabled_; }
    void reset() {
        timing_.reset();
        ready_ = 0;
    }
    void read(uint64_t tick, uint32_t lsn, uint32_t sectors) {
        if (!enabled_)
            return;
        settle(tick);
        ready_ = tick_at(timing_.read(cycles_at(tick), lsn, sectors));
    }
    void seek(uint64_t tick, uint32_t lsn) {
        if (!enabled_)
            return;
        settle(tick);
        ready_ = tick_at(timing_.seek_to(cycles_at(tick), lsn));
    }
    bool busy(uint64_t tick) {
        settle(tick);
        return enabled_ && tick < ready_;
    }
    uint64_t ready() const { return ready_; }

private:
    void settle(uint64_t tick) {
        if (ready_ > tick + stale_ticks)
            reset();
    }
    DiscTiming timing_;
    uint64_t ready_ = 0;
    bool enabled_ = true;
};
}
