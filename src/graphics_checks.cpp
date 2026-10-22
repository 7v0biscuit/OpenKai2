// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_checks.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime interlaced presentation regression: ")+message);
}
uint32_t pattern(uint32_t x,uint32_t y) {
    return ((x*7u+y*3u)&255u)|(((x+y*5u)&255u)<<8)|(((x*3u+y*11u)&255u)<<16);
}
void check(const PresentationFrame& frame,uint32_t height,uint32_t divisor,uint32_t originX=0,uint32_t originY=0,uint32_t width=512u) {
    require(frame && frame.width==width && frame.height==height,"wrong output dimensions");
    for (uint32_t y=0;y<height;++y) for (uint32_t x=0;x<width;++x) {
        const uint32_t expected=pattern(x+originX,y/divisor+originY);
        const auto* pixel=frame.pixels.data()+(size_t(y)*640u+x)*4u;
        require(pixel[0]==(expected&255u) && pixel[1]==((expected>>8)&255u)
                && pixel[2]==((expected>>16)&255u) && pixel[3]==255u,
                "display rows are stretched, shifted, or read from the adjacent buffer");
    }
}
}
void verify_interlaced_presentation() {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSCpuBackend backend;
    backend.Initialize(vram.data(),uint32_t(vram.size()));
    // The retail game reserves pages 0..55 for its 512x224 display and starts
    // its 512x448 render target immediately afterwards, at page 56. Reading
    // 448 display rows therefore exposes two unrelated images on screen.
    for (uint32_t y=0;y<448u;++y) for (uint32_t x=0;x<512u;++x)
        backend.WriteVram(0u,56u*32u,8u,x,y,0x80ff00ffu);
    for (uint32_t y=0;y<224u;++y) for (uint32_t x=0;x<512u;++x)
        backend.WriteVram(0u,0u,8u,x,y,pattern(x,y));
    GSPresentationRequest request{};
    request.pmode=0xffa1u; request.smode2=3u;
    request.dispfb1=0x1000u; request.dispfb2=request.dispfb1;
    request.display1=0x1bf1ff0003227cull; request.display2=request.display1;
    request.contextFrames[0].fbp=56u; request.contextFrames[0].fbw=8u;
    for (uint64_t tick:{0ull,1ull,2ull,3ull}) {
        request.vsyncTick=tick;
        check(backend.Present(request),448u,2u);
    }
    // Both read circuits and nonzero DBX/DBY must use the same memory-row
    // convention, with the offsets applied before line duplication.
    request.pmode=0xffa3u;
    check(backend.Present(request),448u,2u);
    request.dispfb1|=(uint64_t(9u)<<32)|(uint64_t(13u)<<43);
    request.display1=(uint64_t(499u)<<32)|(uint64_t(399u)<<44);
    request.pmode=1u;
    check(backend.Present(request),400u,2u,9u,13u,500u);
    // A black display is a valid fade, even when a neighboring context or a
    // heuristic preferred render target still contains an older image.
    request.dispfb1=0x1000u;request.display1=request.display2;
    request.hasPreferredSource=true;request.preferredDestFbp=0u;
    request.preferredSource.fbp=56u*32u;request.preferredSource.fbw=8u;
    for (uint32_t y=0;y<224u;++y) for (uint32_t x=0;x<512u;++x)
        backend.WriteVram(0u,0u,8u,x,y,0u);
    const auto black=backend.Present(request);
    require(black && !black.usedPreferred && black.sourceFbp==0u,"black display selected a different buffer");
    for (uint32_t y=0;y<448u;++y) for (uint32_t x=0;x<512u;++x) {
        const auto* pixel=black.pixels.data()+(size_t(y)*640u+x)*4u;
        require(pixel[0]==0u && pixel[1]==0u && pixel[2]==0u && pixel[3]==255u,
                "a black display leaked a stale render target");
    }
    // Progressive output with FFMD set still reads a full framebuffer.
    request.hasPreferredSource=false;request.smode2=2u;
    for (uint32_t y=0;y<448u;++y) for (uint32_t x=0;x<512u;++x)
        backend.WriteVram(0u,0u,8u,x,y,pattern(x,y));
    check(backend.Present(request),448u,1u);
    // Interlaced field mode reads alternating rows from a full-height buffer.
    request.smode2=1u;
    for (uint64_t tick:{0ull,1ull}) {
        request.vsyncTick=tick;
        const auto field=backend.Present(request);
        require(field && field.height==448u,"field mode lost its output");
        for (uint32_t y=0;y<448u;++y) for (uint32_t x=0;x<512u;++x) {
            const auto expected=pattern(x,(y&~1u)+uint32_t(tick));
            const auto* pixel=field.pixels.data()+(size_t(y)*640u+x)*4u;
            require(pixel[0]==(expected&255u) && pixel[1]==((expected>>8)&255u)
                    && pixel[2]==((expected>>16)&255u),"field parity changed");
        }
    }
}
}
