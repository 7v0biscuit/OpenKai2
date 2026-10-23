// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_queue.h"

namespace openkai2 {
// Device-thread state. Keep sample rate and channel order unchanged; buffer
// jitter and ramp discontinuities rather than replaying or stretching audio.
class StereoAudioPlayback {
public:
    static constexpr unsigned prefill=2048u,fadeFrames=128u;
    void reset(){
        primed_=false;fadeIn_=0;fadeOut_=0;last_={};boundary_=0;drops_=0;
        underruns_=0;silent_=0;trimmed_=0;
    }
    size_t mix(StereoAudioQueue& queue,std::span<int32_t> output){
        size_t consumed=0;
        const auto stop=queue.stopBoundary();
        if(stop!=boundary_){boundary_=stop;primed_=false;fadeIn_=0;fadeOut_=fadeFrames;}
        const auto drops=queue.dropped();
        if(drops!=drops_){
            drops_=drops;trimmed_.fetch_add(queue.trimPending(prefill*2u),std::memory_order_relaxed);
            // Crossfade to the retained queue head after discarding old latency.
            primed_=false;fadeIn_=0;fadeOut_=fadeFrames;
        }
        for(size_t first=0;first<output.size()/2u;first+=512u){
            const auto count=std::min<size_t>(512u,output.size()/2u-first);
            if(!primed_ && queue.available()>=prefill){primed_=true;fadeIn_=0;}
            std::array<int32_t,1024> raw{};
            const auto frames=primed_ ? queue.mix(std::span<int32_t>(raw.data(),count*2u)) : 0u;
            consumed+=frames;
            for(size_t i=0;i<count;++i){
                const auto at=(first+i)*2u;
                if(i<frames){
                    fadeIn_=std::min(fadeIn_+1u,fadeFrames);
                    if(fadeOut_)--fadeOut_;
                    for(unsigned channel=0;channel<2u;++channel){
                        const int32_t value=raw[i*2u+channel]*int32_t(fadeIn_)/int32_t(fadeFrames);
                        const int32_t tail=fadeOut_ ? last_[channel]*int32_t(fadeOut_)/int32_t(fadeFrames) : 0;
                        output[at+channel]+=value+tail;
                        if(!fadeOut_)last_[channel]=value;
                    }
                }else{
                    if(i==frames && primed_){
                        primed_=false;fadeIn_=0;fadeOut_=fadeFrames;
                        underruns_.fetch_add(1u,std::memory_order_relaxed);
                    }
                    if(fadeOut_){
                        --fadeOut_;
                        for(unsigned channel=0;channel<2u;++channel)
                            output[at+channel]+=last_[channel]*int32_t(fadeOut_)/int32_t(fadeFrames);
                    }
                }
            }
            silent_.fetch_add(count-frames,std::memory_order_relaxed);
        }
        return consumed;
    }
    uint64_t underruns()const{return underruns_.load(std::memory_order_relaxed);}
    uint64_t silentFrames()const{return silent_.load(std::memory_order_relaxed);}
    uint64_t trimmedFrames()const{return trimmed_.load(std::memory_order_relaxed);}
private:
    bool primed_=false;
    unsigned fadeIn_=0,fadeOut_=0;
    std::array<int32_t,2> last_{};
    uint64_t boundary_=0,drops_=0;
    std::atomic<uint64_t> underruns_{0},silent_{0},trimmed_{0};
};
}
