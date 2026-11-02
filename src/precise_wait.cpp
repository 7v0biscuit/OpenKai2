// SPDX-License-Identifier: GPL-3.0-only
#include "precise_wait.h"
#include "simulation_pacing.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace openkai2 {
void precise_sleep(std::chrono::steady_clock::duration duration) noexcept {
    using namespace std::chrono;
    if (duration <= steady_clock::duration::zero())
        return;
#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    thread_local HANDLE timer = [] {
        HANDLE created = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        return created ? created : CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }();
    LARGE_INTEGER due{};
    due.QuadPart = -std::max<long long>(1, duration_cast<nanoseconds>(duration).count() / 100);
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
        return;
    }
#endif
    std::this_thread::sleep_for(duration);
}

void record_pacing_oversleep(std::chrono::steady_clock::duration late) noexcept {
    const auto us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(late).count());
    for (auto seen = pacing_oversleep.load(std::memory_order_relaxed);
         us > seen && !pacing_oversleep.compare_exchange_weak(seen, us, std::memory_order_relaxed);) {
    }
}
}
