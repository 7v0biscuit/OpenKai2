// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <iosfwd>
#include "frame_pacing.h"

namespace openkai2 {
// Presentation may wait on the raster lock. Keep that work off the window
// thread so event polling and close requests continue while rendering is busy.
class FramePresenter {
public:
    explicit FramePresenter(std::function<void(uint64_t)> render);
    ~FramePresenter();
    FramePresenter(const FramePresenter&)=delete;
    FramePresenter& operator=(const FramePresenter&)=delete;
    void request(uint64_t tick);
    uint64_t completed() const { return completed_.load(std::memory_order_acquire); }
    uint64_t rendered() const { return rendered_.load(std::memory_order_relaxed); }
    uint64_t work_us() const { return workUs_.load(std::memory_order_relaxed); }
    void report_timing(std::ostream&) const;
    void rethrow_if_failed();
    void stop();
private:
    void run();
    std::function<void(uint64_t)> render_;
    mutable std::mutex mutex_;
    FramePacing pacing_;
    FramePacing samplePacing_;
    uint64_t beganUs_=0,lastSampleUs_=0;
    std::condition_variable ready_;
    bool stopping_=false,haveTick_=false;
    uint64_t tick_=0,requested_=0;
    std::exception_ptr failure_;
    std::atomic<uint64_t> completed_{0},workUs_{0},rendered_{0};
    std::thread worker_;
};
// Bind on the guest executor thread. VBlank can then request a completed
// field independently of window swaps or the selected display refresh limit.
class FieldPresentationScope {
public:
    explicit FieldPresentationScope(FramePresenter&);
    ~FieldPresentationScope();
    FieldPresentationScope(const FieldPresentationScope&)=delete;
    FieldPresentationScope& operator=(const FieldPresentationScope&)=delete;
private:
    FramePresenter* previous_;
};
void present_guest_field(uint64_t tick);
void verify_frame_presenter();
}
