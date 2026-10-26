// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <chrono>

namespace openkai2 {
// A slow host must not replay a backlog of vertical blanks in a burst. Only
// the host deadline is rebased; guest cycles and interrupt order stay intact.
inline std::chrono::steady_clock::time_point
field_pacing_anchor(std::chrono::steady_clock::time_point scheduled,
                    std::chrono::steady_clock::time_point now) noexcept {
    constexpr auto period = std::chrono::microseconds(16667);
    return now - scheduled > period ? now : scheduled;
}
} // namespace openkai2
