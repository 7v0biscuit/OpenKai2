// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_queue.h"
#include <SoundTouch.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace openkai2 {
// Full-speed PCM passes through unchanged. A genuinely slower producer uses
// SoundTouch's stereo time stretching, retaining pitch and channel phase.
class StretchedPlayback {
  public:
    static constexpr unsigned window = 1024u, hop = 512u, search = 256u;
    static constexpr unsigned target = 4096u, prefill = target + window + search * 2u;
    static constexpr double slowest = 0.25, fastest = 1.25;

    StretchedPlayback() {
        stretch_.setSampleRate(48000);
        stretch_.setChannels(2);
        stretch_.setSetting(SETTING_USE_QUICKSEEK, 0);
        stretch_.setSetting(SETTING_USE_AA_FILTER, 1);
        stretch_.setSetting(SETTING_SEQUENCE_MS, 32);
        stretch_.setSetting(SETTING_SEEKWINDOW_MS, 12);
        stretch_.setSetting(SETTING_OVERLAP_MS, 8);
        raw_.reserve(32768);
        input_.reserve(32768);
    }
    void reset() {
        clearStream();
        speed_ = rate_ = shortRate_ = 1.0;
        intervalInput_ = intervalOutput_ = longInput_ = longOutput_ = 0;
        boundary_ = 0;
        last_.fill(0);
        fadeLeft_ = fadeIn_ = 0;
    }
    void render(StereoAudioQueue &queue, std::span<float> output) {
        const auto stop = queue.stopBoundary();
        if (stop != boundary_) {
            boundary_ = stop;
            clearStream();
            fadeLeft_ = hop;
        }
        const bool wasPlaying = playing_;
        const size_t received = pull(queue);
        const size_t requested = output.size() / 2u;
        if (!wasPlaying) {
            startupInput_ += received;
            startupOutput_ += requested;
        }
        if (!playing_ && available() >= prefill) {
            if (startupOutput_ >= prefill)
                speed_ = rate_ = shortRate_ =
                    std::clamp(double(startupInput_) / startupOutput_, slowest, 1.0);
            playing_ = true;
            fadeIn_ = hop;
            intervalInput_ = intervalOutput_ = longInput_ = longOutput_ = 0;
        }
        if (wasPlaying)
            updateRate(received, requested);
        if (!playing_) {
            fade(output);
            silent_.fetch_add(requested, std::memory_order_relaxed);
            return;
        }
        const double buffered =
            double(available()) + stretch_.numUnprocessedSamples() + double(stretch_.numSamples()) * speed_;
        const double fill = buffered / double(target + window);
        const bool low = fill < 0.85;
        const bool unity = std::abs(rate_ - 1.0) < 0.01 && fill >= 0.7 && fill <= 1.8;
        double wanted = unity ? 1.0 : std::clamp(rate_ * (1.0 + 0.05 * (fill - 1.0)), slowest, fastest);
        if (low)
            wanted = std::clamp(std::min(std::max(shortRate_, rate_ * 0.8), wanted), slowest, fastest);
        if (!stretched_ && rate_ == 1.0)
            wanted = 1.0;
        speed_ = low ? wanted : speed_ + (wanted - speed_) * 0.03;
        if (unity && std::abs(speed_ - 1.0) < 0.002)
            speed_ = 1.0;
        if (!stretched_ && speed_ != 1.0)
            stretched_ = true;
        if (stretched_) {
            stretch_.setTempo(speed_);
            if (available()) {
                stretch_.putSamples(input_.data() + read_ * 2u, unsigned(available()));
                read_ = input_.size() / 2u;
            }
            processed_.resize(requested * 2u);
            const auto count = stretch_.receiveSamples(processed_.data(), unsigned(requested));
            mix(output, processed_.data(), count);
            if (count < requested)
                underrun(output.subspan(size_t(count) * 2u));
        } else {
            const size_t count = std::min(requested, available());
            mix(output, input_.data() + read_ * 2u, count);
            read_ += count;
            if (count < requested)
                underrun(output.subspan(count * 2u));
        }
        if (read_ > 8192u || read_ == input_.size() / 2u) {
            input_.erase(input_.begin(), input_.begin() + read_ * 2u);
            read_ = 0;
        }
    }
    double speed() const { return speed_; }
    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    uint64_t silentFrames() const { return silent_.load(std::memory_order_relaxed); }

