// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <chrono>

namespace openkai2 {
inline std::chrono::steady_clock::time_point
field_pacing_anchor(std::chrono::steady_clock::time_point scheduled,
                    std::chrono::steady_clock::time_point now) noexcept {
    constexpr auto period = std::chrono::microseconds(16667);
    return now - scheduled > period ? now : scheduled;
}
}
