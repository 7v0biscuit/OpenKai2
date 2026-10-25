// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_checks.h"
#include "raster_shading_checks.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_frontend.h"
#include "guest_timing.h"
#include "raster_workers.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>

namespace openkai2 {
namespace {
uint64_t palette_pipeline_hash(unsigned variant,unsigned workers=1u) {
    const auto previous=set_raster_worker_limit(workers);
    struct Restore {unsigned previous;~Restore(){set_raster_worker_limit(previous);}} restore{previous};
    std::vector<uint8_t> vram(4u*1024u*1024u);
    for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*13u^(i>>9u));
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    GSCpuBackend other;other.Initialize(vram.data(),uint32_t(vram.size()));
    constexpr uint8_t formats[]={GS_PSM_T8,GS_PSM_T8H,GS_PSM_T4,GS_PSM_T4HL,GS_PSM_T4HH};
    constexpr uint8_t palettes[]={GS_PSM_CT32,GS_PSM_CT24,GS_PSM_CT16,GS_PSM_CT16S,GS_PSM_Z32};
    GSPrimitiveBatch b{};b.vertexCount=2u;auto& s=b.state;auto& c=s.context;auto& tex=c.tex0;
    s.prim.type=GS_PRIM_SPRITE;s.prim.tme=s.prim.fst=s.prim.abe=true;s.colclamp=1u;
    s.linearFilter=variant&1u;s.textureWidth=s.textureHeight=128u;
    c.frame.fbp=56u;c.frame.fbw=8u;c.zbuf.zbp=168u;c.zbuf.zmask=true;c.test=0x30000u;
    c.alpha=0x44u;c.scissor={0u,511u,0u,95u};
    tex.psm=formats[variant%5u];tex.cpsm=palettes[(variant/5u)%5u];tex.csm=variant/25u;
    tex.tbp0=10240u;tex.tbw=2u;tex.tfx=1u;tex.tcc=true;tex.cbp=16000u;tex.csa=uint8_t((variant*7u)%32u);
    GSTexClutReg clut{};clut.cbw=2u;clut.cou=1u;clut.cov=3u;
    for(unsigned y=0;y<128u;++y)for(unsigned x=0;x<128u;++x)
        backend.WriteVram(tex.psm,tex.tbp0,2u,x,y,(x*11u+y*7u)&255u);
    auto fill=[&](unsigned seed){for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<128u;++x){
        const auto value=(x+y)%7u==0u ? 0u : ((x*13u+seed)&255u)|(((y*17u+seed)&255u)<<8u)
            |(((x*19u+seed)&255u)<<16u)|(((x+y)&1u ? 0x80u : 0u)<<24u);
        backend.WriteVram(tex.cpsm==GS_PSM_Z32 ? GS_PSM_CT32 : tex.cpsm,tex.cbp,2u,x,y,value);
    }};
    for(auto& v:b.vertices){v.r=73u;v.g=127u;v.b=191u;v.a=128u;v.q=1.0f;v.z=1000u;v.u=v.v=12u;}
    b.vertices[1].x=512.0f;b.vertices[1].y=96.0f;b.vertices[1].u=512u*16u+12u;b.vertices[1].v=96u*16u+12u;
    fill(23u);tex.cld=2u;backend.LoadClut(tex,clut);
    uint64_t hash=14695981039346656037ull;
    auto draw=[&](GSCpuBackend& target){target.Submit(b);for(auto byte:vram)hash=(hash^byte)*1099511628211ull;};
    draw(backend);draw(backend); // Reuse the same latched palette and lookup key.
    fill(67u);tex.cld=4u;backend.LoadClut(tex,clut);draw(backend); // CBP0 match: retain the old palette.
    s.texa.ta0=53u;s.texa.ta1=211u;s.texa.aem=true;draw(backend);
    tex.cld=3u;backend.LoadClut(tex,clut);draw(backend); // Explicit reload also remembers CBP1.
    fill(101u);tex.cld=5u;backend.LoadClut(tex,clut);draw(backend); // CBP1 match: retain latched bytes.
    tex.csa^=31u;draw(backend);tex.cld=1u;backend.LoadClut(tex,clut);draw(backend);
    // A second backend has a different palette, even with identical TEX0/TEXA.
    fill(149u);other.LoadClut(tex,clut);draw(other);draw(backend);
    backend.Reset();draw(backend);backend.LoadClut(tex,clut);draw(backend);
    // Reinitialize at the same backend address and change lookup state again.
    backend.Initialize(vram.data(),uint32_t(vram.size()));s.texa.aem=false;s.texa.ta0=127u;
    draw(backend);tex.csm^=1u;backend.LoadClut(tex,clut);draw(backend);
    return hash;
}

void check_draw_history_sampling() {
    struct Disable {~Disable(){enable_guest_timings(false);}} disable;
    for(unsigned variant=0;variant<4u;++variant){
        std::vector<uint8_t> expected;
        for(bool sampled:{false,true}){
            enable_guest_timings(sampled);
            std::vector<uint8_t> vram(4u*1024u*1024u,0x35u);
            auto gs=std::make_unique<GS>();gs->init(vram.data(),uint32_t(vram.size()));gs->setDebugHistoryPaused(false);
            gs->writeRegister(0x4cu,56u|(uint64_t(2u)<<16u));
            gs->writeRegister(0x4eu,168u|(1ull<<32u));
            gs->writeRegister(0x40u,(127ull<<16u)|(63ull<<48u));
            gs->writeRegister(0x47u,0x30000u);gs->writeRegister(0x46u,1u);gs->writeRegister(0x42u,0x44u);
            const bool triangle=(variant&1u)!=0u;
            gs->writeRegister(0x00u,(triangle ? 3u : 6u)|((variant&2u) ? 64u : 0u));
            auto vertex=[&](unsigned x,unsigned y){gs->writeRegister(0x05u,uint64_t(x*16u)|(uint64_t(y*16u)<<16u)|(1000ull<<32u));};
            for(unsigned i=0;i<128u;++i){
                gs->writeRegister(0x01u,(uint64_t(0x3f800000u)<<32u)|0x80000000u|(i*71u&255u)|((i*17u&255u)<<8u)|((i*31u&255u)<<16u));
                const unsigned x=3u+i%12u*7u,y=3u+i/12u*5u;
                vertex(x,y);vertex(x+6u,triangle ? y+1u : y+4u);if(triangle)vertex(x+1u,y+4u);
            }
            const auto history=gs->getDebugHistory();const auto draws=std::count_if(history.begin(),history.end(),[](const auto& row){return row.kind==GSDebugEventKind::Draw;});
            if(draws!=(sampled ? 2 : 128))throw std::runtime_error("Draw history did not retain its intended sampling rate");
            if(!sampled){
                if(std::all_of(vram.begin(),vram.end(),[](uint8_t value){return value==0x35u;}))
                    throw std::runtime_error("Draw history fixture did not render any pixels");
                expected=vram;
            }
            else if(vram!=expected)throw std::runtime_error("Draw history sampling changed rendered VRAM");
        }
    }
}

