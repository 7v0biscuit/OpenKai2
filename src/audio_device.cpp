// SPDX-License-Identifier: GPL-3.0-only
#include "audio_device.h"
#define MA_NO_JACK
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include "external/miniaudio.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>

namespace openkai2 {
namespace {
std::unique_ptr<ma_device> device;
AudioDeviceCallback target = nullptr;
std::atomic<int64_t> lastCall{0};
std::atomic<uint64_t> lateCalls{0}, longestGap{0}, slowestFill{0};
constexpr int64_t lateGapUs = 60000;

int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void deliver(ma_device *, void *output, const void *, ma_uint32 frames) {
    const int64_t began = nowUs(), previous = lastCall.exchange(began, std::memory_order_relaxed);
    if (previous) {
        const auto gap = uint64_t(began - previous);
        if (int64_t(gap) > lateGapUs)
            lateCalls.fetch_add(1, std::memory_order_relaxed);
        if (gap > longestGap.load(std::memory_order_relaxed))
            longestGap.store(gap, std::memory_order_relaxed);
    }
    if (target)
        target(static_cast<float *>(output), frames);
    const auto spent = uint64_t(nowUs() - began);
    if (spent > slowestFill.load(std::memory_order_relaxed))
        slowestFill.store(spent, std::memory_order_relaxed);
}
}

bool open_audio_device(unsigned rate, AudioDeviceCallback callback, std::string &info) {
    close_audio_device();
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = rate;
    config.periodSizeInMilliseconds = 20;
    config.periods = 4;
    config.wasapi.usage = ma_wasapi_usage_pro_audio;
    config.performanceProfile = ma_performance_profile_conservative;
    config.dataCallback = deliver;
    target = callback;
    lastCall = 0;
    lateCalls = longestGap = slowestFill = 0;
    auto opened = std::make_unique<ma_device>();
    if (ma_device_init(nullptr, &config, opened.get()) != MA_SUCCESS) {
        target = nullptr;
        info = "init-failed";
        return false;
    }
    if (ma_device_start(opened.get()) != MA_SUCCESS) {
        ma_device_uninit(opened.get());
        target = nullptr;
        info = "start-failed";
        return false;
    }
    info = std::string("backend=") + ma_get_backend_name(opened->pContext->backend) +
           " device_rate=" + std::to_string(opened->playback.internalSampleRate) +
           " period=" + std::to_string(opened->playback.internalPeriodSizeInFrames) +
           " periods=" + std::to_string(opened->playback.internalPeriods);
    device = std::move(opened);
    return true;
}

std::string audio_device_report() {
    return "late_callbacks=" + std::to_string(lateCalls.load()) + " longest_gap_ms=" + std::to_string(longestGap.load() / 1000u) +
           " slowest_fill_us=" + std::to_string(slowestFill.load());
}

void close_audio_device() {
    if (device) {
        ma_device_uninit(device.get());
        device.reset();
    }
    target = nullptr;
}
}
