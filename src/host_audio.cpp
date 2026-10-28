// SPDX-License-Identifier: GPL-3.0-only
#include "host_audio.h"
#include "audio_stretch.h"
#include <raylib.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace openkai2 {
namespace {
// SPU2 output runs at 48 kHz. The device stream uses the same rate and 32-bit
// float samples, so PCM reaches the mixer without resampling or requantizing.
// Each core's stream is time-stretched to the rate the game produces it.
constexpr unsigned outputRate = 48000u, chunkFrames = 1024u;
HostAudioOptions options;

struct Output {
    std::array<StereoAudioQueue, 2> queues;
    std::array<StretchedPlayback, 2> playback;
    float gain = 1.0f;
};

// Both cores may peak together. Above the knee the excess is compressed into
// the remaining headroom instead of being clipped at full scale.
float limit(float value) {
    constexpr float knee = 0.9f, room = 1.0f - knee;
    const float magnitude = std::fabs(value);
    if (magnitude <= knee)
        return value;
    return std::copysign(knee + room * std::tanh((magnitude - knee) / room), value);
}

void render(Output &output, float *samples, unsigned frames) {
    for (unsigned first = 0; first < frames; first += chunkFrames) {
        const unsigned count = std::min(chunkFrames, frames - first);
        std::array<float, chunkFrames * 2u> mixed{};
        const std::span<float> span(mixed.data(), count * 2u);
        for (unsigned core = 0; core < 2u; ++core)
            output.playback[core].render(output.queues[core], span);
        for (unsigned i = 0; i < count * 2u; ++i)
            samples[first * 2u + i] = limit(mixed[i] * output.gain);
    }
}

std::unique_ptr<Output> output;
std::atomic<Output *> active{nullptr};
const void *activeOwner = nullptr;
AudioStream stream{};
bool closeDevice = false;

void closePlaybackDevice() {
    if (closeDevice)
        CloseAudioDevice();
    closeDevice = false;
}

void deviceCallback(void *buffer, unsigned int frames) {
    auto *samples = static_cast<float *>(buffer);
    if (auto *current = active.load(std::memory_order_acquire))
        render(*current, samples, frames);
    else
        std::fill(samples, samples + frames * 2u, 0.0f);
}

float gainFor(unsigned volume) { return float(std::min(volume, 100u)) / 100.0f; }
} // namespace

void set_host_audio_options(const HostAudioOptions &value) {
    if (value.volume > 100u)
        throw std::runtime_error("Invalid audio volume");
    options = value;
}
const HostAudioOptions &host_audio_options() noexcept { return options; }

// The runtime leaves the playback device to this module, so a disabled
// setting never opens one. deviceOpen reports a device opened by the caller.
void start_host_audio(const void *owner, bool deviceOpen) {
    if (!options.enabled) {
        std::cerr << "[audio:output] enabled=0 reason=settings\n";
        return;
    }
    if (!deviceOpen) {
        InitAudioDevice();
        closeDevice = IsAudioDeviceReady();
    }
    if (!IsAudioDeviceReady()) {
        std::cerr << "[audio:output] enabled=0 reason=no-device\n";
        return;
    }
    output = std::make_unique<Output>();
    output->gain = gainFor(options.volume);
    SetAudioStreamBufferSizeDefault(int(chunkFrames));
    stream = LoadAudioStream(outputRate, 32u, 2u);
    if (!IsAudioStreamValid(stream)) {
        output.reset();
        closePlaybackDevice();
        std::cerr << "[audio:output] enabled=0 reason=stream-unavailable\n";
        return;
    }
    activeOwner = owner;
    active.store(output.get(), std::memory_order_release);
    SetAudioStreamCallback(stream, deviceCallback);
    PlayAudioStream(stream);
    std::cerr << "[audio:output] enabled=1 rate=" << outputRate << " format=f32 channels=2 volume="
              << options.volume << '\n';
}

