// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <span>
namespace openkai2 {
struct HostAudioOptions {
    bool enabled = true;
    unsigned volume = 100; // percent
};
void set_host_audio_options(const HostAudioOptions &options);
const HostAudioOptions &host_audio_options() noexcept;
void start_host_audio(const void* owner,bool deviceOpen);
void stop_host_audio(const void* owner);
void submit_host_audio(const void* owner,unsigned core,std::span<const int16_t> samples,uint32_t rate);
int test_host_audio();
}
