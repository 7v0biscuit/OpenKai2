// SPDX-License-Identifier: GPL-3.0-only
#include "audio_stretch.h"
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace openkai2;
namespace {
void require(bool good, const char *why) {
    if (!good)
        throw std::runtime_error(why);
}
struct Stream {
    StereoAudioQueue queue;
    StretchedPlayback playback;
    size_t position = 0;
    static float sample(size_t frame) {
        return float(int16_t(10000 * std::sin(frame * (2 * 3.141592653589793 * 440 / 48000)))) / 32768;
    }
    void feed(size_t count) {
        std::vector<int16_t> data(count * 2);
        for (size_t i = 0; i < count; ++i, ++position) {
            data[i * 2] = int16_t(sample(position) * 32768);
            data[i * 2 + 1] = -data[i * 2];
        }
        require(queue.push(data) == count, "Audio producer overflowed");
    }
};
void continuous(double rate, bool bursty) {
    auto s = std::make_unique<Stream>();
    double owed = 0, minTempo = 2;
    uint32_t random = 1;
    size_t gaps = 0, crossings = 0, measured = 0;
    float previous = 0;
    for (unsigned step = 0; step < 3000; ++step) {
        random = random * 1664525u + 1013904223u;
        owed += rate * 1024 * (bursty ? .6 + .8 * double(random >> 8) / (1u << 24) : 1.0);
        const auto count = size_t(owed / 512) * 512;
        owed -= count;
        s->feed(count);
        std::vector<float> out(2048);
        s->playback.render(s->queue, out);
        if (step <= 600)
            continue;
        minTempo = std::min(minTempo, s->playback.speed());
        for (size_t i = 0; i < 1024; ++i) {
            require(std::abs(out[i * 2] + out[i * 2 + 1]) < 1e-6, "Stereo phase changed");
            if (previous <= 0 && out[i * 2] > 0)
                ++crossings;
            previous = out[i * 2];
            ++measured;
        }
        for (size_t i = 0; i < 1024; i += 64)
            gaps += std::abs(out[i * 2]) + std::abs(out[(i + 16) * 2]) + std::abs(out[(i + 32) * 2]) < 1e-4;
    }
    const double pitch = double(crossings) * 48000 / measured;
    std::cout << "rate=" << rate << " bursty=" << bursty << " gaps=" << gaps << " min_tempo=" << minTempo
              << " pitch=" << pitch << '\n';
    require(gaps == 0, "Slow or bursty audio left silent gaps");
    require(std::abs(pitch - 440) < 5, "Time stretching changed pitch");
    require(minTempo > rate * .7, "Playback tempo collapsed");
}
}
int main() {
    try {
        for (double rate : {.4, .7, .93, 1.0})
            continuous(rate, false);
        continuous(.93, true);
        auto s = std::make_unique<Stream>();
        s->feed(StretchedPlayback::prefill + 512);
        for (unsigned step = 0; step < 80; ++step) {
            std::vector<float> out(2048);
            s->playback.render(s->queue, out);
            for (size_t i = 0; i < 1024; ++i) {
                const auto frame = step * 1024 + i;
                if (frame < 512)
                    continue;
                require(out[i * 2] == Stream::sample(frame) && out[i * 2 + 1] == -Stream::sample(frame),
                        "Full-speed PCM was changed");
            }
            s->feed(1024);
        }
        s->queue.discardPending();
        std::vector<float> stop(2048);
        s->playback.render(s->queue, stop);
        std::fill(stop.begin(), stop.end(), 0);
        s->playback.render(s->queue, stop);
        require(std::all_of(stop.begin(), stop.end(), [](float f) { return f == 0; }),
                "Stopped audio continued");
        std::cout << "Audio playback checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
