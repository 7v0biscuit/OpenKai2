// SPDX-License-Identifier: GPL-3.0-only
#include "host_audio.h"
#include "audio_queue.h"
#include "audio_buffer.h"
#include "ps2_host_backend.h"
#include <iostream>
#include <mutex>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace openkai2 {
namespace {
std::array<StereoAudioQueue,2> queues;
std::array<StereoAudioPlayback,2> playback;
std::atomic<const void*> activeOwner{nullptr};
std::mutex lifecycle;
AudioStream stream{};
std::atomic<uint64_t> blocks{0},frames{0},nonzero{0};
std::atomic<uint64_t> delivered{0};

void fill(void* buffer,unsigned frameCount){
    auto* out=static_cast<int16_t*>(buffer);
    // Chunking bounds stack use even if a backend requests a large callback.
    for(unsigned first=0;first<frameCount;first+=512u){
        const auto count=std::min(512u,frameCount-first);
        std::array<int32_t,1024> mixed{};
        auto target=std::span<int32_t>(mixed.data(),count*2u);
        for(unsigned core=0;core<2u;++core)
            delivered.fetch_add(playback[core].mix(queues[core],target),std::memory_order_relaxed);
        for(unsigned i=0;i<count*2u;++i)out[first*2u+i]=int16_t(std::clamp(mixed[i],-32768,32767));
    }
}
}

void start_host_audio(const void* owner,bool ready){
    std::lock_guard lock(lifecycle);
    if(!ready){std::cerr<<"[audio:output] ready=0 reason=device-initialization\n";return;}
    if(activeOwner.load(std::memory_order_acquire))return;
    for(auto& queue:queues)queue.reset();blocks=0;frames=0;nonzero=0;delivered=0;
    for(auto& output:playback)output.reset();
    SetAudioStreamBufferSizeDefault(1024);
    stream=LoadAudioStream(48000,16,2);
    if(!IsAudioStreamValid(stream)){
        std::cerr<<"[audio:output] ready=0 rate=48000 channels=2 reason=stream-initialization\n";return;
    }
    SetAudioStreamCallback(stream,fill);PlayAudioStream(stream);
    activeOwner.store(owner,std::memory_order_release);
    std::cerr<<"[audio:output] ready=1 rate=48000 channels=2 prefill_frames="<<StereoAudioPlayback::prefill
        <<" backend=raylib-miniaudio\n";
}
void stop_host_audio(const void* owner){
    std::lock_guard lock(lifecycle);
    if(activeOwner.load(std::memory_order_acquire)!=owner)return;
    activeOwner.store(nullptr,std::memory_order_release);
    StopAudioStream(stream);UnloadAudioStream(stream);stream={};
    std::cerr<<"[audio:output] blocks="<<blocks<<" frames="<<frames<<" nonzero_samples="<<nonzero
        <<" delivered_frames="<<delivered
        <<" dropped_frames="<<queues[0].dropped()+queues[1].dropped()
        <<" underruns="<<playback[0].underruns()+playback[1].underruns()
        <<" trimmed_frames="<<playback[0].trimmedFrames()+playback[1].trimmedFrames()<<'\n';
}
void submit_host_audio(const void* owner,unsigned core,std::span<const int16_t> samples,uint32_t rate){
    if(rate!=48000u || core>=2u || activeOwner.load(std::memory_order_acquire)!=owner)return;
    if(samples.empty()){queues[core].discardPending();return;}
    const auto accepted=queues[core].push(samples);const auto count=++blocks;frames+=accepted;
    nonzero+=std::count_if(samples.begin(),samples.end(),[](int16_t value){return value!=0;});
    if(count<=2u || (count&(count-1u))==0u)
        std::cerr<<"[audio:pcm] blocks="<<blocks<<" core="<<core<<" frames="<<frames
            <<" nonzero_samples="<<nonzero<<" queued="<<queues[core].queued()
            <<" dropped="<<queues[core].dropped()<<" underruns="<<playback[core].underruns()
            <<" silent_frames="<<playback[core].silentFrames()
            <<" trimmed_frames="<<playback[core].trimmedFrames()<<'\n';
}
int test_host_audio(){
    InitAudioDevice();
    if(!IsAudioDeviceReady()){
        std::cerr<<"Audio device test failed: no output device could be opened.\n";return 1;
    }
    const int owner=0;start_host_audio(&owner,true);
    if(activeOwner.load(std::memory_order_acquire)!=&owner){CloseAudioDevice();return 1;}
    std::vector<int16_t> tone(48000u*2u);
    for(unsigned i=0;i<48000u;++i){
        const auto sample=int16_t(2048.0*std::sin(6.283185307179586*440.0*i/48000.0));
        tone[i*2u]=tone[i*2u+1u]=sample;
    }
    submit_host_audio(&owner,0u,tone,48000u);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(delivered.load(std::memory_order_acquire)<48000u && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool complete=delivered.load(std::memory_order_acquire)>=48000u;
    stop_host_audio(&owner);CloseAudioDevice();
    std::cout<<(complete ? "Audio device test passed: 48000 stereo frames consumed by the output callback.\n"
                        : "Audio device test failed: output callback did not consume the tone.\n");
    return complete ? 0 : 1;
}
}