void check_prepared_palette() {
    // Full VRAM snapshots of fourteen draws per fixture from the previous
    // renderer, covering all indexed formats, palette formats, and CSM modes.
    constexpr uint64_t expected[]={
        0x6faa381880d34ff1ull,0x399b4f485fcdef41ull,0xfafc5238b0dadc8bull,0xa641a60bb2990cedull,
        0xd0d33d5aa12e5c8bull,0xd48f1060b6f32b93ull,0x8abbfdabb9c43b78ull,0x6432a1721517ead2ull,
        0xb730ac02270f56beull,0xe5ad6c68a9d0ded2ull,0xb0f51fc6f0f406b8ull,0xf180d346e6907fc7ull,
        0x67e8c57ee183f6e4ull,0xd704c34c138df3efull,0xd819a871fa238ae4ull,0xc50cac874b842f68ull,
        0xb5b31949d479e321ull,0xde15303b98a4cd7full,0x450f0e70fc823decull,0xdf167e4444697d7full,
        0xe4ae3aff8926b78bull,0x6e92ba3305f31e8bull,0x5218e71dea92528bull,0xb0c5f50f95b04b8bull,
        0x27efd23fdae5d28bull,0xfd595ce268b1c0bfull,0xf39fad9ff03ef7feull,0xeff1cef37fc5c85dull,
        0x2a9d45f93cfb2a8bull,0x470d0cdf9c73cc5dull,0xe5bf53a940fb8147ull,0x3d53e65caea1e82ull,
        0x4a791e2cdcecd4d0ull,0x300be0f7e5db373eull,0x3b940b24647680d0ull,0x14cba80fdca58ebull,
        0xab99c7b1c169aff3ull,0x1c957ea4f7f1406eull,0xa0bbbe62f95b885cull,0x582575b532ad906eull,
        0x3589a05f250576faull,0x1ec5fa2484bb6c6cull,0x5cf67363f90603c4ull,0xe8bf3f50a4735a36ull,
        0x9bfc39219cc9ffc4ull,0xe4ae3aff8926b78bull,0x6e92ba3305f31e8bull,0x5218e71dea92528bull,
        0xb0c5f50f95b04b8bull,0x27efd23fdae5d28bull,
    };
    for(unsigned i=0;i<50u;++i)for(auto workers:{1u,4u})
        if(palette_pipeline_hash(i,workers)!=expected[i])
            throw std::runtime_error("Prepared palette changed latched colors or VRAM: "+std::to_string(i));
}

uint64_t early_depth_hash(unsigned variant,unsigned workers=1u) {
    const auto previous=set_raster_worker_limit(workers);
    struct Restore {unsigned previous;~Restore(){set_raster_worker_limit(previous);}} restore{previous};
    std::vector<uint8_t> vram(4u*1024u*1024u);
    for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*13u^(i>>9u));
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    const auto mode=variant%24u;const bool triangle=variant>=24u;
    GSPrimitiveBatch batch{};batch.vertexCount=triangle ? 3u : 2u;auto& s=batch.state;auto& c=s.context;
    s.prim.type=triangle ? GS_PRIM_TRIANGLE : GS_PRIM_SPRITE;s.prim.fst=s.prim.tme=s.prim.abe=true;
    s.linearFilter=mode&1u;s.colclamp=1u;s.textureWidth=s.textureHeight=128u;
    c.frame.fbp=56u;c.frame.fbw=8u;c.zbuf.zbp=168u;c.zbuf.psm=GS_PSM_Z32;c.zbuf.zmask=false;
    c.scissor={0u,511u,0u,223u};c.test=0x50000u;c.alpha=0x44u;
    c.tex0.tbp0=10240u;c.tex0.tbw=2u;c.tex0.tfx=1u;c.tex0.tcc=true;
    if(mode==3u)c.test=0x10000u;
    if(mode==4u)c.test=0x70000u;
    if(mode==5u)c.test=0x30000u;
    if(mode==6u)c.tex0.tbp0=56u*32u;
    if(mode==7u)c.tex0.tbp0=168u*32u;
    if(mode==9u)c.zbuf.psm=GS_PSM_Z16S;
    if(mode==10u)c.frame.psm=GS_PSM_CT24;
    if(mode==11u)c.test|=1u|(5u<<1u)|(128u<<4u)|(3u<<12u);
    if(mode==12u)c.test|=1u<<14u;
    if(mode==13u)c.fba=1u;
    if(mode==14u){s.prim.fge=true;s.fogR=71u;s.fogG=97u;s.fogB=131u;}
    if(mode==15u)c.frame.fbmsk=0x00ff00ffu;
    if(mode==16u)c.tex0.psm=GS_PSM_T8;
    if(mode==17u)c.tex0.psm=GS_PSM_T4;
    if(mode==18u)s.prim.fst=false;
    if(mode==19u)c.clamp=2u|(2u<<2u)|(127ull<<4u)|(31ull<<14u)|(127ull<<24u)|(31ull<<34u);
    if(mode==20u){c.zbuf.zbp=56u;c.zbuf.psm=GS_PSM_CT32;}
    c.tex0.cbp=16000u;c.tex0.cpsm=GS_PSM_CT32;c.tex0.cld=1u;
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x)
        backend.WriteVram(GS_PSM_CT32,16000u,1u,x,y,0x80000000u|(x*17u)|((y*17u)<<8u));
    backend.LoadClut(c.tex0,{});
    for(unsigned y=0;y<224u;++y)for(unsigned x=0;x<512u;++x){
        uint32_t old=(x%3u)*1000u;
        if(mode==2u || y>192u)old=2000u;
        if(mode==4u)old=1000u;
        if(mode==23u)old=x%3u==0u ? 0x80000000u : x%3u==1u ? 0x80000001u : 0xffffffffu;
        backend.WriteVram(c.zbuf.psm,c.zbuf.zbp*32u,8u,x,y,old);
    }
    for(unsigned y=0;y<128u;++y)for(unsigned x=0;x<128u;++x)
        backend.WriteVram(c.tex0.psm,c.tex0.tbp0,2u,x,y,0x80000000u|((x*11u+y*7u)&255u)|(((y*13u)&255u)<<8u)|(((x*17u)&255u)<<16u));
    for(auto& v:batch.vertices){v.r=73u;v.g=127u;v.b=191u;v.a=128u;v.z=mode==23u ? 0x80000001u : 1000u;v.fog=113u;v.u=v.v=12u;v.q=1.0f;}
    batch.vertices[1].x=512.0f;batch.vertices[1].y=224.0f;batch.vertices[1].u=512u*16u+12u;batch.vertices[1].v=224u*16u+12u;
    if(triangle){batch.vertices[1].y=8.75f;batch.vertices[1].v=12u;
        batch.vertices[2].x=12.5f;batch.vertices[2].y=223.25f;batch.vertices[2].v=224u*16u+12u;}
    if(mode==22u && triangle)batch.vertices[2].y=33.25f;
    if(mode==21u && triangle)std::swap(batch.vertices[1],batch.vertices[2]);
    for(unsigned i=0;i<3u;++i){auto& v=batch.vertices[i];v.q=1.0f+i*0.25f;v.s=float(v.u)/2048.0f;v.t=float(v.v)/2048.0f;}
    if(mode==8u){auto prime=batch;prime.vertices[1].x=2;prime.vertices[1].y=2;prime.vertices[2].y=2;
        for(auto& v:prime.vertices)v.u=v.v=8u;
        backend.Submit(prime);vram[c.tex0.tbp0*256u+17u]^=255u;}
    backend.Submit(batch);
    uint64_t hash=14695981039346656037ull;
    auto snapshot=[&](){for(auto byte:vram)hash=(hash^byte)*1099511628211ull;};snapshot();
    auto dependent=batch;dependent.state.context.test=0x30000u;dependent.state.context.zbuf.zmask=true;
    backend.WriteVram(c.tex0.psm,c.tex0.tbp0,2u,0u,0u,0x8013579bu);
    for(auto& v:dependent.vertices)v.u=v.v=8u;
    backend.Submit(dependent);snapshot();return hash;
}

