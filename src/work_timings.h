// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <chrono>
#include <cstdint>
namespace openkai2 {
enum class RuntimeWork : unsigned { guest,dmaSubmit,dmaPending,vu0,vu1,path1,gifPath23,gpuWait,gsFence,count };
void record_runtime_work(RuntimeWork,uint64_t) noexcept;
void flush_runtime_work() noexcept;
class RuntimeWorkTimer {
    RuntimeWork work_;
    bool active_;
    std::chrono::steady_clock::time_point start_;
public:
    explicit RuntimeWorkTimer(RuntimeWork work,bool active=true) noexcept:work_(work),active_(active),
        start_(active ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}){}
    ~RuntimeWorkTimer(){if(active_)record_runtime_work(work_,uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start_).count()));}
};
}
