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
    std::array<std::atomic<uint64_t>, 2> submitted{}, stops{};
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
        std::cerr << "[audio:core] core=" << core
                  << " submitted_frames=" << output->submitted[core].load(std::memory_order_relaxed)
                  << " stop_requests=" << output->stops[core].load(std::memory_order_relaxed)
                  << " queued_frames=" << output->queues[core].queued()
                  << " silent_frames=" << output->playback[core].silentFrames()
                  << " tempo=" << output->playback[core].speed() << '\n';
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
    if (samples.empty()) {
        current->stops[core].fetch_add(1, std::memory_order_relaxed);
        current->queues[core].discardPending();
    } else {
        current->submitted[core].fetch_add(samples.size() / 2u, std::memory_order_relaxed);
        current->queues[core].push(samples);
    }
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
        // A fight running at 40% of real time still plays continuously.
        auto crawl = std::make_unique<Output>();
        fed = gaps = 0;
        for (unsigned step = 0; step < 100u; ++step) {
            const size_t count = std::min<size_t>(410u, 48000u - fed);
            crawl->queues[0].push(std::span<const int16_t>(tone.data() + fed * 2u, count * 2u));
            fed += count;
            std::fill(block.begin(), block.end(), 0.0f);
            render(*crawl, block.data(), 1024u);
            if (step > 30u)
                for (unsigned i = 0; i < 1024u; i += 64u)
                    gaps += std::fabs(block[i * 2u]) + std::fabs(block[(i + 16u) * 2u]) + std::fabs(block[(i + 32u) * 2u]) < 1.0e-4f;
        }
        require(gaps == 0u, "PCM at 40% speed left silent gaps");
        // A game held at 93% speed delivers audio in irregular bursts. The
        // tempo must follow it steadily, never dropping towards half speed
        // each time the queue runs low.
        auto bursty = std::make_unique<Output>();
        uint32_t seed = 1u;
        double owed = 0.0, slowest = 1.0;
        size_t played = 0;
        std::vector<int16_t> chunk(1024u);
        gaps = 0;
        for (unsigned step = 0; step < 3000u; ++step) {
            seed = seed * 1664525u + 1013904223u;
            owed += 0.93 * 1024.0 * (0.6 + 0.8 * double(seed >> 8u) / double(1u << 24u));
            for (; owed >= 512.0; owed -= 512.0) {
                for (unsigned i = 0; i < 512u; ++i, ++played)
                    chunk[i * 2u] = chunk[i * 2u + 1u] = tone[(played % 48000u) * 2u];
                bursty->queues[0].push(chunk);
            }
            std::fill(block.begin(), block.end(), 0.0f);
            render(*bursty, block.data(), 1024u);
            if (step > 600u) {
                slowest = std::min(slowest, bursty->playback[0].speed());
                for (unsigned i = 0; i < 1024u; i += 64u)
                    gaps += std::fabs(block[i * 2u]) + std::fabs(block[(i + 16u) * 2u]) + std::fabs(block[(i + 32u) * 2u]) < 1.0e-4f;
            }
        }
        require(gaps == 0u && slowest > 0.7, "Audio at 93% speed lost its tempo or ran dry");
        // At full speed the stretcher is transparent: output equals input.
        auto steady = std::make_unique<Output>();
        steady->gain = 1.0f;
        size_t pushed = StretchedPlayback::prefill + 512u;
        steady->queues[0].push(std::span<const int16_t>(tone.data(), pushed * 2u));
        std::vector<float> through(24576u * 2u);
        for (unsigned step = 0; step < 24u; ++step) {
            render(*steady, through.data() + step * 1024u * 2u, 1024u);
            const size_t count = std::min<size_t>(1024u, 48000u - pushed);
            steady->queues[0].push(std::span<const int16_t>(tone.data() + pushed * 2u, count * 2u));
            pushed += count;
        }
        // Find where output frame 12000 sits in the tone, then compare 8000
        // frames; the window sum leaves only float rounding (about -90 dB).
        const auto input = [&](size_t frame) { return limit(float(tone[frame * 2u]) / 32768.0f); };
        size_t lag = 0;
        double worst = 1.0;
        for (size_t start = 0u; start < 30000u && worst > 1.0e-4; ++start) {
            worst = 0.0;
            for (size_t i = 0; i < 64u && worst <= 1.0e-4; ++i)
                worst = std::max(worst, double(std::fabs(through[(12000u + i) * 2u] - input(start + i))));
            lag = start;
        }
        for (size_t i = 0; i < 8000u; ++i)
            worst = std::max(worst, double(std::fabs(through[(12000u + i) * 2u] - input(lag + i))));
        require(worst < 1.0e-4, "Full-speed playback changed the waveform");
        check->queues[0].discardPending();
        check->queues[1].discardPending();
        render(*check, samples.data(), 1024u);
        std::fill(samples.begin(), samples.end(), 0.0f);
        render(*check, samples.data(), 1024u);
        require(std::fabs(samples[1023u * 2u]) < 1.0e-6f, "Stopped stream kept playing");
        std::cout << "Host audio mixer passed: 48 kHz float stereo, both SPU2 cores, volume, limiter, transparent full-speed playback, a steady tempo at 93% speed and stretching to 40% speed.\n"
                  << "Host audio output is " << (options.enabled ? "enabled" : "disabled")
                  << " in settings at " << options.volume << "% volume.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
} // namespace openkai2