void check_early_depth() {
    // Complete VRAM and dependent-sample snapshots from the previous renderer.
    constexpr uint64_t expected[]={
        0xee60935c6fc4f47aull,0xfea67fc77ad6e7c4ull,0x4b99100f0f054ce4ull,0xf7722b6735237d64ull,
        0x310386ab08354ce4ull,0x7886ed23780b4764ull,0xbd668f9037e4c4a7ull,0x60edd660b37ec644ull,
        0x2e94b6e3081873caull,0x1cebd814349e31e4ull,0x574a11f4e93d7080ull,0xfea67fc77ad6e7c4ull,
        0x7a64b05b036aacaaull,0xfea67fc77ad6e7c4ull,0x21a5ddcf8c646f52ull,0xd1d9ae4b9f6a11e0ull,
        0x72c58d68be94c9f5ull,0xac2387f55de27f04ull,0xc9a3430b5986f134ull,0xbb52654547626510ull,
        0xd12384e576c76184ull,0xfea67fc77ad6e7c4ull,0xee60935c6fc4f47aull,0x29f16fdd02851ef4ull,
        0xe7ad0269575a1995ull,0x86d910cc21d8ad42ull,0x95e12495a2f90781ull,0x7d5551f1953ac60bull,
        0xe800a043a7b90781ull,0x844471865effc89eull,0x512da307ff8b3a08ull,0x67b495d9ea470b5ull,
        0xff0ece65b004ea98ull,0x44f801973dd9ce72ull,0xde904564438be5a7ull,0x86d910cc21d8ad42ull,
        0x79a8db9c72a21f23ull,0x86d910cc21d8ad42ull,0xf1b620e2fa3be3cfull,0xf7fd22a5919a828cull,
        0xf32edf9cbf0c8c4full,0x2edee30b43b6fa57ull,0x1d23a74517eea351ull,0xa3588f2e7e81281cull,
        0x8623c2fb67e0cab0ull,0xefdff0118d819321ull,0xd2e4618ada858d5eull,0x7fd6cb41fef8fbf0ull,
    };
    for(unsigned i=0;i<48u;++i)for(auto workers:{1u,4u})
        if(early_depth_hash(i,workers)!=expected[i])
            throw std::runtime_error("Early depth changed VRAM or final texture cache state: "+std::to_string(i));
}

uint64_t prepared_pipeline_hash(unsigned variant) {
    const auto previous=set_raster_worker_limit(1u);
    struct Restore {unsigned previous;~Restore(){set_raster_worker_limit(previous);}} restore{previous};
    std::vector<uint8_t> vram(4u*1024u*1024u);
    for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*13u^(i>>9u));
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    GSPrimitiveBatch batch{};const auto mode=variant%32u;const bool triangle=variant>=32u;
    batch.vertexCount=triangle ? 3u : 2u;auto& s=batch.state;auto& c=s.context;
    s.prim.type=triangle ? GS_PRIM_TRIANGLE : GS_PRIM_SPRITE;s.prim.fst=s.prim.tme=s.prim.abe=true;
    s.colclamp=1u;s.textureWidth=s.textureHeight=64u;s.linearFilter=mode&1u;
    c.frame.fbp=56u;c.frame.fbw=1u;c.zbuf.zbp=168u;c.zbuf.psm=GS_PSM_Z32;c.zbuf.zmask=false;
    c.test=0x50000u;c.scissor={0u,47u,0u,39u};c.alpha=0x44u;
    c.tex0.tbp0=10240u;c.tex0.tbw=1u;c.tex0.tfx=1u;c.tex0.tcc=true;
    if(mode<4u)c.test=0x10000u|(uint64_t(mode)<<17u);
    if(mode>=4u && mode<8u)c.zbuf.psm=mode==4u ? GS_PSM_Z24 : mode==5u ? GS_PSM_Z16 : mode==6u ? GS_PSM_Z16S : GS_PSM_CT32;
    if(mode==7u)c.zbuf.zbp=56u;
    if(mode>=8u && mode<12u)c.tex0.psm=mode<10u ? GS_PSM_T8 : GS_PSM_T4;
    if(mode==12u)c.alpha=0u;
    if(mode==13u){s.pabe=1u;c.alpha=0x48u;}
    if(mode==14u)s.prim.abe=false;
    if(mode==15u)c.zbuf.zmask=true;
    if(mode==16u)c.frame.fbmsk=0x00ff00ffu;
    if(mode==17u)c.test|=1u|(5u<<1u)|(128u<<4u)|(3u<<12u);
    if(mode==18u)c.test|=1u<<14u;
    if(mode==19u)c.fba=1u;
    if(mode==20u){s.prim.fge=true;s.fogR=71u;s.fogG=97u;s.fogB=131u;}
    if(mode==21u)c.frame.psm=GS_PSM_CT24;
    if(mode==22u)c.frame.psm=GS_PSM_CT16;
    if(mode==23u)c.tex0.tbp0=56u*32u;
    if(mode==24u)c.tex0.tbp0=168u*32u;
    if(mode==25u)c.zbuf.zbp=56u;
    if(mode==26u){c.scissor={9u,43u,7u,36u};c.frame.fbw=0u;}
    if(mode==27u)c.test=0x70000u;
    if(mode>=28u){s.pabe=true;c.alpha=mode==28u ? 0x48u : 0x44u;}
    const unsigned alpha=mode==13u ? 127u : mode==28u || mode==29u ? 129u : mode==30u ? 0u : mode==31u ? 255u : 128u;
    for(unsigned y=0;y<40u;++y)for(unsigned x=0;x<48u;++x)
        backend.WriteVram(c.zbuf.psm,c.zbuf.zbp*32u,1u,x,y,(x%3u)*1000u);
    for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x)
        backend.WriteVram(c.tex0.psm,c.tex0.tbp0,1u,x,y,(alpha<<24u)|((x*11u+y*7u)&255u)|((y*13u)<<8u)|((x*17u)<<16u));
    c.tex0.cbp=16000u;c.tex0.cpsm=GS_PSM_CT32;c.tex0.cld=1u;
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x)
        backend.WriteVram(GS_PSM_CT32,16000u,1u,x,y,(alpha<<24u)|(x*17u)|((y*17u)<<8u));
    backend.LoadClut(c.tex0,{});
    for(auto& v:batch.vertices){v.r=73u;v.g=127u;v.b=191u;v.a=uint8_t(alpha);v.z=1000u;v.fog=113u;v.u=v.v=12u;}
    batch.vertices[1].x=48.0f;batch.vertices[1].y=40.0f;batch.vertices[1].u=48u*16u+12u;batch.vertices[1].v=40u*16u+12u;
    if(triangle){batch.vertices[1].y=0.0f;batch.vertices[1].v=12u;batch.vertices[2].y=40.0f;batch.vertices[2].v=40u*16u+12u;}
    backend.Submit(batch);
    uint64_t hash=14695981039346656037ull;
    auto snapshot=[&](){for(auto byte:vram)hash=(hash^byte)*1099511628211ull;};snapshot();
    // Observe the final texture cache after another upload and sample.
    backend.WriteVram(c.tex0.psm,c.tex0.tbp0,1u,0u,0u,0x8013579bu);
    for(auto& v:batch.vertices)v.u=v.v=8u;
    backend.Submit(batch);snapshot();return hash;
}

