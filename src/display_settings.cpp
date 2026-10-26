// SPDX-License-Identifier: GPL-3.0-only
#include "display_settings.h"
#include <atomic>
#include <charconv>
#include <stdexcept>

namespace openkai2 {
namespace { std::atomic<unsigned> limit{60u}; }
unsigned parse_window_fps_limit(std::string_view value) {
    if(value.empty())throw std::runtime_error("FPS limit cannot be empty");
    unsigned result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result>240u)
        throw std::runtime_error("FPS limit must be 0 (unlimited) or an integer from 1 to 240");
    return result;
}
void set_window_fps_limit(unsigned value) {
    if(value>240u)throw std::runtime_error("FPS limit cannot exceed 240; use 0 for unlimited");
    limit.store(value,std::memory_order_relaxed);
}
unsigned window_fps_limit() noexcept {return limit.load(std::memory_order_relaxed);}
}
