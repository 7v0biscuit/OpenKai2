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
#include <memory>

namespace openkai2 {
namespace {
std::unique_ptr<ma_device> device;
AudioDeviceCallback target = nullptr;

void deliver(ma_device *, void *output, const void *, ma_uint32 frames) {
    if (target)
        target(static_cast<float *>(output), frames);
}
}

bool open_audio_device(unsigned rate, AudioDeviceCallback callback, std::string &info) {
    close_audio_device();
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = rate;
    config.periodSizeInMilliseconds = 20;
    config.periods = 3;
    config.performanceProfile = ma_performance_profile_conservative;
    config.dataCallback = deliver;
    target = callback;
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

void close_audio_device() {
    if (device) {
        ma_device_uninit(device.get());
        device.reset();
    }
    target = nullptr;
}
}