void check_prepared_pipeline() {
    // Full VRAM and dependent-sample snapshots from the prior renderer.
    constexpr uint64_t expected[]={
        0x87f3619483a8c214ull,0x3b57af5338f35980ull,0x960b13bf7b1273b1ull,0xb889f647550dcdacull,
        0x471638406f116531ull,0x102678ea12eaa5dull,0xac5cba3ab4d547b1ull,0x61f0ead46514de14ull,
        0x13eecef2af754e0eull,0x22e31218b68cfcf7ull,0x33fd11dca7eec556ull,0x611ce2f985396a0eull,
        0x6457d6e2066096f4ull,0xac715388f96aa4d6ull,0x6457d6e2066096f4ull,0x89f5ea286096805dull,
        0xe94bb9917750d1bcull,0xd92dfbdbf520485dull,0x1e8971b0b01f7f68ull,0xd92dfbdbf520485dull,
        0x11b1c0a3d1699d4cull,0x64338d19c8a30c55ull,0xdcfa595dcaa023a3ull,0x4c4fa67c14ebb806ull,
        0x19adbc06fea55614ull,0xd47342664dc3cab5ull,0xea9509ddde8331b1ull,0xb889f647550dcdacull,
        0x68dc49317d3dd813ull,0x629ecabb91d57a06ull,0xeadbedaa1d89b974ull,0x9392f26a2dbb80e6ull,
        0x87f3619483a8c214ull,0x7f914e964d713fe9ull,0x405cb7b2771353b8ull,0xb98bab0c53183fe4ull,
        0xcd40453e80289818ull,0x90f107e0433f6229ull,0x4f96c43e5c9353f8ull,0xb8e808277856e14ull,
        0xd07abb13baea8f4cull,0xd66aae8d861dcc1dull,0xcca4437c15aa8bc6ull,0xc1404bb7e0a579d8ull,
        0x690f4832e9ecfde2ull,0xa8db7f47ee06776aull,0x690f4832e9ecfde2ull,0x5e04fa108cabb789ull,
        0x80532c1fad66ff6bull,0xa4940a3dab90a789ull,0x65135aa3542e6a9cull,0xa4940a3dab90a789ull,
        0x906c4374ac5e2a48ull,0x8bc97ca26dad01d5ull,0x3681a517c7628a34ull,0x2a252d92962eeb5ull,
        0x19adbc06fea55614ull,0xf06ce0ab88bbe0fbull,0xb0ce4cb9ac067c6ull,0xb98bab0c53183fe4ull,
        0xdb0d56baea54819aull,0x7e7f7e2543eb150cull,0x42920dda868e9462ull,0xf3f6dc94d7e1306ull,
    };
    for(unsigned i=0;i<64u;++i)if(prepared_pipeline_hash(i)!=expected[i])
        throw std::runtime_error("Prepared pixel path changed VRAM or cached texture state: "+std::to_string(i));
}

