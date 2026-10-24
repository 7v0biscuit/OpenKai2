// SPDX-License-Identifier: GPL-3.0-only
#include "guest_timing.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <iostream>
#include <sstream>
namespace openkai2 {
namespace {
std::atomic<bool> enabled{false};
#ifdef _WIN32
using Owner=DWORD;
Owner current_owner() noexcept{return GetCurrentThreadId();}
#else
using Owner=std::thread::id;
Owner current_owner() noexcept{return std::this_thread::get_id();}
#endif
std::atomic<Owner> owner{};
std::mutex configuration;
bool owned() noexcept{return owner.load(std::memory_order_relaxed)==current_owner();}
constexpr unsigned interval=256;
struct Entry { uint32_t target=0;unsigned work=0;uint64_t samples=0,total=0,maximum=0; };
struct Timings {
    std::array<Entry,1024> entries{};
    std::array<uint64_t,3> calls{};
    std::array<bool,3> active{};
    uint64_t discarded=0,draws=0;
    std::chrono::steady_clock::time_point reported=std::chrono::steady_clock::now();
};
// Only the registered EE executor touches this state. Avoid emulated TLS
// lookups on each Windows guest call; other threads retain full GS history.
Timings timings;
void report(std::chrono::steady_clock::time_point now) {
    std::array<const Entry*,1024> rows{};size_t count=0;
    for(const auto& row:timings.entries)if(row.samples)rows[count++]=&row;
    if(!count){timings.reported=now;return;}
    const auto used=std::min<size_t>(count,8u);
    std::partial_sort(rows.begin(),rows.begin()+used,rows.begin()+count,
        [](const Entry* a,const Entry* b){return a->total>b->total;});
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-timings.reported).count();
    std::ostringstream out;
    out<<"[guest:timing] window_ms="<<elapsed<<" sampling=1/"<<interval
       <<" scope=inclusive draw_history=1/64 omitted_keys="<<timings.discarded<<'\n';
    constexpr const char* names[]={"dispatch","iop","vu0"};
    for(size_t i=0;i<used;++i){const auto& row=*rows[i];
        out<<"[guest:hotspot] stage="<<names[row.work]<<" target=0x"<<std::hex<<row.target<<std::dec
           <<" samples="<<row.samples<<" sampled_us="<<row.total/1000u
           <<" max_us="<<row.maximum/1000u<<'\n';}
    std::cerr<<out.str();
    timings.entries={};timings.discarded=0;timings.reported=now;
}
}
bool sample_draw_history() noexcept {
    if(!enabled.load(std::memory_order_acquire) || !owned())return true;
    return (++timings.draws&63u)==0u;
}
void enable_guest_timings(bool value) noexcept {
    std::lock_guard lock(configuration);
    if(value){
        if(enabled.load(std::memory_order_relaxed))return;
        timings=Timings{};owner.store(current_owner(),std::memory_order_relaxed);
        enabled.store(true,std::memory_order_release);
    }else if(owned())enabled.store(false,std::memory_order_release);
}
uint64_t begin_guest_sample(GuestWork work) noexcept {
    if(!enabled.load(std::memory_order_acquire) || !owned())return 0;
    const auto at=unsigned(work);
    if(timings.active[at] || (++timings.calls[at]&(interval-1u)))return 0;
    timings.active[at]=true;
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void finish_guest_sample(GuestWork kind,uint32_t target,uint64_t started) noexcept {
    const auto now=std::chrono::steady_clock::now();
    const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count())-started;
    const auto work=unsigned(kind);timings.active[work]=false;
    const auto hash=((target>>2u)*2654435761u+work)&1023u;
    bool recorded=false;
    for(unsigned offset=0;offset<timings.entries.size();++offset){auto& entry=timings.entries[(hash+offset)&1023u];
        if(entry.samples && (entry.target!=target || entry.work!=work))continue;
        entry.target=target;entry.work=work;++entry.samples;entry.total+=elapsed;
        entry.maximum=std::max(entry.maximum,elapsed);recorded=true;break;}
    if(!recorded)++timings.discarded;
    if(now-timings.reported>=std::chrono::seconds(5)){try {report(now);}catch(...) {}}
}
void flush_guest_timings() noexcept {
    try {if(owned())report(std::chrono::steady_clock::now());}catch(...) {}
}
}
