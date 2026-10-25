// SPDX-License-Identifier: GPL-3.0-only
#include "frame_presenter.h"
#include "display_settings.h"
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

namespace openkai2 {
void verify_frame_presenter() {
    using namespace std::chrono_literals;
    auto require=[](bool ok,const char* reason) {
        if(!ok)throw std::runtime_error(std::string("Runtime asynchronous presentation regression: ")+reason);
    };
    const auto originalLimit=window_fps_limit();
    for(unsigned value=0;value<=240u;++value){
        require(parse_window_fps_limit(std::to_string(value))==value,"display limit parsed incorrectly");
        set_window_fps_limit(value);require(window_fps_limit()==value,"display limit was not retained");
    }
    for(const auto value:{"", "-1", "+60", "241", "240fps", "60 ", "4294967296"}){
        bool rejected=false;try{(void)parse_window_fps_limit(value);}catch(const std::runtime_error&){rejected=true;}
        require(rejected,"invalid display limit was accepted");
    }
    bool rejected=false;try{set_window_fps_limit(241u);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"out-of-range display setting was accepted");
    set_window_fps_limit(originalLimit);
    std::mutex mutex;std::condition_variable changed;
    bool release=false,timedOut=false;
    std::vector<uint64_t> ticks;
    std::promise<void> entered,latest;
    FramePresenter presenter([&](uint64_t tick) {
        std::unique_lock<std::mutex> lock(mutex);ticks.push_back(tick);
        if(ticks.size()==1u) {
            entered.set_value();
            if(!changed.wait_for(lock,2s,[&]{return release;})) timedOut=true;
        } else if(tick==1010u) latest.set_value();
    });
    presenter.request(10u);
    require(entered.get_future().wait_for(2s)==std::future_status::ready,"worker did not begin");
    // Simulate a renderer blocked on guest drawing. Window requests must
    // still return; a thousand ticks coalesce without a stale-frame backlog.
    // Four host polls per original 60 Hz field model a 240 Hz window. Repeated
    // requests must not create extra guest fields or conversion jobs.
    for(uint64_t tick=11;tick<=1010u;++tick)for(unsigned poll=0;poll<4u;++poll)presenter.request(tick);
    {
        std::lock_guard<std::mutex> lock(mutex);
        require(!timedOut && presenter.completed()==0u,"window waited for the raster lock");
        release=true;
    }
    changed.notify_all();
    require(latest.get_future().wait_for(2s)==std::future_status::ready,"latest requested field was lost");
    presenter.stop();presenter.rethrow_if_failed();
    require(ticks==std::vector<uint64_t>({10u,1010u}) && presenter.completed()==1001u && presenter.rendered()==2u,
            "presentation duplicated ticks or processed a stale queue");
    std::promise<void> failed;
    FramePresenter throwing([&](uint64_t){failed.set_value();throw std::runtime_error("presentation-test-error");});
    throwing.request(0u);
    require(failed.get_future().wait_for(2s)==std::future_status::ready,"error test worker did not begin");
    throwing.stop();bool reported=false;
    try{throwing.rethrow_if_failed();}catch(const std::runtime_error& error){reported=std::string(error.what())=="presentation-test-error";}
    require(reported,"worker exception was lost");
}
}