// Compare prepared sprite/triangle writes with the unchanged scalar point
// pipeline. This oracle retains the original alpha/depth/write-mask ordering.
void check_tested_pixels() {
    std::vector<uint8_t> expected(4u*1024u*1024u),actual(expected.size());
    auto reference=std::make_unique<GSCpuBackend>(),candidate=std::make_unique<GSCpuBackend>();
    reference->Initialize(expected.data(),unsigned(expected.size()));candidate->Initialize(actual.data(),unsigned(actual.size()));
    GSPrimitiveBatch point{};point.vertexCount=1u;auto& state=point.state;auto& c=state.context;
    state.prim.type=GS_PRIM_POINT;state.prim.abe=true;state.colclamp=1u;
    c.frame.fbp=56u;c.frame.fbw=1u;c.zbuf.psm=GS_PSM_Z32;c.scissor={0u,0u,0u,0u};
    constexpr unsigned alphas[]={0,1,63,127,128,129,254,255};
    constexpr unsigned masks[]={0,0x00ff00ffu,0xff000000u};
    constexpr unsigned blends[]={0x44u,0x48u,0x64u,0x1bu,0x1du,0xffu};
    constexpr uint32_t depths[]={0u,1234u,1235u,UINT32_MAX};
    unsigned count=0;
    for(unsigned atst=0;atst<8u;++atst)for(unsigned afail=0;afail<4u;++afail)
    for(unsigned alpha:alphas)for(unsigned date=0;date<3u;++date)for(unsigned mask:masks)
    for(unsigned fba=0;fba<2u;++fba)for(unsigned depth=0;depth<4u;++depth)for(auto z:depths){
        c.test=1u|(uint64_t(atst)<<1u)|(127u<<4u)|(uint64_t(afail)<<12u)|(uint64_t(depth)<<17u);
        if(date)c.test|=0x4000u|(uint64_t(date-1u)<<15u);
        const unsigned pixelX=(count*17u)&511u,pixelY=(count*13u)%224u;
        c.scissor={uint16_t(pixelX),uint16_t(pixelX),uint16_t(pixelY),uint16_t(pixelY)};
        c.frame.fbmsk=mask;c.fba=fba;c.zbuf.zbp=count%11u ? 168u : 56u;c.zbuf.zmask=count%3u==0u;
        c.alpha=blends[count%6u]|(uint64_t(71u)<<32u);state.pabe=count&1u;
        auto& v=point.vertices[0];v.r=83u;v.g=197u;v.b=41u;v.a=uint8_t(alpha);v.z=z;v.x=float(pixelX);v.y=float(pixelY);
        auto draw=point;draw.vertexCount=2u;draw.state.prim.type=GS_PRIM_SPRITE;
        for(auto& vertex:draw.vertices)vertex=v;draw.vertices[1].x=float(pixelX+1u);draw.vertices[1].y=float(pixelY+1u);
        for(unsigned shape=0;shape<2u;++shape){
            for(auto* target:{reference.get(),candidate.get()}){
                target->WriteVram(GS_PSM_CT32,56u*32u,1u,pixelX,pixelY,(count&1u) ? 0x9163c12du : 0x7163c12du);
                target->WriteVram(GS_PSM_Z32,c.zbuf.zbp*32u,1u,pixelX,pixelY,1234u);
            }
            if(shape){draw.vertexCount=3u;draw.state.prim.type=GS_PRIM_TRIANGLE;
                draw.vertices[1].x=float(pixelX+2u);draw.vertices[1].y=float(pixelY);draw.vertices[2].x=float(pixelX);draw.vertices[2].y=float(pixelY+2u);}
            reference->Submit(point);candidate->Submit(draw);
            for(auto base:{56u,c.zbuf.zbp})for(unsigned x=0;x<2u;++x)
                if(reference->ReadVram(GS_PSM_CT32,base*32u,1u,pixelX+x,pixelY)!=candidate->ReadVram(GS_PSM_CT32,base*32u,1u,pixelX+x,pixelY))
                    throw std::runtime_error("Tested pixel writer differs from scalar point: case="+std::to_string(count)+", shape="+std::to_string(shape));
        }
        ++count;
        if((count%512u)==0u && actual!=expected)throw std::runtime_error("Tested pixel writer changed neighboring VRAM");
    }
    if(count!=73728u || actual!=expected)throw std::runtime_error("Tested pixel comparisons incomplete");
}

void check_pixel_blending() {
    std::vector<uint8_t> vram(4u*1024u*1024u);
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    GSPrimitiveBatch batch{};batch.vertexCount=2;auto& s=batch.state;auto& c=s.context;
    s.prim.type=GS_PRIM_SPRITE;s.prim.abe=true;s.colclamp=1u;
    c.frame.fbp=56u;c.frame.fbw=1u;c.zbuf.zmask=true;c.test=0x30000u;c.scissor={0u,0u,0u,0u};
    batch.vertices[1].x=batch.vertices[1].y=1.0f;
    uint32_t seed=0x47891123u;
    auto random=[&](){seed=seed*1664525u+1013904223u;return seed;};
    for(auto psm:{GS_PSM_CT32,GS_PSM_CT24,GS_PSM_CT16})for(unsigned pabe=0;pabe<2u;++pabe)
    for(unsigned selectors=0;selectors<256u;++selectors)for(unsigned alpha:{0u,1u,63u,127u,128u,129u,254u,255u}) {
        c.frame.psm=psm;s.pabe=pabe;c.fba=selectors&1u;c.frame.fbmsk=(selectors&2u) ? 0x00ff00ffu : 0u;
        const uint32_t input=random(),old=random(),fix=random()&255u;
        for(auto& v:batch.vertices){v.r=input&255u;v.g=(input>>8u)&255u;v.b=(input>>16u)&255u;v.a=uint8_t(alpha);}
        c.alpha=selectors|(uint64_t(fix)<<32u);
        backend.WriteVram(psm,56u*32u,1u,0u,0u,old);
        const uint32_t raw=backend.ReadVram(psm,56u*32u,1u,0u,0u);
        const uint32_t dest=psm==GS_PSM_CT16 ? ((raw&31u)<<3u)|(((raw>>5u)&31u)<<11u)
            |(((raw>>10u)&31u)<<19u)|((raw&0x8000u)<<16u) : psm==GS_PSM_CT24 ? raw|0x80000000u : raw;
        uint32_t expected=0u;
        auto pick=[](unsigned which,int source,int destination){return which==0u ? source : which==1u ? destination : 0;};
        const int factor=((selectors>>4u)&3u)==0u ? alpha : ((selectors>>4u)&3u)==1u ? (dest>>24u)&255u : fix;
        for(unsigned lane=0;lane<3u;++lane) {
            const int source=(input>>(8u*lane))&255u,destination=(dest>>(8u*lane))&255u;
            int color=source;
            if(!(pabe && alpha<128u))color=(((pick(selectors&3u,source,destination)
                -pick((selectors>>2u)&3u,source,destination))*factor)>>7u)
                +pick((selectors>>6u)&3u,source,destination);
            expected|=uint32_t(std::clamp(color,0,255))<<(8u*lane);
        }
        expected|=(alpha|((c.fba && psm!=GS_PSM_CT24) ? 128u : 0u))<<24u;
        expected=(expected&~c.frame.fbmsk)|(dest&c.frame.fbmsk);
        if(psm==GS_PSM_CT24)expected&=0x00ffffffu;
        if(psm==GS_PSM_CT16)expected=((expected>>3u)&31u)|(((expected>>11u)&31u)<<5u)
            |(((expected>>19u)&31u)<<10u)|((expected>>16u)&0x8000u);
        backend.Submit(batch);
        if(backend.ReadVram(psm,56u*32u,1u,0u,0u)!=expected)
            throw std::runtime_error("Pixel blend differs from scalar GS equation");
    }
}

