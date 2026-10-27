// SPDX-License-Identifier: GPL-3.0-only
#include "frame_presenter.h"
#include <chrono>
#include <utility>
#include <ostream>
#include <iostream>
#include <sstream>

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
void FramePresenter::report_timing(std::ostream& output) const {
    FramePacingSummary timing;
    {std::lock_guard<std::mutex> lock(mutex_);timing=pacing_.summary();}
    output<<"[performance:frames] completed="<<timing.frames<<" measured_fps="<<timing.fps()
          <<" interval_p50_us_upper="<<timing.p50Us<<" interval_p95_us_upper="<<timing.p95Us
          <<" interval_p99_us_upper="<<timing.p99Us<<" max_interval_us="<<timing.maxUs
          <<" intervals_over_25ms="<<timing.late<<" average_render_us="<<(timing.frames?timing.workUs/timing.frames:0u)
          <<" coalesced_requests="<<(completed()-rendered())<<'\n';
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
        const auto finished=std::chrono::steady_clock::now();
        const auto work=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(finished-began).count());
        workUs_.fetch_add(work,std::memory_order_relaxed);
        FramePacingSummary sample;uint64_t sampleElapsed=0;
        {std::lock_guard<std::mutex> lock(mutex_);
            const auto now=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(finished.time_since_epoch()).count());
            pacing_.record(now,work);samplePacing_.record(now,work);
            if(!beganUs_)beganUs_=lastSampleUs_=now;
            if(now-lastSampleUs_>=5000000u){sample=samplePacing_.summary();sampleElapsed=(now-beganUs_)/1000u;
                samplePacing_.restart(now);lastSampleUs_=now;}
        }
        if(sample.frames){
            std::ostringstream line;
            line<<"[performance:sample] elapsed_ms="<<sampleElapsed<<" completed="<<sample.frames
                <<" measured_fps="<<sample.fps()<<" interval_p95_us_upper="<<sample.p95Us
                <<" interval_p99_us_upper="<<sample.p99Us<<" max_interval_us="<<sample.maxUs
                <<" intervals_over_25ms="<<sample.late<<'\n';
            std::cout<<line.str();
        }
        processed=generation;
        rendered_.fetch_add(1u,std::memory_order_relaxed);
        completed_.store(processed,std::memory_order_release);
    }
}
}
