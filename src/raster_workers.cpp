// SPDX-License-Identifier: GPL-3.0-only
#include "raster_workers.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace openkai2 {
namespace { std::atomic<unsigned> workerLimit{4u};std::atomic<uint64_t> parallelBatches{0}; }
struct RasterWorkers::State {
    std::mutex submission,mutex;
    std::condition_variable changed,finished;
    std::vector<std::thread> threads;
    std::function<void(unsigned)> task;
    std::array<std::exception_ptr,4> failures{};
    uint64_t generation=0;
    unsigned capacity=1,active=1,pending=0;
    bool stopping=false;
    void execute(unsigned index) noexcept {
        try { task(index); } catch(...) { failures[index]=std::current_exception(); }
    }
    void worker(unsigned index) {
        uint64_t observed=0;
        std::unique_lock lock(mutex);
        for(;;) {
            changed.wait(lock,[&] { return stopping || generation!=observed; });
            if(stopping)return;
            observed=generation;
            if(index>=active)continue;
            lock.unlock();execute(index);lock.lock();
            if(--pending==0)finished.notify_one();
        }
    }
    ~State() {
        { std::lock_guard lock(mutex);stopping=true; }
        changed.notify_all();
        for(auto& worker:threads)worker.join();
    }
};
RasterWorkers::RasterWorkers(unsigned count):state_(std::make_unique<State>()) {
    state_->capacity=std::clamp(count,1u,4u);
    auto* state=state_.get();
    for(unsigned index=1;index<state_->capacity;++index)
        state_->threads.emplace_back([state,index] { state->worker(index); });
}
RasterWorkers::~RasterWorkers()=default;
unsigned RasterWorkers::count() const { return state_->capacity; }
void RasterWorkers::run(unsigned count,const std::function<void(unsigned)>& task) {
    auto& state=*state_;
    std::lock_guard submission(state.submission);
    const auto active=std::clamp(count,1u,state.capacity);
    if(active==1u) { task(0);return; }
    {
        std::lock_guard lock(state.mutex);
        state.task=task;state.active=active;state.pending=active-1u;state.failures={};++state.generation;
    }
    state.changed.notify_all();state.execute(0);
    {
        std::unique_lock lock(state.mutex);
        state.finished.wait(lock,[&] { return state.pending==0; });
        state.task={};
    }
    for(const auto& failure:state.failures)if(failure)std::rethrow_exception(failure);
    ++parallelBatches;
}
RasterWorkers& raster_workers() {
    static RasterWorkers workers(std::max(1u,std::min(4u,std::thread::hardware_concurrency())));
    return workers;
}
unsigned set_raster_worker_limit(unsigned value) { return workerLimit.exchange(std::clamp(value,1u,4u)); }
unsigned raster_worker_limit() { return workerLimit.load(); }
uint64_t raster_parallel_batches() { return parallelBatches.load(); }
void verify_raster_workers() {
    RasterWorkers workers(4);
    for(unsigned iteration=0;iteration<32u;++iteration) {
        std::array<unsigned,4> counts{};
        const unsigned active=1u+iteration%4u;
        workers.run(active,[&](unsigned index) { ++counts[index]; });
        for(unsigned i=0;i<4u;++i)if(counts[i]!=(i<active ? 1u : 0u))
            throw std::runtime_error("Raster workers lost or duplicated a partition");
    }
    bool caught=false;std::atomic<unsigned> completed=0;
    try { workers.run(4,[&](unsigned index) { ++completed;if(index==2u)throw std::runtime_error("worker failure"); }); }
    catch(const std::runtime_error&) { caught=true; }
    if(!caught || completed!=4u)throw std::runtime_error("Raster worker error did not join the complete batch");
    completed=0;workers.run(4,[&](unsigned) { ++completed; });
    if(completed!=4u)throw std::runtime_error("Raster workers did not recover after an exception");
}
}
