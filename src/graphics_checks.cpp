// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_checks.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <algorithm>
#include <cmath>
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
void check_interlaced_rows(const PresentationFrame& frame,uint32_t height,uint32_t field,
                           uint32_t originX=0,uint32_t originY=0,uint32_t width=512u) {
    require(frame && frame.width==width && frame.height==height,"wrong interlaced dimensions");
    for (uint32_t y=0;y<height;++y) for (uint32_t x=0;x<width;++x) {
        const uint32_t row=y/2u;
        const bool interpolate=(y&1u)!=field;
        const uint32_t other=field ? (row==0u ? 0u : row-1u) : std::min(row+1u,(height-1u)/2u);
        const uint32_t a=pattern(x+originX,row+originY);
        const uint32_t b=pattern(x+originX,other+originY);
        const auto* pixel=frame.pixels.data()+(size_t(y)*640u+x)*4u;
        for (unsigned channel=0;channel<3u;++channel) {
            const auto ca=(a>>(8u*channel))&255u,cb=(b>>(8u*channel))&255u;
            require(pixel[channel]==(interpolate ? (ca+cb+1u)/2u : ca),
                    "interlaced output did not retain the field's scanline positions");
        }
        require(pixel[3]==255u,"interlaced alpha changed");
    }
}
void check_field_stability() {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSCpuBackend backend;
    backend.Initialize(vram.data(),uint32_t(vram.size()));
    GSPresentationRequest request{};
    request.pmode=0xffa1u;request.smode2=3u;request.dispfb1=0x1000u;
    request.display1=0x1bf1ff0003227cull;
    // A stationary vertical ramp sampled at alternating even and odd source
    // lines must retain its position. Duplicating each field row shifts it by
    // one output line every field, reproducing the reported slight shake.
    for (uint64_t tick:{0ull,1ull,2ull,3ull}) {
        request.vsyncTick=tick;
        for (uint32_t y=0;y<224u;++y) for (uint32_t x=0;x<512u;++x) {
            const uint32_t value=2u*y+uint32_t(tick&1u);
            backend.WriteVram(0u,0u,8u,x,y,0x80000000u|(value<256u ? value : 255u));
        }
        const auto frame=backend.Present(request);
        require(frame && frame.width==512u && frame.height==448u,"field stability lost output");
        for (uint32_t y=1u;y<254u;++y) for (uint32_t x=0;x<512u;++x) {
            const auto* pixel=frame.pixels.data()+(size_t(y)*640u+x)*4u;
            require(pixel[0]==y && pixel[1]==0u && pixel[2]==0u && pixel[3]==255u,
                    "a stationary interlaced image moved between fields");
        }
    }
}
void check_sprite_coordinates() {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSCpuBackend backend;
    backend.Initialize(vram.data(),uint32_t(vram.size()));
    for (uint32_t y=0;y<32u;++y) for (uint32_t x=0;x<64u;++x)
        backend.WriteVram(0u,4096u,1u,x,y,pattern(x,y));
    GSPrimitiveBatch batch{};
    batch.vertexCount=2;
    auto& state=batch.state;
    state.prim.type=GS_PRIM_SPRITE;state.prim.tme=true;state.prim.fst=true;
    state.context.frame.fbp=56u;state.context.frame.fbw=8u;
    state.context.zbuf.zmask=true;state.context.test=0x30000u;
    state.context.scissor={0u,63u,0u,31u};
    state.context.xyoffset.ofx=28672u;state.context.xyoffset.ofy=29184u;
    state.context.tex0.tbp0=4096u;state.context.tex0.tbw=1u;
    state.context.tex0.tfx=1u;state.textureWidth=64u;state.textureHeight=32u;
    state.colclamp=1u;
    for (auto& vertex:batch.vertices) vertex.r=vertex.g=vertex.b=vertex.a=128u;
    // Retail logos start at -7/16, -1/2 relative to XYOFFSET. Pixel centers
    // are integral GS coordinates: ceil the edges and interpolate from the
    // original fractional position, retaining all four UV fraction bits.
    for (unsigned variant=0;variant<3u;++variant) {
        backend.ClearFramebuffer(state.context,0x80abcdefu);
        const float left=variant==0u ? -0.4375f : 0.0f;
        const float top=variant==0u ? -0.5f : 0.0f;
        batch.vertices[0].x=1792.0f+left;batch.vertices[0].y=1824.0f+top;
        batch.vertices[1].x=1824.0f+left;batch.vertices[1].y=1840.0f+top;
        batch.vertices[0].u=variant==0u ? 1u : variant==1u ? 12u : 0u;
        batch.vertices[0].v=0u;
        batch.vertices[1].u=batch.vertices[0].u+(variant==1u ? 256u : 512u);
        batch.vertices[1].v=256u;
        backend.Submit(batch);
        for (uint32_t y=0;y<16u;++y) for (uint32_t x=0;x<32u;++x) {
            const uint32_t sx=variant==1u ? uint32_t(0.75f+float(x)*0.5f) : x;
            require((backend.ReadVram(0u,56u*32u,8u,x,y)&0xffffffu)==pattern(sx,y),
                    "fractional sprite coordinates or UVs shifted the image");
        }
        require(backend.ReadVram(0u,56u*32u,8u,32u,0u)==0x80abcdefu
                && backend.ReadVram(0u,56u*32u,8u,0u,16u)==0x80abcdefu,
                "sprite covered its exclusive bottom or right edge");
    }
}
void check_bilinear_samples() {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    auto rgba=[](uint32_t x,uint32_t y) {
        return pattern(x,y)|(((x*13u+y*17u)&255u)<<24u);
    };
    for (unsigned y=0;y<64u;++y) for (unsigned x=0;x<128u;++x)
        backend.WriteVram(0u,4096u,2u,x,y,rgba(x,y));
    GSPrimitiveBatch batch{};batch.vertexCount=2;
    auto& state=batch.state;
    state.prim.type=GS_PRIM_SPRITE;state.prim.tme=true;state.prim.fst=true;
    state.linearFilter=true;state.colclamp=1u;
    state.context.frame.fbp=56u;state.context.frame.fbw=8u;
    state.context.zbuf.zmask=true;state.context.test=0x30000u;
    state.context.scissor={0u,127u,0u,63u};
    state.context.tex0.tbp0=4096u;state.context.tex0.tbw=2u;
    state.context.tex0.tfx=1u;state.context.tex0.tcc=true;
    state.textureWidth=128u;state.textureHeight=64u;
    batch.vertices[1].x=128.0f;batch.vertices[1].y=64.0f;
    for (unsigned fraction:{8u,12u}) {
        batch.vertices[0].u=batch.vertices[0].v=fraction;
        batch.vertices[1].u=128u*16u+fraction;batch.vertices[1].v=64u*16u+fraction;
        backend.TextureFlush();backend.Submit(batch);
        for (unsigned y=0;y<64u;++y) for (unsigned x=0;x<128u;++x) {
            const uint32_t samples[]={rgba(x,y),rgba((x+1u)%128u,y),
                rgba(x,(y+1u)%64u),rgba((x+1u)%128u,(y+1u)%64u)};
            const uint32_t actual=backend.ReadVram(0u,56u*32u,8u,x,y);
            for (unsigned shift=0;shift<32u;shift+=8u) {
                const float weight=fraction==8u ? 0.0f : 0.25f;
                auto channel=[&](unsigned i) { return float((samples[i]>>shift)&255u); };
                const float top=channel(0)+(channel(1)-channel(0))*weight;
                const float bottom=channel(2)+(channel(3)-channel(2))*weight;
                const uint32_t expected=uint32_t(std::lround(top+(bottom-top)*weight));
                require(((actual>>shift)&255u)==expected,
                        "aligned or fractional bilinear samples changed across a texture page or wrap boundary");
            }
        }
    }
}
void check_texture_cache_scope() {
    std::vector<uint8_t> vram(3u*GSMem::TexturePageCache::kPageSize);
    GSMem::TexturePageCache scalar,scoped;
    for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*7u+(i>>13u));
    require(*scalar.Resolve(vram.data(),17u)==*scoped.Resolve(vram.data(),17u),"initial cache fill changed");
    vram[17u]^=255u;
    scoped.BeginReadOnly();
    for(unsigned address:{17u,8192u+19u,17u,16384u+23u,16384u+41u,8192u+31u})
        require(*scalar.Resolve(vram.data(),address)==*scoped.Resolve(vram.data(),address),
                "read-only texture scope lost initial stale bytes or eviction order");
    scoped.EndReadOnly(vram.data());
    vram[8192u+31u]^=255u;
    for(unsigned address:{8192u+31u,17u,8192u+31u})
        require(*scalar.Resolve(vram.data(),address)==*scoped.Resolve(vram.data(),address),
                "read-only texture scope changed the final cached page");
    scoped.Invalidate();scoped.BeginReadOnly();scoped.EndReadOnly(vram.data());
    require(*scoped.Resolve(vram.data(),17u)==vram[17u],"empty scope populated an invalid cache page");
}
void check_bilinear_rounding() {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    GSPrimitiveBatch batch{};batch.vertexCount=2;auto& state=batch.state;auto& ctx=state.context;
    state.prim.type=GS_PRIM_SPRITE;state.prim.tme=true;state.linearFilter=true;
    state.textureWidth=state.textureHeight=2u;state.colclamp=1u;
    ctx.frame.fbp=56u;ctx.frame.fbw=1u;ctx.zbuf.zmask=true;ctx.test=0x30000u;
    ctx.scissor={0u,0u,0u,0u};ctx.tex0.tbp0=4096u;ctx.tex0.tbw=1u;ctx.tex0.tfx=1u;ctx.tex0.tcc=true;
    batch.vertices[1].x=batch.vertices[1].y=1.0f;
    uint32_t seed=0x92348712u;
    auto random=[&]{seed=seed*1664525u+1013904223u;return seed;};
    for(unsigned test=0;test<8192u;++test) {
        uint32_t colors[4];for(auto& color:colors)color=random();
        float fx,fy;
        state.prim.fst=test<4096u;
        if(state.prim.fst) {
            fx=float(test&15u)/16.0f;fy=float((test>>4u)&15u)/16.0f;
            for(auto& vertex:batch.vertices){vertex.u=uint16_t(8u+(test&15u));vertex.v=uint16_t(8u+((test>>4u)&15u));}
        } else {
            fx=float(random()&65535u)/65536.0f;fy=float(random()&65535u)/65536.0f;
            for(auto& vertex:batch.vertices){vertex.s=(0.5f+fx)/2.0f;vertex.t=(0.5f+fy)/2.0f;vertex.q=1.0f;}
        }
        for(unsigned i=0;i<4u;++i)backend.WriteVram(0u,4096u,1u,i&1u,i>>1u,colors[i]);
        backend.TextureFlush();backend.Submit(batch);
        uint32_t expected=0;
        for(unsigned channel=0;channel<4u;++channel) {
            auto c=[&](unsigned i){return float((colors[i]>>(8u*channel))&255u);};
            const float top=c(0)+(c(1)-c(0))*fx,bottom=c(2)+(c(3)-c(2))*fx;
            const auto value=std::clamp<long>(std::lround(top+(bottom-top)*fy),0l,255l);
            expected|=uint32_t(value)<<(8u*channel);
        }
        require(backend.ReadVram(0u,56u*32u,1u,0u,0u)==expected,
                "vector bilinear RGBA rounding differs from scalar sampling");
    }
}
uint64_t sprite_pipeline_hash(unsigned variant) {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    GSPrimitiveBatch batch{};batch.vertexCount=2;auto& state=batch.state;
    state.prim.type=GS_PRIM_SPRITE;state.prim.tme=variant<32u;state.prim.fst=true;
    state.linearFilter=(variant&1u)!=0u;state.colclamp=1u;
    auto& ctx=state.context;
    ctx.frame.fbp=56u;ctx.frame.fbw=8u;ctx.zbuf.zbp=120u;
    ctx.zbuf.psm=GS_PSM_Z32;ctx.zbuf.zmask=true;ctx.test=0x30000u;
    ctx.scissor={0u,127u,0u,63u};ctx.tex0.tbp0=4096u;ctx.tex0.tbw=2u;
    ctx.tex0.psm=GS_PSM_CT32;ctx.tex0.tfx=1u;ctx.tex0.tcc=true;
    state.textureWidth=128u;state.textureHeight=64u;
    switch ((variant/2u)%16u) {
    case 1: state.prim.abe=true;ctx.alpha=0u;break; // Identity blend.
    case 2: state.prim.abe=true;ctx.alpha=0x44u;break;
    case 3: ctx.test|=1u|(5u<<1u)|(100u<<4u);break;
    case 4: ctx.fba=1u;break;
    case 5: ctx.frame.fbmsk=0x00ff00ffu;break;
    case 6: ctx.test|=1u<<14u;break;
    case 7: state.prim.fge=true;state.fogR=71;state.fogG=83;state.fogB=97;break;
    case 8: ctx.test=0x50000u;break; // GE depth comparison.
    case 9: ctx.test=0x70000u;ctx.zbuf.psm=GS_PSM_Z16;break;
    case 10: ctx.test=0x10000u;break; // Never pass depth.
    case 11: ctx.frame.psm=GS_PSM_CT24;break;
    case 12: ctx.zbuf.zmask=false;ctx.tex0.tfx=0u;ctx.tex0.tcc=false;break;
    case 13: ctx.tex0.tbp0=4097u;ctx.tex0.psm=GS_PSM_CT24;state.texa.ta0=73u;break;
    case 14: ctx.clamp=3u|(3u<<2u)|(63ull<<4u)|(31ull<<24u);break;
    case 15: ctx.tex0.tbp0=56u*32u;break; // Framebuffer/texture feedback.
    }
    for (unsigned y=0;y<64u;++y) for (unsigned x=0;x<128u;++x) {
        backend.WriteVram(ctx.frame.psm,56u*32u,8u,x,y,0x80907050u);
        backend.WriteVram(ctx.zbuf.psm,120u*32u,8u,x,y,(x+3u*y)%13u);
    }
    for (unsigned y=0;y<64u;++y) for (unsigned x=0;x<128u;++x)
        backend.WriteVram(ctx.tex0.psm,ctx.tex0.tbp0,2u,x,y,
                          pattern(x,y)|(((x*13u+y*17u)&255u)<<24u));
    for (auto& vertex:batch.vertices) {
        vertex.r=113u;vertex.g=128u;vertex.b=157u;vertex.a=117u;vertex.z=7.0;vertex.fog=173u;
    }
    batch.vertices[1].x=128.0f;batch.vertices[1].y=64.0f;
    batch.vertices[0].u=batch.vertices[0].v=8u;
    batch.vertices[1].u=128u*16u+8u;batch.vertices[1].v=64u*16u+8u;
    backend.Submit(batch);
    // Change VRAM without TEXFLUSH, then cross cache-page and wrap boundaries.
    for (unsigned i=0;i<8u;++i)
        backend.WriteVram(ctx.tex0.psm,ctx.tex0.tbp0,2u,(i*23u)%128u,(i*11u)%64u,0x8071e219u+i);
    batch.vertices[0].x=-0.4375f;batch.vertices[0].y=-0.5f;
    batch.vertices[1].x=127.5625f;batch.vertices[1].y=63.5f;
    batch.vertices[0].u=batch.vertices[0].v=12u;
    batch.vertices[1].u=128u*16u+12u;batch.vertices[1].v=64u*16u+12u;
    backend.Submit(batch);
    std::vector<uint8_t> snapshot;backend.SnapshotVram(snapshot);
    uint64_t hash=14695981039346656037ull;
    for (const auto byte:snapshot) hash=(hash^byte)*1099511628211ull;
    return hash;
}
void check_sprite_pipeline() {
    // Complete VRAM snapshots from the preceding scalar renderer (9ce860a),
    // covering fast-path eligibility, depth, masks, blending, cache feedback,
    // texel centers, fractional coordinates, page crossings, and wrapping.
    constexpr uint64_t expected[]={
        0x6dc38cd12c2caad7ull,0xcf069afb661d0adcull,0x6dc38cd12c2caad7ull,0xcf069afb661d0adcull,
        0x857be7086093fbabull,0xea10bbc36f895c34ull,0x3d61446891bed2ebull,0x12cb6605727376eeull,
        0xc2ce7e9c69963cd7ull,0x457554df6d8d99dcull,0x8559df93d60219abull,0x53f7605ef8367699ull,
        0x17f842cb6011516bull,0x17f842cb6011516bull,0x39ee0c2e0e151e26ull,0x16160cfaf892fe8bull,
        0x18f25febae5bae99ull,0x7e267d573a1996a9ull,0x380d895b49eaef1ull,0xb45343935ef19b64ull,
        0x17f842cb6011516bull,0x17f842cb6011516bull,0x297dfb1a7080b807ull,0xae00998d530ac954ull,
        0x507d2e152a052adbull,0xfa945c21c5edffd5ull,0xf3a984d05277286eull,0x19841978e4c96f1dull,
        0x2f0d963bb31c9f6bull,0x23ae255a451a432bull,0xb537da85ac144ef6ull,0xbfa7d80952b681faull,
        0xb932e97fd271d16bull,0xb932e97fd271d16bull,0xb932e97fd271d16bull,0xb932e97fd271d16bull,
        0xbe2519791586d16bull,0xbe2519791586d16bull,0xb932e97fd271d16bull,0xb932e97fd271d16bull,
        0x5cfe48847971d16bull,0x5cfe48847971d16bull,0x1659a20f1cd4516bull,0x1659a20f1cd4516bull,
        0x17f842cb6011516bull,0x17f842cb6011516bull,0x110770923c81516bull,0x110770923c81516bull,
        0x759ad5731756db43ull,0x759ad5731756db43ull,0x977b47e89f242bf8ull,0x977b47e89f242bf8ull,
        0x17f842cb6011516bull,0x17f842cb6011516bull,0xe339880c2f93516bull,0xe339880c2f93516bull,
        0xc59521f0e3d4278dull,0xc59521f0e3d4278dull,0x1cd95a6689b25b72ull,0x1cd95a6689b25b72ull,
        0xb932e97fd271d16bull,0xb932e97fd271d16bull,0x8592208c19cf993cull,0x8592208c19cf993cull,
    };
    for (unsigned i=0;i<64u;++i)
        require(sprite_pipeline_hash(i)==expected[i],"sprite optimization changed VRAM or texture-cache behavior");
}
}
void verify_interlaced_presentation() {
    check_texture_cache_scope();
    check_bilinear_rounding();
    check_sprite_pipeline();
    check_sprite_coordinates();
    check_bilinear_samples();
    check_field_stability();
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
        check_interlaced_rows(backend.Present(request),448u,uint32_t(request.vsyncTick&1u));
    }
    // Both read circuits and nonzero DBX/DBY must use the same memory-row
    // convention, with the offsets applied before field interpolation.
    request.pmode=0xffa3u;
    check_interlaced_rows(backend.Present(request),448u,uint32_t(request.vsyncTick&1u));
    request.dispfb1|=(uint64_t(9u)<<32)|(uint64_t(13u)<<43);
    request.display1=(uint64_t(499u)<<32)|(uint64_t(399u)<<44);
    request.pmode=1u;
    check_interlaced_rows(backend.Present(request),400u,uint32_t(request.vsyncTick&1u),9u,13u,500u);
    request.dispfb1=0x1000u;
    for (const uint32_t height:{65u,127u,447u}) {
        request.display1=(uint64_t(499u)<<32)|(uint64_t(height-1u)<<44);
        for (uint64_t tick:{0ull,1ull}) {
            request.vsyncTick=tick;
            check_interlaced_rows(backend.Present(request),height,uint32_t(tick),0u,0u,500u);
        }
    }
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
