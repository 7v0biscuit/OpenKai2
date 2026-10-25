// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace openkai2 {
// One EE producer and one device callback consumer. Full queues drop new
// frames; the producer never overwrites a frame the callback may be reading.
class StereoAudioQueue {
public:
    static constexpr uint64_t capacity=65536u;
    void reset(){read_=0;written_=0;dropped_=0;discardBefore_=0;}
    void discardPending(){discardBefore_.store(written_.load(std::memory_order_relaxed),std::memory_order_release);}
    size_t push(std::span<const int16_t> samples){
        const auto write=written_.load(std::memory_order_relaxed);
        const auto read=read_.load(std::memory_order_acquire);
        const auto frames=std::min<uint64_t>(samples.size()/2u,capacity-(write-read));
        for(uint64_t i=0;i<frames;++i){const auto at=((write+i)&(capacity-1u))*2u;
            samples_[at]=samples[i*2u];samples_[at+1u]=samples[i*2u+1u];}
        written_.store(write+frames,std::memory_order_release);
        dropped_.fetch_add(samples.size()/2u-frames,std::memory_order_relaxed);
        return size_t(frames);
    }
    size_t mix(std::span<int32_t> output){
        const auto read=std::max(read_.load(std::memory_order_relaxed),discardBefore_.load(std::memory_order_acquire));
        const auto write=written_.load(std::memory_order_acquire);
        const auto frames=std::min<uint64_t>(output.size()/2u,write-read);
        for(uint64_t i=0;i<frames;++i){const auto at=((read+i)&(capacity-1u))*2u;
            output[i*2u]+=samples_[at];output[i*2u+1u]+=samples_[at+1u];}
        read_.store(read+frames,std::memory_order_release);return size_t(frames);
    }
    uint64_t queued()const{
        const auto read=read_.load(std::memory_order_acquire);
        const auto write=written_.load(std::memory_order_acquire);
        return write>=read ? write-read : 0u;
    }
    // Consumer-only operations: honor the stop boundary before checking a
    // prefill or dropping old latency. Only the callback advances the reader.
    uint64_t available()const{
        const auto read=std::max(read_.load(std::memory_order_relaxed),stopBoundary());
        return written_.load(std::memory_order_acquire)-read;
    }
    uint64_t stopBoundary()const{return discardBefore_.load(std::memory_order_acquire);}
    uint64_t trimPending(uint64_t keep){
        const auto read=std::max(read_.load(std::memory_order_relaxed),stopBoundary());
        const auto write=written_.load(std::memory_order_acquire);
        const auto skipped=write-read>keep ? write-read-keep : 0u;
        read_.store(read+skipped,std::memory_order_release);return skipped;
    }
    uint64_t dropped()const{return dropped_.load(std::memory_order_relaxed);}
private:
    std::array<int16_t,capacity*2u> samples_{};
    alignas(64) std::atomic<uint64_t> read_{0};
    alignas(64) std::atomic<uint64_t> written_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> discardBefore_{0};
};
}
