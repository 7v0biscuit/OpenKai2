// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_queue.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace openkai2 {
// Plays one SPU2 stream at whatever rate the game produces it. When the game
// runs below full speed the queue drains, so the playback rate follows the
// queue level and WSOLA stretches the audio without changing its pitch;
// overlapping windows are aligned on the best waveform match, which keeps
// the stretched output free of the gaps and clicks an underrun would cause.
class StretchedPlayback {
public:
    static constexpr unsigned window = 1024u, hop = window / 2u, search = 256u;
    static constexpr unsigned target = 3072u, prefill = target + window + search * 2u;
    static constexpr double slowest = 0.5, fastest = 1.25;

    StretchedPlayback() {
        for (unsigned i = 0; i < window; ++i)
            weights_[i] = float(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / window));
    }
    void reset() {
        input_.clear();
        tail_.fill(0.0f);
        output_.clear();
        outputRead_ = 0;
        previous_ = -1;
        position_ = 0.0;
        speed_ = 1.0;
        playing_ = false;
        boundary_ = 0;
    }
    // Adds up to output.size()/2 stereo frames, scaled to +-1.0, into output.
    void render(StereoAudioQueue &queue, std::span<float> output) {
        const auto stop = queue.stopBoundary();
        if (stop != boundary_) {
            boundary_ = stop;
            fadeTail();
            input_.clear();
            previous_ = -1;
            position_ = 0.0;
            playing_ = false;
        }
        pull(queue);
        const size_t frames = output.size() / 2u;
        for (size_t written = 0; written < frames;) {
            if (outputRead_ == output_.size() / 2u) {
                output_.clear();
                outputRead_ = 0;
                produce(queue);
                if (output_.empty()) {
                    silent_.fetch_add(frames - written, std::memory_order_relaxed);
                    return;
                }
            }
            const size_t count = std::min(frames - written, output_.size() / 2u - outputRead_);
            for (size_t i = 0; i < count * 2u; ++i)
                output[written * 2u + i] += output_[outputRead_ * 2u + i];
            outputRead_ += count;
            written += count;
        }
    }
    double speed() const { return speed_; }
    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    uint64_t silentFrames() const { return silent_.load(std::memory_order_relaxed); }

private:
    size_t frames() const { return input_.size() / 2u; }
    void pull(StereoAudioQueue &queue) {
        const auto available = queue.available();
        if (!available)
            return;
        raw_.assign(size_t(std::min<uint64_t>(available, 16384u)) * 2u, 0);
        const size_t got = queue.mix(raw_);
        for (size_t i = 0; i < got * 2u; ++i)
            input_.push_back(float(raw_[i]) / 32768.0f);
    }
    float sample(size_t frame, unsigned channel) const { return input_[frame * 2u + channel]; }
    // Cross-correlation of a candidate window start against the natural
    // continuation of the previous window, over the overlapping half.
    double match(size_t candidate, size_t natural) const {
        double sum = 0.0;
        for (unsigned i = 0; i < hop; i += 2u)
            sum += double(sample(candidate + i, 0) + sample(candidate + i, 1)) *
                   double(sample(natural + i, 0) + sample(natural + i, 1));
        return sum;
    }
    bool produce(StereoAudioQueue &queue) {
        pull(queue);
        if (!playing_) {
            if (frames() < prefill)
                return false;
            playing_ = true;
            position_ = double(search);
        }
        // Rate follows the queued depth: below the target the stream is
        // stretched, above it slightly compressed to recover latency.
        const double queued = double(frames()) - position_;
        const double wanted = std::clamp(queued / double(target + window), slowest, fastest);
        speed_ += (wanted - speed_) * 0.08;
        const auto nominal = size_t(position_);
        if (nominal + search + window > frames()) {
            underruns_.fetch_add(1u, std::memory_order_relaxed);
            fadeTail();
            playing_ = false;
            input_.erase(input_.begin(), input_.begin() + std::min(input_.size(), nominal * 2u));
            previous_ = -1;
            return false;
        }
        size_t start = nominal;
        if (previous_ >= 0) {
            const size_t natural = size_t(previous_) + hop;
            if (natural != nominal) {
                double best = -1.0e300;
                const size_t first = nominal > search ? nominal - search : 0u;
                for (size_t candidate = first; candidate <= nominal + search; candidate += 2u) {
                    const double score = match(candidate, natural);
                    if (score > best) {
                        best = score;
                        start = candidate;
                    }
                }
            }
        }
        // Hann windows at half-window hops sum to one, so overlap-add keeps
        // the level of the source.
        output_.resize(size_t(hop) * 2u);
        for (unsigned i = 0; i < hop; ++i)
            for (unsigned c = 0; c < 2u; ++c)
                output_[i * 2u + c] = tail_[i * 2u + c] + sample(start + i, c) * weights_[i];
        for (unsigned i = 0; i < hop; ++i)
            for (unsigned c = 0; c < 2u; ++c)
                tail_[i * 2u + c] = sample(start + hop + i, c) * weights_[hop + i];
        previous_ = int64_t(start);
        position_ += double(hop) * speed_;
        // Discard input no later window can reach.
        const size_t keep = std::min<size_t>(size_t(previous_), size_t(position_) > search ? size_t(position_) - search : 0u);
        if (keep > 4096u) {
            input_.erase(input_.begin(), input_.begin() + keep * 2u);
            previous_ -= int64_t(keep);
            position_ -= double(keep);
        }
        return true;
    }
    // A stream that stops or runs dry ends on the decaying half of its last
    // window rather than on a hard edge.
    void fadeTail() {
        output_.assign(tail_.begin(), tail_.end());
        outputRead_ = 0;
        tail_.fill(0.0f);
    }

    std::array<float, window> weights_{};
    std::array<float, hop * 2u> tail_{};
    std::vector<float> input_, output_;
    std::vector<int32_t> raw_;
    size_t outputRead_ = 0;
    int64_t previous_ = -1;
    double position_ = 0.0, speed_ = 1.0;
    bool playing_ = false;
    uint64_t boundary_ = 0;
    std::atomic<uint64_t> underruns_{0}, silent_{0};
};
} // namespace openkai2
