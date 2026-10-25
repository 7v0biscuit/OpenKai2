// SPDX-License-Identifier: GPL-3.0-only
#include "fade_checks.h"
#include "ps2_runtime.h"
#include "startup_diagnostics.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sstream>

namespace openkai2 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime fade callback regression: ")+message);
}
}
void verify_fade_callbacks(PS2Runtime& runtime, uint8_t* memory) {
    constexpr uint32_t object=0x003b5630u, queue=0x00280000u, capacity=0x10000u;
    constexpr uint32_t gp=0x003ac4f0u, stack=0x01ffffc0u, returned=0x0019de98u;
    for (auto entry:translation::fade_callback_entries)
        require(runtime.hasFunction(entry), "reviewed fade renderer is not registered");
    std::memcpy(memory+0x003786e0u, translation::fade_callback_slots.data(),
                translation::fade_callback_slots.size()*4);
    auto put=[&](uint32_t address, uint32_t value) { runtime.memory().write32(address,value); };
    auto real=[&](uint32_t address, float value) { std::memcpy(memory+address,&value,4); };
    auto read_real=[&](uint32_t address) { float value; std::memcpy(&value,memory+address,4); return value; };
    // Screen-rectangle/color fixtures for the two solid overlays. These are
    // ordinary coordinates and colors; the original emitters build the packets.
    for (uint32_t address=0x00378770u; address<0x003787e0u; address+=16) {
        std::memset(memory+address,0,16);
        real(address+12,1.0f);
    }
    for (unsigned i=0; i<3; ++i) real(0x00378790u+i*4,1.0f);
    auto setup=[&](uint32_t mode, float level, float step) {
        std::memset(memory+object-16,0xa5,96);
        std::memset(memory+object,0,64);
        std::memcpy(memory+object,"# FADE #",8);
        put(object+0x1c,0x00249af0u);
        put(gp-0x759cu,object); put(gp-0x7598u,mode);
        real(gp-0x7594u,level); real(gp-0x7590u,step);
        put(gp-0x7588u,0u); put(gp-0x7584u,0u); put(gp-0x758cu,0u);
        put(gp-0x7bacu,0u); real(gp-0x7574u,0.5f);
        real(gp-0x7940u,640.0f); real(gp-0x793cu,448.0f);
        std::memset(memory+queue-16,0xa5,capacity+32);
        std::memset(memory+queue,0xcc,capacity);
        put(gp-0x7b80u,queue); put(gp-0x7b40u,0xffffffffu);
        put(gp-0x7b20u,0u); put(gp-0x7b1cu,0u); put(gp-0x7b18u,0u);
        // Use the game's batched queue mode, retaining the completed packets
        // in EE RAM for inspection without requiring a host graphics device.
        put(gp-0x7b28u,1u); put(gp-0x7b24u,queue);
        put(gp-0x7fe0u,0x01234560u);
    };
    auto invoke=[&](uint32_t entry, float alpha=0.0f) {
        R5900Context context{};
        context.pc=entry;
        context.r[4]=_mm_cvtsi32_si128(object);
        context.r[28]=_mm_cvtsi32_si128(gp);
        context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(returned);
        for (unsigned reg=16; reg<=23; ++reg)
            context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
        for (unsigned reg=20; reg<32; ++reg) context.f[reg]=float(reg)+0.125f;
        context.f[12]=alpha;
        context.vu0_vf[0]=_mm_set_ps(1.0f,0.0f,0.0f,0.0f);
        context.vu0_vf[20]=_mm_set_ps(0.0f,1.0f,16.0f,16.0f);
        context.vu0_vf[21]=_mm_set_ps(16777215.0f,0.0f,0.0f,0.0f);
        const auto saved=context;
        for (unsigned call=0; call<1024 && context.pc!=returned; ++call) {
            require(runtime.hasFunction(context.pc), "fade update reached an unregistered continuation");
            runtime.lookupFunction(context.pc)(memory,&context,&runtime);
        }
        require(context.pc==returned && getRegU32(&context,29)==stack
                && getRegU32(&context,31)==returned && getRegU32(&context,28)==gp,
                "fade call did not restore the outer frame");
        for (unsigned reg=16; reg<=23; ++reg)
            require(std::memcmp(&context.r[reg],&saved.r[reg],16)==0, "fade call changed a saved integer register");
        for (unsigned reg=20; reg<32; ++reg)
            require(context.f[reg]==saved.f[reg], "fade call changed a saved floating-point register");
        for (unsigned i=0; i<16; ++i)
            require(memory[queue-16+i]==0xa5 && memory[queue+capacity+i]==0xa5
                    && memory[object-16+i]==0xa5 && memory[object+64+i]==0xa5,
                    "fade call changed neighboring memory");
    };
    auto check_quad=[&](uint32_t color) {
        require(runtime.memory().read32(gp-0x7b80u)==queue+0xa0u, "solid fade packet length is wrong");
        require(runtime.memory().read32(queue)==0x10000009u
                && runtime.memory().read32(queue+0x70)==color
                && runtime.memory().read64(queue+0x78)==1u,
                "solid fade DMA tag or RGBAQ value is wrong");
        require(runtime.memory().read32(queue+0x80)==0u
                && runtime.memory().read32(queue+0x88)==5u
                && runtime.memory().read32(queue+0x90)==0x1c002800u
                && runtime.memory().read32(queue+0x98)==5u,
                "solid fade rectangle does not cover the viewport");
        require(runtime.memory().read32(gp-0x7fe0u)==0x01234560u, "fade renderer did not restore drawing state");
        require(std::all_of(memory+queue+0xa0,memory+queue+capacity,[](uint8_t b){return b==0xcc;}),
                "solid fade wrote beyond its packet");
    };
    // Execute the actual caller from the log, including its floating-point
    // delay-slot argument and the continuation after JALR at 0x00249B84.
    setup(10,0.75f,0.25f); invoke(0x00249af0u);
    require(read_real(gp-0x7594u)==0.5f, "fade update did not advance its level");
    check_quad(0x40000000u);
    setup(9,0.75f,0.25f); invoke(0x00249af0u); check_quad(0x40ffffffu);
    setup(0,0.75f,0.25f); put(gp-0x7584u,0x002492d0u);
    invoke(0x00249af0u); check_quad(0x40000000u);

    setup(10,0.125f,0.25f); put(gp-0x7588u,0x00167188u);
    put(0x0035fb24u,0xdeadbeefu); invoke(0x00249af0u);
    require(read_real(gp-0x7594u)==0.0f && runtime.memory().read32(object+0x1c)==0x00249d90u
            && runtime.memory().read32(gp-0x7588u)==0u && runtime.memory().read32(0x0035fb24u)==object,
            "fade completion did not clamp, dispatch, and clear its callback");
    check_quad(0u);
    const uint32_t end=runtime.memory().read32(gp-0x7b80u);
    put(0x0035fb24u,0xdeadbeefu); invoke(runtime.memory().read32(object+0x1c));
    require(runtime.memory().read32(gp-0x7b80u)==end && runtime.memory().read32(0x0035fb24u)==0xdeadbeefu,
            "completed fade emitted another packet or completion callback");

    setup(1,0.75f,0.25f); invoke(0x00249af0u);
    const uint32_t radial_end=runtime.memory().read32(gp-0x7b80u);
    require(radial_end>queue && radial_end<=queue+capacity && ((radial_end-queue)&15u)==0u
            && read_real(gp-0x7574u)<0.5f, "active radial fade did not build a bounded draw queue");

    // Cover every shared slot's transparent path and the textured renderer's
    // initial delay without constructing unrelated texture resources.
    for (uint32_t mode=1; mode<20; ++mode) {
        if (mode==9 || mode==10) continue;
        setup(mode,mode<12 ? 0.0f : 0.1f,0.0f); invoke(0x00249af0u);
        require(runtime.memory().read32(gp-0x7b80u)==queue, "inactive fade emitted a draw packet");
        if (mode<12) require(read_real(gp-0x7574u)<0.5f, "shared radial fade callback did not execute");
    }

    // Follow an original solid overlay through DMA, GIF decoding, rasterization,
    // and host presentation. No host window or disc assets are needed.
    setup(9,1.0f,0.0f);
    put(gp-0x7fe0u,0u);
    invoke(0x00249af0u);
    std::memset(memory+queue+0xa0u,0,16);
    put(queue+0xa0u,0x70000000u); // Terminate the generated CNT DMA chain.
    require(runtime.syncCoreSubsystems(), "graphics subsystem initialization failed");
    const auto writes=runtime.memory().gsWriteCount();
    runtime.memory().write64(0x12000000u,1u);
    runtime.memory().write64(0x12000070u,10u<<9);
    runtime.memory().write64(0x12000080u,(uint64_t(639u)<<32)|(uint64_t(447u)<<44));
    runtime.memory().write32(0x12000004u,0u);
    require(runtime.memory().gsWriteCount()==writes+4u,
            "32/64-bit display writes were missing from the GS counter");
    runtime.gs().writeRegister(0x4cu,uint64_t(10u)<<16); // FRAME_1, PSMCT32
    runtime.gs().writeRegister(0x4eu,uint64_t(1u)<<32); // Mask depth writes.
    runtime.memory().write32(0x1000e000u,1u);
    R5900Context dmaContext{};
    runtime.kickGifDmaChainFromMMIO(memory,&dmaContext,0u,0u,queue,0x105u);
    runtime.gs().latchHostPresentationFrame();
    std::vector<uint8_t> pixels;
    uint32_t width=0,height=0;
    require(runtime.gs().copyLatchedHostPresentationFrame(pixels,width,height),
            "generated overlay did not produce a host frame");
    require(width==640u && height==448u && pixels.size()==size_t(width)*height*4,
            "generated overlay produced the wrong viewport");
    for (uint32_t y: {0u,224u,447u}) for (uint32_t x: {0u,320u,639u}) {
        const size_t offset=(size_t(y)*width+x)*4;
        require(pixels[offset]==255u && pixels[offset+1]==255u && pixels[offset+2]==255u,
                "generated overlay did not reach the presented pixels");
    }
    std::ostringstream report;
    report_startup_graphics(runtime.gs(),123u,report);
    if (report.str().find("frame=1 size=640x448")==std::string::npos
            || report.str().find("nonblack_pixels=286720")==std::string::npos
            || report.str().find("kind=draw")==std::string::npos)
        throw std::runtime_error("Runtime fade callback regression: unexpected graphics report: "+report.str());
    // A new original packet must update the presented frame, not reuse the
    // previous white frame. Full black also exercises zero-valued RGB output.
    setup(10,1.0f,0.0f); put(gp-0x7fe0u,0u); invoke(0x00249af0u);
    std::memset(memory+queue+0xa0u,0,16); put(queue+0xa0u,0x70000000u);
    runtime.kickGifDmaChainFromMMIO(memory,&dmaContext,0u,0u,queue,0x105u);
    runtime.gs().latchHostPresentationFrame();
    require(runtime.gs().copyLatchedHostPresentationFrame(pixels,width,height),
            "black overlay lost the host frame");
    for (size_t i=0;i<pixels.size();i+=4)
        require(pixels[i]==0u && pixels[i+1]==0u && pixels[i+2]==0u,
                "black overlay did not replace the prior presented frame");
    std::ostringstream black;
    report_startup_graphics(runtime.gs(),124u,black);
    require(black.str().find("nonblack_pixels=0 ")!=std::string::npos,
            "black frame was reported as missing or nonblack");
}
}