uint64_t triangle_pipeline_hash(unsigned variant,unsigned workers=4u,bool checkDispatch=false) {
    const auto previous=set_raster_worker_limit(workers);
    struct Restore {unsigned previous;~Restore(){set_raster_worker_limit(previous);}} restore{previous};
    std::vector<uint8_t> vram(4u*1024u*1024u);
    for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*13u^(i>>9u));
    GSCpuBackend backend;backend.Initialize(vram.data(),uint32_t(vram.size()));
    GSPrimitiveBatch batch{};batch.vertexCount=3;auto& s=batch.state;auto& c=s.context;
    s.prim.type=GS_PRIM_TRIANGLE;s.prim.tme=true;s.prim.fst=true;s.prim.iip=(variant&2u)!=0u;
    s.linearFilter=(variant&1u)!=0u;s.colclamp=1u;s.textureWidth=128u;s.textureHeight=128u;
    s.texa.ta0=s.texa.ta1=128u;
    c.frame.fbp=56u;c.frame.fbw=2u;c.zbuf.zmask=true;c.zbuf.zbp=168u;c.zbuf.psm=GS_PSM_Z32;
    c.test=0x30000u;c.scissor={0u,127u,0u,95u};c.tex0.tbp0=8192u;c.tex0.tbw=2u;c.tex0.tcc=true;c.tex0.tfx=1u;
    c.tex0.psm=(variant/4u)%4u==0u ? GS_PSM_CT32 : (variant/4u)%4u==1u ? GS_PSM_CT16
        : (variant/4u)%4u==2u ? GS_PSM_T8 : GS_PSM_T4;
    if(variant>=16u){s.prim.abe=true;c.alpha=0x44u;
        s.textureWidth=s.textureHeight=512u;c.tex0.tbw=c.frame.fbw=8u;
        c.scissor={0u,511u,0u,447u};}
    if(variant==1u)c.tex0.tbp0=56u*32u;
    if(variant==2u)c.zbuf.zmask=false;
    if(variant==3u)c.test|=1u<<14u;
    if(variant==4u)c.frame.fbmsk=0x00ff00ffu;
    if(variant==5u){s.prim.fge=true;s.fogR=31;s.fogG=73;s.fogB=113;}
    if(variant==6u){s.prim.fst=false;}
    if(variant==7u){c.test=0x50000u;}
    if(variant==8u){c.clamp=3u|(3u<<2u)|(63ull<<4u)|(31ull<<24u);}
    if(variant==9u)++c.tex0.tbp0;
    if(variant==10u)c.frame.psm=GS_PSM_CT16;
    if(variant==11u){c.tex0.tbp0=56u*32u;}
    if(variant==12u){c.zbuf.zmask=false;c.zbuf.zbp=256u;}
    if(variant==13u)c.test|=1u|(5u<<1u)|(117u<<4u)|(3u<<12u);
    if(variant==14u)s.pabe=1u;
    if(variant==15u)c.fba=1u;
    if(variant==33u || variant==43u)c.tex0.tbp0=56u*32u;
    if(variant==34u){c.zbuf.zmask=false;c.zbuf.zbp=384u;}
    if(variant==35u){c.test=0x50000u;c.zbuf.zbp=56u;}
    if(variant==39u)c.scissor.x0=256u;
    if(variant==40u){c.scissor.x0=300u;c.scissor.y0=224u;}
    if(variant==41u){c.zbuf.zmask=false;c.zbuf.zbp=256u;}
    if(variant==42u)c.frame.fbmsk=0x00ff00ffu;
    if(variant==44u){++c.tex0.tbp0;c.clamp=3u|(3u<<2u)|(255ull<<4u)|(256ull<<14u);}
    if(variant==45u)c.test|=1u|(5u<<1u)|(117u<<4u)|(3u<<12u);
    if(variant==46u){s.pabe=1u;s.prim.fge=true;s.fogR=71u;s.fogG=97u;s.fogB=131u;}
    if(variant==47u)c.frame.psm=GS_PSM_CT16;
    c.tex0.cbp=16000u;c.tex0.cpsm=GS_PSM_CT32;c.tex0.cld=1u;
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x)
        backend.WriteVram(GS_PSM_CT32,16000u,1u,x,y,0x80000000u|(x*17u)|((y*17u)<<8u)|(((x^y)*17u)<<16u));
    backend.LoadClut(c.tex0,{});
    batch.vertices[0].x=0.25f;batch.vertices[0].y=0.5f;
    batch.vertices[1].x=127.25f;batch.vertices[1].y=8.75f;
    batch.vertices[2].x=12.5f;batch.vertices[2].y=95.25f;
    for(unsigned i=0;i<3u;++i){auto& v=batch.vertices[i];v.u=i==1u ? 2044u : 8u;v.v=i==2u ? 1532u : 12u;
        v.r=uint8_t(71u+i*31u);v.g=uint8_t(113u+i*17u);v.b=uint8_t(191u-i*19u);
        v.a=variant>=16u ? 128u : uint8_t(57u+i*31u);v.z=UINT32_MAX;v.q=1.0f+i*0.25f;
        v.s=float(v.u)/2048.0f;v.t=float(v.v)/2048.0f;v.fog=uint8_t(101u+i*29u);}
    if(variant>=16u){batch.vertices[1].x=511.25f;batch.vertices[2].y=447.25f;
        batch.vertices[1].u=8188u;batch.vertices[2].v=7164u;}
    if(variant==38u)std::swap(batch.vertices[1],batch.vertices[2]);
    // A stale initial page must retain its bytes until the serial sampler
    // first evicts it, even on the immutable-source path.
    if((variant>=16u && variant<32u) || variant==36u){auto prime=batch;prime.vertices[1].x=2;prime.vertices[2].y=2;
        for(auto& v:prime.vertices)v.u=v.v=8u;
        backend.Submit(prime);vram[c.tex0.tbp0*256u+17u]^=255u;}
    const auto before=raster_parallel_batches();
    backend.Submit(batch);
    if(checkDispatch){
        const bool eligible=workers>1u && variant>=32u && variant!=33u && variant!=35u
            && variant!=36u && variant!=41u && variant!=43u;
        if(raster_workers().count()>1u && (raster_parallel_batches()!=before)!=eligible)
            throw std::runtime_error("Triangle bypassed parallel eligibility or stale-cache guard: "+std::to_string(variant));
    }
    uint64_t hash=14695981039346656037ull;
    auto hashVram=[&](){for(auto byte:vram)hash=(hash^byte)*1099511628211ull;};
    hashVram();
    auto dependent=batch;dependent.state.linearFilter=false;dependent.state.prim.iip=false;
    for(auto& v:dependent.vertices)v.u=v.v=8u;
    backend.WriteVram(c.tex0.psm,c.tex0.tbp0,c.tex0.tbw,0u,0u,0x80ff13a7u);
    backend.Submit(dependent);
    hashVram();
    return hash;
}

