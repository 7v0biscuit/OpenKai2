// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace openkai2 {
enum class GuestWork : unsigned { dispatch, iop, vu0 };
uint64_t begin_guest_sample(GuestWork work) noexcept;
void finish_guest_sample(GuestWork work,uint32_t target,uint64_t started) noexcept;
class GuestTimingScope {
public:
    GuestTimingScope(GuestWork work,uint32_t target) noexcept
        :work_(work),target_(target),started_(begin_guest_sample(work)){}
    ~GuestTimingScope(){if(started_)finish_guest_sample(work_,target_,started_);}
    GuestTimingScope(const GuestTimingScope&)=delete;
    GuestTimingScope& operator=(const GuestTimingScope&)=delete;
private:
    GuestWork work_;
    uint32_t target_;
    uint64_t started_;
};
void enable_guest_timings(bool enabled) noexcept;
bool sample_draw_history() noexcept;
void flush_guest_timings() noexcept;
}
