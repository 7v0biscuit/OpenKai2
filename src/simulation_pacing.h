// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace openkai2 {
struct FieldLateness {
    std::atomic<uint64_t> fields{0}, late{0}, longest{0};
};
inline FieldLateness field_lateness;
inline std::atomic<uint64_t> pacing_oversleep{0};
inline void record_field_lateness(std::chrono::steady_clock::duration behind) noexcept {
    const auto us = behind.count() > 0 ? uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(behind).count()) : 0u;
    field_lateness.fields.fetch_add(1u, std::memory_order_relaxed);
    if (us > 4000u)
        field_lateness.late.fetch_add(1u, std::memory_order_relaxed);
    for (auto seen = field_lateness.longest.load(std::memory_order_relaxed);
         us > seen && !field_lateness.longest.compare_exchange_weak(seen, us, std::memory_order_relaxed);) {
    }
}
inline std::chrono::steady_clock::time_point
field_pacing_anchor(std::chrono::steady_clock::time_point scheduled,
                    std::chrono::steady_clock::time_point now) noexcept {
    constexpr auto period = std::chrono::microseconds(16667);
    record_field_lateness(now - scheduled);
    return now - scheduled > period ? now : scheduled;
}
}