  private:
    size_t available() const { return input_.size() / 2u - read_; }
    size_t pull(StereoAudioQueue &queue) {
        const auto count = std::min<uint64_t>(queue.available(), 16384u);
        if (!count)
            return 0;
        raw_.assign(size_t(count) * 2u, 0);
        const auto got = queue.mix(raw_);
        for (size_t i = 0; i < got * 2u; ++i)
            input_.push_back(float(raw_[i]) / 32768.0f);
        return got;
    }
    void updateRate(size_t input, size_t output) {
        intervalInput_ += input;
        intervalOutput_ += output;
        longInput_ += input;
        longOutput_ += output;
        if (intervalOutput_ >= 4096u) {
            shortRate_ = double(intervalInput_) / intervalOutput_;
            intervalInput_ = intervalOutput_ = 0;
        }
        if (longOutput_ >= 16384u) {
            const double measured = double(longInput_) / longOutput_;
            rate_ += (measured - rate_) * 0.25;
            longInput_ = longOutput_ = 0;
        }
    }
    void mix(std::span<float> destination, const float *source, size_t frames) {
        for (size_t i = 0; i < frames; ++i) {
            const float gain = fadeIn_ ? 1.0f - float(fadeIn_) / hop : 1.0f;
            for (unsigned channel = 0; channel < 2u; ++channel) {
                const float sample = source[i * 2u + channel] * gain;
                destination[i * 2u + channel] += sample;
                last_[channel] = sample;
            }
            if (fadeIn_)
                --fadeIn_;
        }
    }
    void fade(std::span<float> destination) {
        const auto start = last_;
        for (size_t i = 0; i < destination.size() / 2u && fadeLeft_; ++i) {
            const float gain = float(fadeLeft_ - 1u) / hop;
            for (unsigned channel = 0; channel < 2u; ++channel)
                destination[i * 2u + channel] += start[channel] * gain;
            --fadeLeft_;
        }
        if (!fadeLeft_)
            last_.fill(0);
    }
    void clearStream() {
        stretch_.clear();
        stretch_.setTempo(1.0);
        input_.clear();
        read_ = 0;
        playing_ = stretched_ = false;
        speed_ = rate_ = shortRate_ = 1.0;
        startupInput_ = startupOutput_ = 0;
        intervalInput_ = intervalOutput_ = longInput_ = longOutput_ = 0;
    }
    void underrun(std::span<float> remaining) {
        underruns_.fetch_add(1u, std::memory_order_relaxed);
        silent_.fetch_add(remaining.size() / 2u, std::memory_order_relaxed);
        clearStream();
        fadeLeft_ = hop;
        fade(remaining);
    }
    soundtouch::SoundTouch stretch_;
    std::vector<int32_t> raw_;
    std::vector<float> input_, processed_;
    size_t read_ = 0;
    bool playing_ = false, stretched_ = false;
    double speed_ = 1.0, rate_ = 1.0, shortRate_ = 1.0;
    uint64_t intervalInput_ = 0, intervalOutput_ = 0, longInput_ = 0, longOutput_ = 0;
    uint64_t startupInput_ = 0, startupOutput_ = 0;
    uint64_t boundary_ = 0;
    std::array<float, 2> last_{};
    unsigned fadeLeft_ = 0, fadeIn_ = 0;
    std::atomic<uint64_t> underruns_{0}, silent_{0};
};
} // namespace openkai2
