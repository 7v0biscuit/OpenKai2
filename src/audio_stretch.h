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
class StretchedPlayback {
  public:
    static constexpr unsigned window = 1024u, hop = 512u, search = 256u;
    static constexpr unsigned target = 6144u, prefill = target + window + search * 2u;
    static constexpr double slowest = 0.25, fastest = 1.1;
    static constexpr double startBelow = 0.6, startAbove = 2.0, reserve = 0.45;
    static constexpr unsigned averaged = 48u, settledCalls = 96u, crossfade = 384u;

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
        fed_.reserve(history * 2u);
    }
    void reset() {
        clearStream();
        last_.fill(0);
        fadeLeft_ = fadeIn_ = 0;
        boundary_ = 0;
        resumeRate_ = 1.0;
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
            bool slow = false;
            if (startupOutput_ >= prefill) {
                rate_ = shortRate_ = std::clamp(double(startupInput_) / startupOutput_, slowest, 1.0);
                slow = rate_ < 0.85;
                if (!slow)
                    rate_ = shortRate_ = 1.0;
            }
            if (resumeRate_ < 0.97) {
                rate_ = shortRate_ = std::min(rate_, resumeRate_);
                slow = true;
            }
            resumeRate_ = 1.0;
            playing_ = true;
            fadeIn_ = hop;
            if (slow) {
                stretched_ = true;
                speed_ = rate_;
            }
            intervalInput_ = intervalOutput_ = longInput_ = longOutput_ = 0;
        }
        if (wasPlaying)
            updateRate(received, requested);
        if (!playing_) {
            fade(output);
            silent_.fetch_add(requested, std::memory_order_relaxed);
            return;
        }
        steer();
        size_t written = 0;
        if (!tail_.empty()) {
            const size_t count = std::min(requested, tail_.size() / 2u);
            mix(output, tail_.data(), count);
            tail_.erase(tail_.begin(), tail_.begin() + count * 2u);
            written = count;
        }
        const auto rest = output.subspan(written * 2u);
        const size_t wanted = requested - written;
        if (stretched_) {
            stretch_.setTempo(speed_);
            if (available()) {
                remember(input_.data() + read_ * 2u, available());
                stretch_.putSamples(input_.data() + read_ * 2u, unsigned(available()));
                read_ = input_.size() / 2u;
            }
            processed_.resize(wanted * 2u);
            const auto count = stretch_.receiveSamples(processed_.data(), unsigned(wanted));
            mix(rest, processed_.data(), count);
            if (count < wanted)
                underrun(rest.subspan(size_t(count) * 2u));
        } else {
            const size_t count = std::min(wanted, available());
            mix(rest, input_.data() + read_ * 2u, count);
            read_ += count;
            if (count < wanted)
                underrun(rest.subspan(count * 2u));
        }
        if (read_ > 8192u || read_ == input_.size() / 2u) {
            input_.erase(input_.begin(), input_.begin() + read_ * 2u);
            read_ = 0;
        }
    }
    double speed() const { return speed_; }
    bool stretching() const { return stretched_; }
    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    uint64_t silentFrames() const { return silent_.load(std::memory_order_relaxed); }

  private:
    static constexpr size_t history = 16384u;
    size_t available() const { return input_.size() / 2u - read_; }
    double buffered() const {
        return double(available()) + double(tail_.size() / 2u) + stretch_.numUnprocessedSamples() +
               double(stretch_.numSamples()) * speed_;
    }
    void steer() {
        const double fill = buffered() / double(target);
        fills_[fillAt_++ % averaged] = fill;
        const size_t count = std::min<size_t>(fillAt_, averaged);
        double average = 0.0;
        for (size_t i = 0; i < count; ++i)
            average += fills_[i];
        average /= double(count);
        recent_ = fillAt_ == 1u ? fill : recent_ + (fill - recent_) * 0.2;
        if (!stretched_) {
            const bool falling = recent_ < startBelow && average < 0.9;
            const bool rising = count >= averaged / 4u && average > startAbove;
            if (!falling && !rising)
                return;
            stretched_ = true;
            settled_ = 0;
            speed_ = wantedSpeed(recent_);
            return;
        }
        const bool low = fill < reserve;
        const double wanted = wantedSpeed(low ? fill : recent_);
        speed_ = low ? std::min(speed_, wanted) : speed_ + (wanted - speed_) * 0.2;
        if (std::abs(rate_ - 1.0) < 0.03 && std::abs(average - 1.0) < 0.2 && std::abs(speed_ - 1.0) < 0.03)
            ++settled_;
        else
            settled_ = 0;
        if (settled_ >= settledCalls)
            leave();
    }
    double wantedSpeed(double fill) const { return std::clamp(rate_ * (1.0 + 0.6 * (fill - 1.0)), slowest, fastest); }
    void leave() {
        const size_t pending = stretch_.numUnprocessedSamples() + crossfade;
        tail_.resize(size_t(stretch_.numSamples()) * 2u);
        tail_.resize(size_t(stretch_.receiveSamples(tail_.data(), stretch_.numSamples())) * 2u);
        const size_t held = fed_.size() / 2u;
        const size_t back = std::min(held, pending + crossfade);
        std::vector<float> resume(fed_.end() - back * 2u, fed_.end());
        const size_t blend = std::min({size_t(crossfade), tail_.size() / 2u, back});
        const size_t from = tail_.size() / 2u - blend;
        for (size_t i = 0; i < blend; ++i) {
            const float in = float(i + 1u) / float(blend + 1u);
            for (unsigned channel = 0; channel < 2u; ++channel)
                tail_[(from + i) * 2u + channel] =
                    tail_[(from + i) * 2u + channel] * (1.0f - in) + resume[i * 2u + channel] * in;
        }
        input_.erase(input_.begin(), input_.begin() + read_ * 2u);
        read_ = 0;
        input_.insert(input_.begin(), resume.begin() + blend * 2u, resume.end());
        stretch_.clear();
        stretch_.setTempo(1.0);
        fed_.clear();
        stretched_ = false;
        speed_ = 1.0;
        settled_ = 0;
        fillAt_ = 0;
    }
    void remember(const float *samples, size_t frames) {
        fed_.insert(fed_.end(), samples, samples + frames * 2u);
        if (fed_.size() > history * 2u)
            fed_.erase(fed_.begin(), fed_.end() - history * 2u);
    }
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
        tail_.clear();
        fed_.clear();
        read_ = 0;
        playing_ = stretched_ = false;
        speed_ = rate_ = shortRate_ = 1.0;
        startupInput_ = startupOutput_ = 0;
        intervalInput_ = intervalOutput_ = longInput_ = longOutput_ = 0;
        fillAt_ = settled_ = 0;
    }
    void underrun(std::span<float> remaining) {
        underruns_.fetch_add(1u, std::memory_order_relaxed);
        silent_.fetch_add(remaining.size() / 2u, std::memory_order_relaxed);
        const double rate = rate_;
        clearStream();
        resumeRate_ = rate;
        fadeLeft_ = hop;
        fade(remaining);
    }
    soundtouch::SoundTouch stretch_;
    std::vector<int32_t> raw_;
    std::vector<float> input_, processed_, tail_, fed_;
    size_t read_ = 0;
    bool playing_ = false, stretched_ = false;
    double speed_ = 1.0, rate_ = 1.0, shortRate_ = 1.0;
    uint64_t intervalInput_ = 0, intervalOutput_ = 0, longInput_ = 0, longOutput_ = 0;
    uint64_t startupInput_ = 0, startupOutput_ = 0;
    uint64_t boundary_ = 0;
    std::array<double, averaged> fills_{};
    size_t fillAt_ = 0;
    double recent_ = 1.0, resumeRate_ = 1.0;
    unsigned settled_ = 0;
    std::array<float, 2> last_{};
    unsigned fadeLeft_ = 0, fadeIn_ = 0;
    std::atomic<uint64_t> underruns_{0}, silent_{0};
};
}
