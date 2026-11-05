// SPDX-License-Identifier: GPL-3.0-only
#include "frame_presenter.h"
#include "simulation_pacing.h"
#include "work_timings.h"
#include <chrono>
#include <utility>
#include <ostream>
#include <iostream>
#include <sstream>

namespace openkai2 {
namespace { thread_local FramePresenter* fieldPresenter=nullptr; std::atomic<uint64_t> latestFrame{0}; }
uint64_t latest_presented_frame() noexcept { return latestFrame.load(std::memory_order_acquire); }
FieldPresentationScope::FieldPresentationScope(FramePresenter& presenter):previous_(fieldPresenter){fieldPresenter=&presenter;}
FieldPresentationScope::~FieldPresentationScope(){fieldPresenter=previous_;}
void present_guest_field(uint64_t tick){if(fieldPresenter)fieldPresenter->request(tick);}
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
bool FramePresenter::wait_for_frame(uint64_t previous, std::chrono::steady_clock::duration timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    frameReady_.wait_for(lock,timeout,[&]{return stopping_ || completed()!=previous;});
    return completed()!=previous;
}
void FramePresenter::rethrow_if_failed() {
    std::exception_ptr error;
    { std::lock_guard<std::mutex> lock(mutex_);error=failure_; }
    if (error) std::rethrow_exception(error);
}
void FramePresenter::stop() {
    {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
    ready_.notify_all();
    frameReady_.notify_all();
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
            frameReady_.notify_all();
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
            static uint64_t lastFields=0,lastLate=0,lastGuest=0,lastFence=0,lastDma=0,lastWorker=0,lastDraw=0,lastBatch=0,lastGpuWait=0,lastGpuBusy=0;
            const uint64_t worker=runtime_work_total_ns(RuntimeWork::gsWorker);
            const uint64_t draw=runtime_work_total_ns(RuntimeWork::gsDraw);
            const uint64_t batch=runtime_work_total_ns(RuntimeWork::gsBatch);
            const uint64_t fields=field_lateness.fields.load(std::memory_order_relaxed);
            const uint64_t lateFields=field_lateness.late.load(std::memory_order_relaxed);
            const uint64_t guest=runtime_work_total_ns(RuntimeWork::guest);
            const uint64_t fence=runtime_work_total_ns(RuntimeWork::gsFence);
            const uint64_t dma=runtime_work_total_ns(RuntimeWork::dmaPending);
            const uint64_t gpuWait=runtime_work_total_ns(RuntimeWork::gpuWait);
            const uint64_t gpuBusy=runtime_work_total_ns(RuntimeWork::gpuDevice);
            line<<"[performance:sample] elapsed_ms="<<sampleElapsed<<" completed="<<sample.frames
                <<" measured_fps="<<sample.fps()<<" interval_p95_us_upper="<<sample.p95Us
                <<" interval_p99_us_upper="<<sample.p99Us<<" max_interval_us="<<sample.maxUs
                <<" intervals_over_25ms="<<sample.late
                <<" render_avg_us="<<(sample.frames?sample.workUs/sample.frames:0u)<<" render_max_us="<<sample.maxWorkUs
                <<" game_fields="<<(fields-lastFields)<<" game_late_fields="<<(lateFields-lastLate)
                <<" game_max_late_us="<<field_lateness.longest.exchange(0,std::memory_order_relaxed)
                <<" pacing_oversleep_max_us="<<pacing_oversleep.exchange(0,std::memory_order_relaxed)
                <<" game_busy_ms="<<(guest-lastGuest)/1000000u
                <<" gs_fence_ms="<<(fence-lastFence)/1000000u<<" gs_fence_max_us="<<take_runtime_work_max_ns(RuntimeWork::gsFence)/1000u
                <<" dma_wait_ms="<<(dma-lastDma)/1000000u<<" dma_wait_max_us="<<take_runtime_work_max_ns(RuntimeWork::dmaPending)/1000u
                <<" gpu_wait_ms="<<(gpuWait-lastGpuWait)/1000000u<<" gpu_busy_ms="<<(gpuBusy-lastGpuBusy)/1000000u
                <<" gpu_wait_max_us="<<take_runtime_work_max_ns(RuntimeWork::gpuWait)/1000u
                <<" gs_busy_ms="<<(worker-lastWorker)/1000000u<<" gs_draw_ms="<<(draw-lastDraw)/1000000u
                <<" gs_batch_ms="<<(batch-lastBatch)/1000000u<<'\n';
            lastWorker=worker;lastDraw=draw;lastBatch=batch;lastGpuWait=gpuWait;lastGpuBusy=gpuBusy;
            lastFields=fields;lastLate=lateFields;lastGuest=guest;lastFence=fence;lastDma=dma;
            std::cout<<line.str();
        }
        processed=generation;
        rendered_.fetch_add(1u,std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.store(processed,std::memory_order_release);
        }
        frameReady_.notify_all();
        latestFrame.fetch_add(1u,std::memory_order_acq_rel);
    }
}
}
