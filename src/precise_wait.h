// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace openkai2 {
void precise_sleep(std::chrono::steady_clock::duration duration) noexcept;
void record_pacing_oversleep(std::chrono::steady_clock::duration late) noexcept;
template <class Predicate>
bool precise_wait_until(std::condition_variable &cv, std::unique_lock<std::mutex> &lock,
                        std::chrono::steady_clock::time_point deadline, Predicate ready) {
#ifdef _WIN32
    using namespace std::chrono;
    if (deadline == steady_clock::time_point::max()) {
        cv.wait(lock, ready);
        return true;
    }
    bool slept = false;
    while (!ready()) {
        const auto now = steady_clock::now();
        if (now >= deadline) {
            if (slept)
                record_pacing_oversleep(now - deadline);
            return ready();
        }
        slept = true;
        const auto left = deadline - now;
        if (left > milliseconds(25)) {
            cv.wait_until(lock, deadline - milliseconds(20), ready);
        } else if (left > microseconds(1500)) {
            lock.unlock();
            precise_sleep(std::min<steady_clock::duration>(left - microseconds(1000), milliseconds(2)));
            lock.lock();
        } else {
            lock.unlock();
            std::this_thread::yield();
            lock.lock();
        }
    }
    return true;
#else
    return cv.wait_until(lock, deadline, ready);
#endif
}
}
