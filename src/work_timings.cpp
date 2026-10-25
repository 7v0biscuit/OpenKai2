// SPDX-License-Identifier: GPL-3.0-only
#include "work_timings.h"
#include <array>
#include <atomic>
#include <iostream>
#include <sstream>
namespace openkai2 {
namespace {
struct Total {std::atomic<uint64_t> calls{0},nanoseconds{0};};
std::array<Total,unsigned(RuntimeWork::count)> totals;
constexpr const char* names[]={"guest-executor","dma-submit","dma-pending","vu0","vu1","path1-submit"};
void report(unsigned index) noexcept {
    try {
        const auto& total=totals[index];const auto calls=total.calls.load(std::memory_order_relaxed);
        if(!calls)return;
        std::ostringstream out;out<<"[runtime:work] stage="<<names[index]<<" calls="<<calls
            <<" host_elapsed_us="<<total.nanoseconds.load(std::memory_order_relaxed)/1000u<<" scope=inclusive\n";
        std::cerr<<out.str();
    }catch(...){}
}
}
void record_runtime_work(RuntimeWork work,uint64_t nanoseconds) noexcept {
    auto& total=totals[unsigned(work)];total.nanoseconds.fetch_add(nanoseconds,std::memory_order_relaxed);
    const auto calls=total.calls.fetch_add(1,std::memory_order_relaxed)+1u;
    if(calls>=64u && (calls&(calls-1u))==0u)report(unsigned(work));
}
void flush_runtime_work() noexcept {for(unsigned i=0;i<totals.size();++i)report(i);}
}
