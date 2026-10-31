// SPDX-License-Identifier: GPL-3.0-only
#include "mpeg_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ps2_stubs { void resetMpegStubState(); }
namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime MPEG decode regression: ")+message);
}
void invoke_native(PS2Runtime& runtime,uint32_t entry,std::array<uint32_t,6> args) {
    constexpr uint32_t stack=0x003589f0u,gp=0x003ac4f0u,returned=0x0019de98u;
    auto* memory=runtime.memory().getRDRAM();
    R5900Context context{};context.pc=entry;
    for (unsigned i=0;i<args.size();++i) context.r[4+i]=_mm_cvtsi32_si128(args[i]);
    context.r[28]=_mm_cvtsi32_si128(gp);context.r[29]=_mm_cvtsi32_si128(stack);
    context.r[31]=_mm_cvtsi32_si128(returned);
    for (unsigned step=0;step<4096u && context.pc!=returned;++step) {
        require(runtime.hasFunction(context.pc),"native input builder reached a missing entry");
        runtime.lookupFunction(context.pc)(memory,&context,&runtime);
    }
    require(context.pc==returned && getRegU32(&context,29)==stack
            && getRegU32(&context,28)==gp,"native input builder lost its caller frame");
}
std::vector<uint8_t> encode_test_movie(bool padded,int width,int height) {
    struct Encoder {
        AVCodecContext* context=nullptr;
        AVFrame* frame=av_frame_alloc();
        AVPacket* packet=av_packet_alloc();
        ~Encoder() { avcodec_free_context(&context);av_frame_free(&frame);av_packet_free(&packet); }
    } encoder;
    const auto* codec=avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    require(codec!=nullptr,"MPEG-2 test encoder is unavailable");
    encoder.context=avcodec_alloc_context3(codec);
    require(encoder.context && encoder.frame && encoder.packet,"cannot allocate the test encoder");
    auto& context=*encoder.context;
    context.width=width;context.height=height;context.pix_fmt=AV_PIX_FMT_YUV420P;
    context.time_base={1001,30000};context.framerate={30000,1001};
    context.gop_size=1;context.max_b_frames=0;context.thread_count=1;
    context.bit_rate=1000000;
    require(avcodec_open2(&context,codec,nullptr)>=0,"cannot open the test encoder");
    encoder.frame->format=context.pix_fmt;encoder.frame->width=width;encoder.frame->height=height;
    require(av_frame_get_buffer(encoder.frame,32)>=0,"cannot allocate the test image");
    std::vector<uint8_t> bytes;
    auto drain=[&] {
        for (;;) {
            const int result=avcodec_receive_packet(&context,encoder.packet);
            if (result==AVERROR(EAGAIN) || result==AVERROR_EOF) return;
            require(result>=0,"test encoding failed");
            bytes.insert(bytes.end(),encoder.packet->data,encoder.packet->data+encoder.packet->size);
            av_packet_unref(encoder.packet);
        }
    };
    for (int index=0;index<3;++index) {
        require(av_frame_make_writable(encoder.frame)>=0,"test image is not writable");
        for (int y=0;y<height;++y) for (int x=0;x<width;++x)
            encoder.frame->data[0][y*encoder.frame->linesize[0]+x]=uint8_t(40+((x/16)%2)*80+((y/16)%2)*20+index*8);
        for (int plane=1;plane<=2;++plane) for (int y=0;y<height/2;++y)
            std::memset(encoder.frame->data[plane]+y*encoder.frame->linesize[plane],128,width/2);
        encoder.frame->pts=index;
        require(avcodec_send_frame(&context,encoder.frame)>=0,"cannot submit the test image");
        drain();
    }
    require(avcodec_send_frame(&context,nullptr)>=0,"cannot finish test encoding");drain();
    if (padded) {
        bytes.insert(bytes.end(),{0,0,1,0xb7});
        bytes.resize((bytes.size()+2047u)&~size_t(2047u),0);
    }
    require(!bytes.empty() && bytes.size()<=65536u,"encoded test exceeds the native input ring");
    return bytes;
}
struct MovieObservation {
    unsigned pictures=0,returns=0,cycles=0,stage=0;
    bool workers=false;
    uint32_t stack=0x003589f0u,gp=0x003ac4f0u;
    unsigned width=32,height=32;
    R5900Context caller{};
    std::vector<uint8_t> lastPicture;
};
thread_local MovieObservation* observation=nullptr;
void verify_native_movie(bool refill,bool workers=false,bool fullSize=false) {
    auto owned=std::make_unique<PS2Runtime>();auto& runtime=*owned;
    require(runtime.memory().initialize(),"cannot allocate guest memory");
    auto* memory=runtime.memory().getRDRAM();auto& ee=runtime.eeScheduler();
    constexpr uint32_t handle=0x00290000u,work=0x002a0000u,descriptor=0x002b0000u;
    constexpr uint32_t input=0x00400000u,tags=0x00410000u,source=0x00420000u,image=0x00600000u;
    constexpr uint32_t stack=0x003589f0u,gp=0x003ac4f0u,finish=0x00190930u;
    MovieObservation observed;observed.workers=workers;observation=&observed;
    observed.width=fullSize ? 512u : 32u;observed.height=fullSize ? 448u : 32u;
    const unsigned imageBytes=observed.width*observed.height*4u;
    observed.lastPicture.resize(imageBytes);
    const unsigned slots=fullSize ? 32u : 4u;
    struct ObservationScope { ~ObservationScope() { observation=nullptr; } } scope;
    ps2_stubs::resetMpegStubState();
    auto invoke=[&](uint32_t entry,std::array<uint32_t,6> args) {
        invoke_native(runtime,entry,args);
    };
    invoke(0x00107978u,{handle,work,0x4000u,0,0,0});
    invoke(0x001974d0u,{descriptor,input,tags,slots,0u,0u});
    constexpr uint32_t backend=0x002f0000u,video=descriptor-0xa48u;
    if (refill) {
        for (unsigned i=0;i<4u;++i) {
            const std::array<uint8_t,4> end{0,0,1,0xb7};
            std::memcpy(memory+0x00387248u+i*4u,end.data(),end.size());
        }
        runtime.memory().write32(backend+0x1b30u,video);
        runtime.memory().write32(video+0x9ecu,1u);
        invoke(0x00107dd0u,{handle,1u,0x00198308u,backend,0,0});
    }
    const auto movie=encode_test_movie(!refill,observed.width,observed.height);
    require(!refill || movie.size()<2048u,"refill fixture must begin below one native DMA block");
    std::memcpy(memory+source,movie.data(),movie.size());
    std::memset(memory+image-16u,0xa5,imageBytes+32u);
    invoke(0x00198268u,{descriptor,source,uint32_t(movie.size()),0,0,0});
    invoke(0x00197848u,{descriptor,0,0,0,0,0});
    const auto beforeWork=std::vector<uint8_t>(memory+work,memory+work+0x4000u);
    struct Binding {
        PS2Runtime& runtime;uint32_t address;PS2Runtime::RecompiledFunction original;
        ~Binding() { runtime.replaceFunction(address,original); }
    } binding{runtime,finish,runtime.lookupFunction(finish)},
      initialized{runtime,0x001908e8u,runtime.lookupFunction(0x001908e8u)},
      serviced{runtime,0x00190920u,runtime.lookupFunction(0x00190920u)},
      callback{runtime,0x0018a808u,runtime.lookupFunction(0x0018a808u)};
    runtime.replaceFunction(finish,[](uint8_t*,R5900Context* context,PS2Runtime* active) {
        auto* memory=active->memory().getRDRAM();auto& ee=active->eeScheduler();
        auto& result=*observation;const unsigned pictures=result.pictures;
        require(getRegU32(context,2)==1u && getRegU32(context,29)==result.stack
                && getRegU32(context,28)==result.gp,"picture decoding lost SDK success or its caller frame");
        if (!result.workers)
            require(ee.currentVSyncTick()==0u,"native decoder inserted display waits into synchronous decoding");
        if (pictures==3u) {
            require(active->memory().read32(work)==1u,"native decoder did not report end of stream");
            require(std::memcmp(memory+image,result.lastPicture.data(),result.lastPicture.size())==0,
                    "end of stream overwrote the last decoded picture");
        } else {
            require(active->memory().read32(handle)==result.width && active->memory().read32(handle+4u)==result.height
                    && active->memory().read32(handle+8u)==pictures,"native picture dimensions or index are wrong");
            for (unsigned y=0;y<result.height;++y) for (unsigned x=0;x<result.width;++x) {
                const unsigned offset=(x/16u)*result.height*16u*4u+y*16u*4u+(x%16u)*4u;
                const int luma=40+int((x/16u)%2u)*80+int((y/16u)%2u)*20+int(pictures)*8;
                const int expected=(luma-16)*255/219;
                for (unsigned c=0;c<3u;++c)
                    require(std::abs(int(memory[image+offset+c])-expected)<=8,
                            "decoded pixels or macroblock-column placement are wrong");
                require(memory[image+offset+3u]==0x80,"decoded alpha is not GS-compatible");
            }
            ++result.pictures;
            if (result.pictures==3u)
                std::memcpy(result.lastPicture.data(),memory+image,result.lastPicture.size());
        }
        ++result.returns;
        if (result.workers) {
            require(ee.currentThreadId()==5 && ee.currentThread()->currentPriority==1,
                    "native movie decoding left its synchronous worker");
            if (result.returns<4u) {
                context->pc=0x00107bd0u;context->r[4]=_mm_cvtsi32_si128(handle);
                context->r[5]=_mm_cvtsi32_si128(image);context->r[6]=_mm_cvtsi32_si128(result.width*result.height/256u);
                context->r[31]=_mm_cvtsi32_si128(finish);return;
            }
            *context=result.caller;context->pc=getRegU32(context,31);
            context->r[2]=_mm_cvtsi32_si128(1u);
            return;
        }
        if (result.returns==4u) { active->requestStop();return; }
        invoke_native(*active,0x00197a48u,{descriptor,0,0,0,0,0});
        invoke_native(*active,0x00197b48u,{descriptor,0,0,0,0,0});
        invoke_native(*active,0x00197848u,{descriptor,0,0,0,0,0});
        context->pc=0x00107bd0u;context->r[4]=_mm_cvtsi32_si128(handle);
        context->r[5]=_mm_cvtsi32_si128(image);context->r[6]=_mm_cvtsi32_si128(result.width*result.height/256u);
        context->r[31]=_mm_cvtsi32_si128(finish);
    });
    R5900Context context{};context.pc=0x00107bd0u;
    context.r[4]=_mm_cvtsi32_si128(handle);context.r[5]=_mm_cvtsi32_si128(image);
    context.r[6]=_mm_cvtsi32_si128(observed.width*observed.height/256u);context.r[28]=_mm_cvtsi32_si128(gp);
    context.r[29]=_mm_cvtsi32_si128(stack);context.r[31]=_mm_cvtsi32_si128(finish);
    if (workers) {
        runtime.replaceFunction(0x001908e8u,[](uint8_t*,R5900Context* context,PS2Runtime*) {
            context->r[4]=_mm_setzero_si128();
            context->r[31]=_mm_cvtsi32_si128(0x00190920u);context->pc=0x00158fd8u;
        });
        runtime.replaceFunction(0x0018a808u,[](uint8_t*,R5900Context* context,PS2Runtime*) {
            auto& result=*observation;
            if (result.returns==4u) { context->r[2]=_mm_cvtsi32_si128(1u);context->pc=getRegU32(context,31);return; }
            result.caller=*context;result.stack=getRegU32(context,29);result.gp=getRegU32(context,28);
            context->pc=0x00107bd0u;context->r[4]=_mm_cvtsi32_si128(handle);
            context->r[5]=_mm_cvtsi32_si128(image);context->r[6]=_mm_cvtsi32_si128(result.width*result.height/256u);
            context->r[31]=_mm_cvtsi32_si128(finish);
        });
        runtime.replaceFunction(0x00190920u,[](uint8_t*,R5900Context* context,PS2Runtime* active) {
            auto& result=*observation;auto& ee=active->eeScheduler();
            require(ee.currentThreadId()==1 && ee.currentThread()->currentPriority==24,
                    "movie service did not restore the main thread's priority");
            if (result.stage==0u) {
                ee.setGsVSyncCallback(0x00159758u,0x003ac4f0u,0x01fefe00u);
                active->memory().write32(0x003c05e0u+6u*48u,0x0018a808u);
            }
            if (result.stage==2u && ++result.cycles==8u) { active->requestStop();return; }
            context->r[31]=_mm_cvtsi32_si128(0x00190920u);
            if (result.stage==1u) { result.stage=2u;context->pc=0x001599e0u; }
            else { result.stage=1u;context->r[4]=_mm_cvtsi32_si128(6u);context->pc=0x0016f6e8u; }
        });
        context.pc=0x0010a8b8u;context.r[29]=_mm_cvtsi32_si128(PS2_RAM_SIZE-16u);
        context.r[31]=_mm_cvtsi32_si128(0x001908e8u);
    }
    ee.reset(memory,context);
    std::mutex mutex;std::condition_variable changed;bool done=false,timedOut=false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock,std::chrono::seconds(4),[&] { return done; })) {
            timedOut=true;runtime.requestStop();
        }
    });
    std::exception_ptr failure;
    try { ee.run(); } catch (...) { failure=std::current_exception(); }
    { std::lock_guard lock(mutex);done=true; }changed.notify_one();watchdog.join();
    if (failure) std::rethrow_exception(failure);
    if (timedOut || observed.pictures!=3u || observed.returns!=4u) {
        const auto* worker=ee.thread(5);
        throw std::runtime_error("Runtime MPEG decode regression: movie handoff stalled; pictures="
            +std::to_string(observed.pictures)+" returns="+std::to_string(observed.returns)
            +" main_cycles="+std::to_string(observed.cycles)+" worker_wakeups="
            +std::to_string(worker ? worker->wakeupCount : 0u));
    }
    if (workers) {
        const auto* worker=ee.thread(5);
        require(observed.cycles==8u && worker && worker->wakeupCount==0u,
                "movie decoding accumulated worker wakeups or blocked subsequent frames");
    }
    require(runtime.memory().read32(work+0xf0u)==(image | 0x20000000u)
            && runtime.memory().read32(work+0xfcu)==observed.width*observed.height/256u,"decoder used foreign SDK output offsets");
    for (unsigned offset:{0xb0u,0xd8u,0xdcu,0xe0u,0xe4u})
        require(std::memcmp(memory+work+offset,beforeWork.data()+offset,4u)==0,
                "decoder overwrote this SDK's modes or picture-info record");
    for (unsigned i=0;i<16u;++i)
        require(memory[image-16u+i]==0xa5 && memory[image+imageBytes+i]==0xa5,
                "decoded picture crossed its destination bounds");
    require(std::memcmp(memory+source,movie.data(),movie.size())==0,"DMA changed its source stream");
    const uint32_t dmaBytes=refill ? 8192u : uint32_t(movie.size());
    require(runtime.memory().read32(0x1000b410u)==input+dmaBytes
            && runtime.memory().read32(0x1000b420u)==0u
            && (runtime.memory().read32(0x1000b400u)&0x100u)==0u
            && (runtime.memory().read32(0x1000e010u)&(1u<<5u))!=0u,
            "IPU DMA did not retire its bytes and completion status");
    if (refill)
        require(runtime.memory().read32(video+0x9f0u)==1u,
                "native refill callback did not execute its original end-of-input path");
    ps2_stubs::resetMpegStubState();
}
}
void verify_mpeg_decode() {
    verify_native_movie(false,true);
    verify_native_movie(true,true);
    verify_native_movie(false);
    verify_native_movie(true);
    verify_native_movie(false,true,true);
}
}
