// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>

namespace openkai2 {
inline std::atomic<uint32_t> &guest_random_state() {
    static std::atomic<uint32_t> state{1u};
    return state;
}
inline void guest_srand(uint32_t seed) { guest_random_state().store(seed, std::memory_order_relaxed); }
inline int32_t guest_rand() {
    auto &state = guest_random_state();
    const uint32_t next = state.load(std::memory_order_relaxed) * 1103515245u + 12345u;
    state.store(next, std::memory_order_relaxed);
    return int32_t((next >> 16) & 0x7fffu);
}
}
