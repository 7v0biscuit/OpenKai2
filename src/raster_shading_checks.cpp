// SPDX-License-Identifier: GPL-3.0-only
#include "raster_shading_checks.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "raster_workers.h"
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>
namespace openkai2 {
RasterShadingResult raster_shading_workload(unsigned mode,unsigned iterations,unsigned workers) {
    if(mode>=128u || !iterations)throw std::runtime_error("Invalid raster shading workload");
    const auto previous=set_raster_worker_limit(workers);
    struct Restore {unsigned previous;~Restore(){set_raster_worker_limit(previous);}} restore{previous};
    std::vector<uint8_t> vram(4u*1024u*1024u);
    for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*13u^(i>>9u));
    auto backend=std::make_unique<GSCpuBackend>();backend->Initialize(vram.data(),uint32_t(vram.size()));
    GSPrimitiveBatch b{};b.vertexCount=3u;auto& s=b.state;auto& c=s.context;
    s.prim.type=GS_PRIM_TRIANGLE;s.prim.tme=s.prim.iip=s.prim.abe=true;
    s.prim.fst=(mode&16u)==0u;s.linearFilter=mode&1u;s.colclamp=1u;
    s.textureWidth=s.textureHeight=128u;s.texa.ta0=37u;s.texa.ta1=211u;
    c.frame.fbp=56u;c.frame.fbw=8u;c.zbuf.zbp=168u;c.zbuf.psm=GS_PSM_Z32;c.zbuf.zmask=true;
    c.scissor={0u,511u,0u,223u};c.test=0x30000u;c.alpha=0x44u;
    constexpr uint8_t formats[]={GS_PSM_CT32,GS_PSM_CT16,GS_PSM_T8,GS_PSM_T4};
    c.tex0.psm=formats[(mode/32u)%4u];c.tex0.tbp0=10240u;c.tex0.tbw=2u;
    c.tex0.tfx=(mode/2u)%4u;c.tex0.tcc=(mode&8u)!=0u;
    c.tex0.cbp=16000u;c.tex0.cpsm=GS_PSM_CT32;c.tex0.cld=1u;
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x)
        backend->WriteVram(GS_PSM_CT32,16000u,1u,x,y,((x*17u)&255u)|(((y*17u)&255u)<<8u)|(((x^y)*17u)<<16u)|(((x+y)*13u&255u)<<24u));
    backend->LoadClut(c.tex0,{});
    for(unsigned i=0;i<3u;++i){auto& v=b.vertices[i];
        v.r=uint8_t(i==0u ? 0u : i==1u ? 255u : 127u);v.g=uint8_t(i==0u ? 255u : i==1u ? 1u : 191u);
        v.b=uint8_t(i==0u ? 73u : i==1u ? 254u : 0u);v.a=uint8_t(i==0u ? 0u : i==1u ? 255u : 128u);
        v.z=1000u+i*117u;v.q=0.75f+float(i)*0.5f;
        v.u=i==1u ? 2044u : 8u;v.v=i==2u ? 2040u : 12u;
        v.s=float(v.u)/2048.0f;v.t=float(v.v)/2048.0f;v.fog=uint8_t(i*101u);
    }
    b.vertices[0].x=0.25f;b.vertices[0].y=0.5f;
    b.vertices[1].x=255.25f;b.vertices[1].y=0.75f;
    b.vertices[2].x=0.5f;b.vertices[2].y=191.25f;
    backend->Submit(b);
    const auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i)backend->Submit(b);
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
    uint64_t hash=14695981039346656037ull;
    const auto capture=[&]{for(auto byte:vram)hash=(hash^byte)*1099511628211ull;};capture();
    for(auto& v:b.vertices)v.u=v.v=8u;
    s.prim.fst=true;s.prim.iip=false;s.linearFilter=false;
    backend->WriteVram(c.tex0.psm,c.tex0.tbp0,c.tex0.tbw,0u,0u,0x80ff13a7u);
    backend->Submit(b);capture();
    return {hash,double(elapsed)/iterations};
}
void verify_raster_shading() {
    constexpr uint64_t expected[128]={
        0x570a882c7db6c244ull,0x3c9ae6c33fef7b61ull,0x39be9190705da8c9ull,0x2ea00dadd65738e7ull,
        0xf970d1cfd3e56e5ull,0x511ccc0fe2e5d80bull,0xf970d1cfd3e56e5ull,0x511ccc0fe2e5d80bull,
        0xa7b5e079e19ac953ull,0xc0ea6af5c63b951full,0x3c1d60e7eb04f26aull,0x1b540958871cc638ull,
        0xfaf14f9b693feaeull,0xc3ff5beb661b4402ull,0xd5f52274c290a69dull,0xef2dbc5b3d85ba03ull,
        0xcaa02aa7b90b577dull,0x37ed89ee28924259ull,0x6e756901005b318ull,0xce16079046011896ull,
        0xe35b4645d662e36ull,0x58478dcbbecac94full,0xe35b4645d662e36ull,0x58478dcbbecac94full,
        0x8e457ff572baded5ull,0x4ed8a12ab841f49bull,0x3280789ab992bf81ull,0xf41f35432f2d1199ull,
        0x10e800a77ca5ca65ull,0x4eef61f389bd3478ull,0xc827faf81aa8c14dull,0x50389629bd5a4298ull,
        0x415536b2f2ed1973ull,0xc0444654dd17ada7ull,0xc150b94f9f8fd35aull,0x27da2107ce797397ull,
        0xbaf2288cf31fc7e0ull,0xcbcac786cfb0535eull,0xbaf2288cf31fc7e0ull,0xcbcac786cfb0535eull,
        0x44e8a126eb0375ffull,0xe580db761bcab6fbull,0x7d4fc5e21f4f2d5bull,0xc48f03fdc9234347ull,
        0xcf0cc7c0e112a6bull,0x946edd1b79a08b13ull,0xda325eda8187e8a8ull,0x9727c889aa468aedull,
        0x7563cd457d3e1f10ull,0x6fb52a0b14b1418full,0x994a4ee0c36ea0eeull,0xaaeef4e560628be7ull,
        0xcef6723aba22ae09ull,0x79f5edd46a1a0767ull,0xcef6723aba22ae09ull,0x79f5edd46a1a0767ull,
        0x8788f7c13852ef86ull,0x586426cc1add96a6ull,0x228093dfb5f2f364ull,0x732d65b674acd856ull,
        0x100ce07217ed55f9ull,0xedbd9739266bb8ceull,0x16882427a16a7332ull,0x16ab630dcba0dc0eull,
        0xd3d614b107b86b91ull,0x6daf1c87d5f4c29ull,0x83f99bcb2b61502ull,0x2f5ecd35ab2ed9a4ull,
        0xacbbf64aaeaaefd5ull,0xe7a99cbff4903deaull,0xacbbf64aaeaaefd5ull,0xe7a99cbff4903deaull,
        0xdd7e902cf996d2bcull,0xd0d98165cec0144cull,0xd4e66a1ac04a4373ull,0xef23904977692953ull,
        0x581582d24d4af575ull,0xafa8365048468345ull,0x21f297452c5c7912ull,0xcda1c60d8bbdaddfull,
        0x6c68c8e7e286ce26ull,0x17b9c2760752c215ull,0xcbff713be7c1aaaaull,0x92074d556b64f581ull,
        0x381dc0c43c11b445ull,0xf6204c6213b933bbull,0x381dc0c43c11b445ull,0xf6204c6213b933bbull,
        0xa8f218aa5439c9e1ull,0xaa4c1208556e6720ull,0xbe01060d95adb0f0ull,0xd89d6c05dc0645baull,
        0x314980dfa11bcc65ull,0xbdaed45f68254490ull,0x255cfa108216ca2cull,0x5ff6da9ae97c3ce2ull,
        0x146b970a9aa8991full,0xd3e18bbc43eeae2bull,0xdcb566409676b81cull,0xc102096a8e560dc1ull,
        0x30460997b5096435ull,0x2f7b622115c0f4e9ull,0x30460997b5096435ull,0x2f7b622115c0f4e9ull,
        0x5901b9e6890c0d06ull,0xd0f581156cc9af83ull,0xfbfab34745bc51e9ull,0xad254c8fe2406ea5ull,
        0x1a7ef20b70c76b29ull,0x1cb787c88194ef9cull,0xa164a1849b50bd69ull,0x651d4f591a2cbb45ull,
        0x9f60fcf1ab8c4f01ull,0x7ab938a084364f5ull,0x293bbb96901f6b57ull,0x90feefe99dfc43b5ull,
        0x1019e82ec27b8a0full,0x3e0b3053056fd627ull,0x1019e82ec27b8a0full,0x3e0b3053056fd627ull,
        0x5344a05cd8b35329ull,0xb0f0668299abade1ull,0x1395a642eead608cull,0x7f718990e588eb75ull,
        0x4f4be4c5fd48b9dull,0x53f06d34980277e2ull,0x10f2b47e0ddd604ull,0x87bcd0e324999da5ull,
    };
    for(unsigned mode=0;mode<128u;++mode)for(auto workers:{1u,4u}) {
        const auto actual=raster_shading_workload(mode,1u,workers).hash;
        if(actual!=expected[mode])throw std::runtime_error("Original raster shading reference mismatch: "+std::to_string(mode));
    }
}
}
