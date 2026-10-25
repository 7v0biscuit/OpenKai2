// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <span>
namespace openkai2 {
void start_host_audio(const void* owner,bool ready);
void stop_host_audio(const void* owner);
void submit_host_audio(const void* owner,unsigned core,std::span<const int16_t> samples,uint32_t rate);
int test_host_audio();
}