void check_triangle_pipeline() {
    // Complete VRAM snapshots from the previous serial renderer, before
    // and after a dependent sample that observes the final cached page.
    constexpr uint64_t expected[]={
        0x308cc64e246791c0ull,0x10af1f51b16627edull,0xa6a20f730ecfc768ull,0x8f60fefe665f3279ull,
        0x423d60b0c126c258ull,0x8d944d8fb66232c7ull,0xe83493e64ffe80a8ull,0xcff8a167de28f8c1ull,
        0xdf5885a6650d2520ull,0x8ec815c0df63180ull,0x92d12009d72a7062ull,0x1c578a8384731a57ull,
        0xfb786d0aa9439231ull,0x3aaa479ad16f73f4ull,0x272543d57efefac6ull,0x3aaa479ad16f73f4ull,
        0xf6073cd714104d50ull,0x6767277e4ab292daull,0xf6073cd714104d50ull,0x6767277e4ab292daull,
        0xc717ded35a3145e8ull,0x34f20ad9d7b85c79ull,0xc717ded35a3145e8ull,0x34f20ad9d7b85c79ull,
        0x2b986bb5fb8a8286ull,0x437c4e67751534b3ull,0x2b986bb5fb8a8286ull,0x437c4e67751534b3ull,
        0xed43356a060fc6c6ull,0x777aef448388fa00ull,0xed43356a060fc6c6ull,0x777aef448388fa00ull,
        0x6f98c09f3878619eull,0xa678336bc01f5409ull,0x40f5eb742d3e20feull,0xcaf098efc68ca3a6ull,
        0xc717ded35a3145e8ull,0x6784299ad581bbd1ull,0x86d4de36ef0b7a28ull,0xc0f097fdd8d04547ull,
        0xd791386bcb280702ull,0xd89ced2b31eb2663ull,0xc6555c59637e92c6ull,0x3c14492bd64ff32eull,
        0x66a1eafb20a7fc86ull,0x742c5ec6d194c9d0ull,0x139cc95fec0321c4ull,0x84e4cfef53fb4d05ull,
    };
    for(unsigned i=0;i<48u;++i)for(auto workers:{1u,4u})
        if(triangle_pipeline_hash(i,workers,true)!=expected[i])
            throw std::runtime_error("Triangle pipeline changed VRAM or sampler residency: "+std::to_string(i));
}