void stop_host_audio(const void *owner) {
    if (!output || owner != activeOwner)
        return;
    active.store(nullptr, std::memory_order_release);
    // raylib holds its mixer lock while it runs stream callbacks, so no
    // callback can still be reading the queues once the stream is unloaded.
    StopAudioStream(stream);
    UnloadAudioStream(stream);
    uint64_t dropped = 0, underruns = 0;
    for (unsigned core = 0; core < 2u; ++core) {
        dropped += output->queues[core].dropped();
        underruns += output->playback[core].underruns();
    }
    std::cerr << "[audio:output] stopped dropped_frames=" << dropped << " underruns=" << underruns << '\n';
    output.reset();
    activeOwner = nullptr;
    closePlaybackDevice();
}

void submit_host_audio(const void *owner, unsigned core, std::span<const int16_t> samples, uint32_t rate) {
    auto *current = active.load(std::memory_order_acquire);
    if (!current || owner != activeOwner || core >= 2u || rate != outputRate)
        return;
    if (samples.empty())
        current->queues[core].discardPending();
    else
        current->queues[core].push(samples);
}

int test_host_audio() {
    try {
        const auto require = [](bool condition, const char *message) {
            if (!condition)
                throw std::runtime_error(message);
        };
        auto check = std::make_unique<Output>();
        check->gain = gainFor(50u);
        // Two cores of constant PCM at full speed: once primed, the stream
        // must carry their sum at half volume.
        std::vector<int16_t> first(16384u * 2u, 8192), second(16384u * 2u, 4096);
        check->queues[0].push(first);
        check->queues[1].push(second);
        std::vector<float> samples(8192u * 2u);
        render(*check, samples.data(), 8192u);
        const float expected = (8192.0f + 4096.0f) * 0.5f / 32768.0f;
        require(samples.front() < expected * 0.05f, "Mixer started without a fade-in");
        require(std::fabs(samples[6000u * 2u] - expected) < 1.0e-5f &&
                    std::fabs(samples[6000u * 2u + 1u] - expected) < 1.0e-5f,
                "Mixer did not sum both SPU2 cores at the requested volume");
        require(limit(0.5f) == 0.5f && limit(4.0f) <= 1.0f && limit(1.0f) < 1.0f && limit(1.0f) > 0.95f &&
                    limit(-4.0f) == -limit(4.0f),
                "Limiter changed quiet samples or clipped loud ones");
        // A tone produced at 70% of real time is stretched, not interrupted.
        auto slow = std::make_unique<Output>();
        std::vector<int16_t> tone(48000u * 2u);
        for (unsigned i = 0; i < 48000u; ++i)
            tone[i * 2u] = tone[i * 2u + 1u] = int16_t(8000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / 48000.0));
        size_t fed = 0, gaps = 0;
        std::vector<float> block(1024u * 2u);
        for (unsigned step = 0; step < 60u; ++step) {
            const size_t count = std::min<size_t>(717u, 48000u - fed);
            slow->queues[0].push(std::span<const int16_t>(tone.data() + fed * 2u, count * 2u));
            fed += count;
            std::fill(block.begin(), block.end(), 0.0f);
            render(*slow, block.data(), 1024u);
            if (step > 12u)
                for (unsigned i = 0; i < 1024u; i += 64u)
                    gaps += std::fabs(block[i * 2u]) + std::fabs(block[(i + 16u) * 2u]) + std::fabs(block[(i + 32u) * 2u]) < 1.0e-4f;
        }
        require(gaps == 0u, "Slow PCM left silent gaps instead of being stretched");
        require(slow->playback[0].speed() < 0.85, "Playback rate did not follow the slower stream");
        check->queues[0].discardPending();
        check->queues[1].discardPending();
        render(*check, samples.data(), 1024u);
        std::fill(samples.begin(), samples.end(), 0.0f);
        render(*check, samples.data(), 1024u);
        require(std::fabs(samples[1023u * 2u]) < 1.0e-6f, "Stopped stream kept playing");
        std::cout << "Host audio mixer passed: 48 kHz float stereo, both SPU2 cores, volume, limiter and time-stretching.\n"
                  << "Host audio output is " << (options.enabled ? "enabled" : "disabled")
                  << " in settings at " << options.volume << "% volume.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
} // namespace openkai2
