// SPDX-License-Identifier: GPL-3.0-only
#include "frame_presenter.h"
#include <chrono>
#include <utility>

namespace openkai2 {
FramePresenter::FramePresenter(std::function<void(uint64_t)> render)
    :render_(std::move(render)),worker_([this]{run();}) {}
FramePresenter::~FramePresenter() { stop(); }
void FramePresenter::request(uint64_t tick) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || (haveTick_ && tick_==tick)) return;
        tick_=tick;haveTick_=true;++requested_;
    }
    ready_.notify_one();
}
void FramePresenter::rethrow_if_failed() {
    std::exception_ptr error;
    { std::lock_guard<std::mutex> lock(mutex_);error=failure_; }
    if (error) std::rethrow_exception(error);
}
void FramePresenter::stop() {
    {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
    ready_.notify_one();
    if (worker_.joinable()) worker_.join();
}
void FramePresenter::run() {
    uint64_t processed=0;
    for (;;) {
        uint64_t generation,tick;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock,[&]{return stopping_ || requested_!=processed;});
            if (stopping_) return;
            generation=requested_;tick=tick_;
        }
        const auto began=std::chrono::steady_clock::now();
        try { render_(tick); }
        catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            failure_=std::current_exception();stopping_=true;
            return;
        }
        workUs_.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-began).count()),std::memory_order_relaxed);
        processed=generation;
        rendered_.fetch_add(1u,std::memory_order_relaxed);
        completed_.store(processed,std::memory_order_release);
    }
}
}
