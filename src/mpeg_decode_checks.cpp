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
std::vector<uint8_t> encode_test_movie(bool padded) {
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
    context.width=32;context.height=32;context.pix_fmt=AV_PIX_FMT_YUV420P;
    context.time_base={1001,30000};context.framerate={30000,1001};
    context.gop_size=1;context.max_b_frames=0;context.thread_count=1;
    context.bit_rate=1000000;
    require(avcodec_open2(&context,codec,nullptr)>=0,"cannot open the test encoder");
    encoder.frame->format=context.pix_fmt;encoder.frame->width=32;encoder.frame->height=32;
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
        for (int y=0;y<32;++y) for (int x=0;x<32;++x)
            encoder.frame->data[0][y*encoder.frame->linesize[0]+x]=uint8_t(40+(x/16)*80+(y/16)*20+index*8);
        for (int plane=1;plane<=2;++plane) for (int y=0;y<16;++y)
            std::memset(encoder.frame->data[plane]+y*encoder.frame->linesize[plane],128,16);
        encoder.frame->pts=index;
        require(avcodec_send_frame(&context,encoder.frame)>=0,"cannot submit the test image");
        drain();
    }
    require(avcodec_send_frame(&context,nullptr)>=0,"cannot finish test encoding");drain();
    if (padded) {
        bytes.insert(bytes.end(),{0,0,1,0xb7});
        bytes.resize((bytes.size()+2047u)&~size_t(2047u),0);
    }
    require(!bytes.empty() && bytes.size()<=8192u,"encoded test exceeds the native input ring");
    return bytes;
}
void verify_native_movie(bool refill) {
    auto owned=std::make_unique<PS2Runtime>();auto& runtime=*owned;
    require(runtime.memory().initialize(),"cannot allocate guest memory");
    auto* memory=runtime.memory().getRDRAM();auto& ee=runtime.eeScheduler();
    constexpr uint32_t handle=0x00290000u,work=0x002a0000u,descriptor=0x002b0000u;
    constexpr uint32_t input=0x002c0000u,tags=0x002c3000u,source=0x002d0000u,image=0x002e0000u;
    constexpr uint32_t stack=0x003589f0u,gp=0x003ac4f0u,finish=0x00190920u;
    ps2_stubs::resetMpegStubState();
    auto invoke=[&](uint32_t entry,std::array<uint32_t,6> args) {
        invoke_native(runtime,entry,args);
    };
    invoke(0x00107978u,{handle,work,0x4000u,0,0,0});
    // Use the original CRI ring constructor, copy routine, and DMA submission.
    // No host sceMpegAddBs call or injected decoded frame supplies the decoder.
    invoke(0x001974d0u,{descriptor,input,tags,4u,0u,0u});
    constexpr uint32_t backend=0x002f0000u,video=descriptor-0xa48u;
    if (refill) {
        // The supported executable supplies four standard MPEG sequence-end
        // codes at the constant address used by the original CRI callback.
        for (unsigned i=0;i<4u;++i) {
            const std::array<uint8_t,4> end{0,0,1,0xb7};
            std::memcpy(memory+0x00387248u+i*4u,end.data(),end.size());
        }
        runtime.memory().write32(backend+0x1b30u,video);
        runtime.memory().write32(video+0x9ecu,1u);
        invoke(0x00107dd0u,{handle,1u,0x00198308u,backend,0,0});
    }
    const auto movie=encode_test_movie(!refill);
    require(!refill || movie.size()<2048u,"refill fixture must begin below one native DMA block");
    std::memcpy(memory+source,movie.data(),movie.size());
    std::memset(memory+image-16u,0xa5,4096u+32u);
    invoke(0x00198268u,{descriptor,source,uint32_t(movie.size()),0,0,0});
    invoke(0x00197848u,{descriptor,0,0,0,0,0});
    const auto beforeWork=std::vector<uint8_t>(memory+work,memory+work+0x4000u);
    struct Binding {
        PS2Runtime& runtime;PS2Runtime::RecompiledFunction original;
        ~Binding() { runtime.replaceFunction(finish,original); }
    } binding{runtime,runtime.lookupFunction(finish)};
    static thread_local unsigned pictures;
    pictures=0;
    // This hook only observes the SDK return and asks for the next picture.
    runtime.replaceFunction(finish,[](uint8_t*,R5900Context* context,PS2Runtime* active) {
        auto* memory=active->memory().getRDRAM();auto& ee=active->eeScheduler();
        require(getRegU32(context,2)==1u && getRegU32(context,29)==stack
                && getRegU32(context,28)==gp,"picture decoding lost SDK success or its caller frame");
        require(active->memory().read32(handle)==32u && active->memory().read32(handle+4u)==32u
                && active->memory().read32(handle+8u)==pictures,"native picture dimensions or index are wrong");
        for (unsigned y=0;y<32u;++y) for (unsigned x=0;x<32u;++x) {
            const unsigned offset=(x/16u)*32u*16u*4u+y*16u*4u+(x%16u)*4u;
            const int luma=40+int(x/16u)*80+int(y/16u)*20+int(pictures)*8;
            const int expected=(luma-16)*255/219;
            for (unsigned c=0;c<3u;++c)
                require(std::abs(int(memory[image+offset+c])-expected)<=8,
                        "decoded pixels or macroblock-column placement are wrong");
            require(memory[image+offset+3u]==0x80,"decoded alpha is not GS-compatible");
        }
        ++pictures;
        if (pictures==3u) { active->requestStop();return; }
        // The retail caller pauses IPU DMA after each picture and restores it
        // before the next. An IPU reset bit incorrectly retained on reads
        // would discard queued pictures during this ordinary context switch.
        invoke_native(*active,0x00197a48u,{descriptor,0,0,0,0,0});
        invoke_native(*active,0x00197b48u,{descriptor,0,0,0,0,0});
        invoke_native(*active,0x00197848u,{descriptor,0,0,0,0,0});
        ee.postEvent({EeEventType::VBlankStart,0,0});ee.postEvent({EeEventType::VBlankStart,0,0});
        context->pc=0x00107bd0u;context->r[4]=_mm_cvtsi32_si128(handle);
        context->r[5]=_mm_cvtsi32_si128(image);context->r[6]=_mm_cvtsi32_si128(4u);
        context->r[31]=_mm_cvtsi32_si128(finish);
    });
    R5900Context context{};context.pc=0x00107bd0u;
    context.r[4]=_mm_cvtsi32_si128(handle);context.r[5]=_mm_cvtsi32_si128(image);
    context.r[6]=_mm_cvtsi32_si128(4u);context.r[28]=_mm_cvtsi32_si128(gp);
    context.r[29]=_mm_cvtsi32_si128(stack);context.r[31]=_mm_cvtsi32_si128(finish);
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
    require(!timedOut && pictures==3u,"native decoder stalled after IPU DMA input");
    require(runtime.memory().read32(work+0xf0u)==(image | 0x20000000u)
            && runtime.memory().read32(work+0xfcu)==4u,"decoder used foreign SDK output offsets");
    for (unsigned offset:{0xb0u,0xd8u,0xdcu,0xe0u,0xe4u})
        require(std::memcmp(memory+work+offset,beforeWork.data()+offset,4u)==0,
                "decoder overwrote this SDK's modes or picture-info record");
    for (unsigned i=0;i<16u;++i)
        require(memory[image-16u+i]==0xa5 && memory[image+4096u+i]==0xa5,
                "decoded picture crossed its destination bounds");
    require(std::memcmp(memory+source,movie.data(),movie.size())==0,"DMA changed its source stream");
    // The original no-data callback fills the rest of the input ring to force
    // a final transfer; ordinary input submits only its complete blocks.
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
    verify_native_movie(false);
    verify_native_movie(true);
}
}