void check_parallel_sprites() {
    const auto previous=set_raster_worker_limit(1);
    struct Restore { unsigned previous;~Restore(){set_raster_worker_limit(previous);} } restore{previous};
    std::vector<uint8_t> original(4u*1024u*1024u);
    for(unsigned i=0;i<original.size();++i)original[i]=uint8_t((i*13u)^(i>>9u));
    for(unsigned variant=0;variant<64u;++variant) {
        auto serialVram=original,parallelVram=original;
        auto serial=std::make_unique<GSCpuBackend>(),parallel=std::make_unique<GSCpuBackend>();
        serial->Initialize(serialVram.data(),unsigned(serialVram.size()));
        parallel->Initialize(parallelVram.data(),unsigned(parallelVram.size()));
        GSPrimitiveBatch batch{};batch.vertexCount=2;auto& state=batch.state;auto& ctx=state.context;
        state.prim.type=GS_PRIM_SPRITE;state.prim.tme=true;state.prim.fst=true;
        state.colclamp=1u;state.linearFilter=(variant&1u)!=0;
        state.textureWidth=512u;state.textureHeight=512u;
        ctx.tex0.tbp0=8192u;ctx.tex0.tbw=8u;ctx.tex0.tfx=1u;ctx.tex0.tcc=true;
        ctx.frame.fbp=56u;ctx.frame.fbw=8u;ctx.zbuf.zmask=true;ctx.test=0x30000u;
        ctx.scissor={0u,511u,0u,447u};
        for(auto& v:batch.vertices) { v.r=107u;v.g=131u;v.b=97u;v.a=117u;v.fog=173u;v.q=1.0f; }
        batch.vertices[1].x=512.0f;batch.vertices[1].y=448.0f;
        batch.vertices[0].u=batch.vertices[0].v=12u;
        batch.vertices[1].u=512u*16u+12u;batch.vertices[1].v=448u*16u+12u;
        if(variant>=2u && variant<=5u) { state.prim.abe=true;ctx.alpha=variant<4u ? 0x44u : 0x6au; }
        if(variant==6u)ctx.frame.fbmsk=0x00ff00ffu;
        if(variant==7u) { state.prim.fge=true;state.fogR=47;state.fogG=83;state.fogB=71; }
        if(variant==8u)ctx.test|=1u<<14u;
        if(variant==9u)ctx.test|=1u|(5u<<1u)|(117u<<4u)|(3u<<12u);
        if(variant==10u) { state.prim.fst=false;batch.vertices[1].s=1.0f;batch.vertices[1].t=0.875f; }
        if(variant==11u)ctx.fba=1u;
        if(variant>=16u && variant<22u) {
            state.prim.abe=true;ctx.alpha=0x6au;
            if(variant<18u)ctx.tex0.tcc=false;
            else { ctx.tex0.tfx=0u;for(auto& v:batch.vertices)v.a=0u; }
            if(variant==20u)state.pabe=1u;
            if(variant==21u)ctx.tex0.tfx=2u;
        }
        if(variant==12u)ctx.test=0x50000u;
        if(variant==13u)ctx.tex0.tbp0=56u*32u;
        if(variant==23u)++ctx.tex0.tbp0;
        if(variant>=24u && variant<34u) {
            constexpr uint32_t formats[]={GS_PSM_CT24,GS_PSM_CT16,GS_PSM_CT16S,GS_PSM_T8,GS_PSM_T4,
                GS_PSM_T8H,GS_PSM_T4HL,GS_PSM_T4HH,GS_PSM_Z32,GS_PSM_Z16};
            ctx.tex0.psm=formats[variant-24u];ctx.tex0.tbp0=8192u;
            state.texa.ta0=state.texa.ta1=128u;ctx.tex0.cpsm=GS_PSM_CT32;
        }
        if(variant>=34u && variant<40u) {
            ctx.tex0.tbp0=0u;ctx.frame.fbp=128u;ctx.zbuf.zbp=256u;
            ctx.zbuf.psm=variant<38u ? GS_PSM_Z32 : variant==38u ? GS_PSM_Z24 : GS_PSM_Z16;
            ctx.test=variant==34u ? 0x50000u : variant==35u ? 0x70000u : 0x30000u;
            ctx.zbuf.zmask=variant<36u;
            for(auto& v:batch.vertices)v.z=UINT32_MAX;
        }
        if(variant==40u || variant==41u) {
            ctx.test=0x50000u;for(auto& v:batch.vertices)v.z=variant==40u ? UINT32_MAX : 127u;
        }
        if(variant==42u)ctx.tex0.tbp0=8193u;
        if(variant==43u)ctx.clamp=2u|(2u<<2u)|(uint64_t(511u)<<14u)|(uint64_t(511u)<<34u);
        if(variant==44u)ctx.clamp=3u|(3u<<2u)|(uint64_t(255u)<<4u)|(uint64_t(256u)<<14u)
            |(uint64_t(255u)<<24u)|(uint64_t(256u)<<34u);
        if(variant>=45u && variant<=47u)ctx.frame.psm=variant==45u ? GS_PSM_CT24 : variant==46u ? GS_PSM_CT16 : GS_PSM_CT16S;
        if(variant==48u) { ctx.zbuf.zmask=false;ctx.zbuf.psm=GS_PSM_Z32;ctx.zbuf.zbp=0u; }
        if(variant==49u) { ctx.zbuf.zmask=false;ctx.zbuf.psm=GS_PSM_Z32;ctx.zbuf.zbp=256u; }
        if(variant==50u) { ctx.frame.fbp=480u; }
        if(variant==51u) { ctx.tex0.psm=GS_PSM_T8;ctx.tex0.tbp0=56u*32u; }
        if(variant==52u) { batch.vertices[1].x=256;batch.vertices[1].y=128; }
        if(variant==53u) { batch.vertices[1].x=256;batch.vertices[1].y=127; }
        if(variant>=54u && variant<58u) {
            state.prim.abe=true;ctx.alpha=0x6au;ctx.tex0.tfx=0u;
            for(auto& v:batch.vertices) { v.a=0u;v.z=127u; }
            ctx.test=0x50000u;
            if(variant==55u) { ctx.zbuf.zmask=false;ctx.zbuf.psm=GS_PSM_Z32; }
            if(variant==56u)ctx.tex0.psm=GS_PSM_T8;
            if(variant==57u)ctx.test|=1u|(5u<<1u)|(127u<<4u)|(3u<<12u);
        }
        if(variant>=58u) {
            state.prim.abe=true;ctx.alpha=0x44u;ctx.tex0.tfx=0u;
            for(auto& v:batch.vertices)v.a=0u;
            if(variant==59u)ctx.tex0.tcc=false;
            if(variant==60u)ctx.alpha=0x64u; // FIX=0 also preserves destination RGB.
            if(variant==61u)state.pabe=1u;
            if(variant==62u)ctx.test|=1u|(5u<<1u)|(127u<<4u)|(3u<<12u);
            if(variant==63u){ctx.zbuf.zmask=false;ctx.zbuf.psm=GS_PSM_Z32;}
        }
        if(ctx.tex0.psm==GS_PSM_T8 || ctx.tex0.psm==GS_PSM_T4 || ctx.tex0.psm==GS_PSM_T8H
                || ctx.tex0.psm==GS_PSM_T4HL || ctx.tex0.psm==GS_PSM_T4HH) {
            ctx.tex0.cbp=16000u;ctx.tex0.cpsm=GS_PSM_CT32;ctx.tex0.cld=1u;
            for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x) {
                const uint32_t index=y*16u+x;
                const uint32_t color=0x80000000u|index|((index^0x63u)<<8u)|((255u-index)<<16u);
                serial->WriteVram(GS_PSM_CT32,16000u,1u,x,y,color);
                parallel->WriteVram(GS_PSM_CT32,16000u,1u,x,y,color);
            }
            serial->LoadClut(ctx.tex0,{});parallel->LoadClut(ctx.tex0,{});
        }
        if(variant==14u || variant==15u || variant==22u) {
            auto prime=batch;prime.vertices[1].x=16;prime.vertices[1].y=1;
            if(variant==22u)prime.state.context.tex0.tbp0=4096u;
            prime.vertices[1].u=16u*16u+12u;prime.vertices[1].v=16u+12u;
            serial->Submit(prime);parallel->Submit(prime);
            if(variant==15u) { serialVram[8192u*256u+17u]^=255u;parallelVram[8192u*256u+17u]^=255u; }
            if(variant==22u) { serialVram[4096u*256u+17u]^=255u;parallelVram[4096u*256u+17u]^=255u; }
        }
        const auto before=raster_parallel_batches();
        set_raster_worker_limit(1);serial->Submit(batch);
        if(raster_parallel_batches()!=before)throw std::runtime_error("Serial sprite unexpectedly dispatched raster workers");
        set_raster_worker_limit(4);parallel->Submit(batch);
        const bool eligible=variant!=12u && variant!=13u && variant!=15u
            && variant!=41u && variant!=48u && variant!=49u && variant!=50u && variant!=51u && variant!=53u
            && (variant<54u || (variant>=58u && variant!=63u));
        if(raster_workers().count()>1u && (raster_parallel_batches()!=before)!=eligible)
            throw std::runtime_error("Parallel sprite bypassed an eligibility or stale-cache guard: "+std::to_string(variant));
        if(serialVram!=parallelVram)throw std::runtime_error("Parallel sprite changed complete VRAM output: "+std::to_string(variant));
        // Mutate the last tap's texel without TEXFLUSH, then read that exact
        // page first. Evicting it before this read would hide a lost cache.
        const unsigned sampleX=state.linearFilter ? 0u : 511u;
        const unsigned sampleY=state.linearFilter ? 448u : 447u;
        const auto changed=serial->ReadVram(ctx.tex0.psm,ctx.tex0.tbp0,ctx.tex0.tbw,sampleX,sampleY)^0x80ff13a7u;
        serial->WriteVram(ctx.tex0.psm,ctx.tex0.tbp0,ctx.tex0.tbw,sampleX,sampleY,changed);
        parallel->WriteVram(ctx.tex0.psm,ctx.tex0.tbp0,ctx.tex0.tbw,sampleX,sampleY,changed);
        state.prim.abe=state.prim.fge=false;state.prim.fst=true;state.pabe=0u;state.linearFilter=false;
        ctx.test=0x30000u;ctx.fba=0u;ctx.frame.fbmsk=0u;ctx.tex0.tfx=1u;ctx.tex0.tcc=true;
        batch.vertices[1].x=32u;batch.vertices[1].y=2u;
        for(auto& v:batch.vertices) { v.u=sampleX*16u+8u;v.v=sampleY*16u+8u; }
        serial->Submit(batch);parallel->Submit(batch);
        if(serialVram!=parallelVram)throw std::runtime_error("Parallel sprite lost final sampler-cache state");
    }
}
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
    verify_raster_shading();
    check_tested_pixels();check_draw_history_sampling();check_prepared_palette();check_early_depth();check_prepared_pipeline();check_pixel_blending();check_triangle_pipeline();
    verify_raster_workers();check_parallel_sprites();
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
