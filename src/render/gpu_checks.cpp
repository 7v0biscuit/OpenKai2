// SPDX-License-Identifier: GPL-3.0-only
#include <cstdlib>
#include "gpu_checks.h"
#include "gpu_backend.h"
#include "gpu_device.h"
#include "gpu_layout.h"
#include "graphics_settings.h"
#include "hd_textures.h"
#include <filesystem>
#include "runtime_layout.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_frontend.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace openkai2 {
namespace {
std::bitset<gpu::PageCount> referencePages(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y,unsigned w,unsigned h){
    std::bitset<gpu::PageCount> result;if(!w || !h)return result;
    if(uint64_t(w)*h>4u*1024u*1024u){result.set();return result;}
    for(unsigned row=y;row<y+h;row=std::min(y+h,(row|7u)+1u))
        for(unsigned col=x;col<x+w;col=std::min(x+w,(col|7u)+1u))result.set(gpu::bit_address(psm,base,width,col,row)/(gpu::PageBytes*8u));
    return result;
}
void checkFootprints(){
    constexpr unsigned formats[]={0u,1u,2u,10u,19u,20u,27u,36u,44u,48u,49u,50u,58u};
    constexpr unsigned sizes[]={0u,1u,7u,8u,9u,31u,32u,63u,64u,65u,127u,128u,129u,255u,256u,513u};
    unsigned cases=0u;
    for(unsigned psm:formats)for(unsigned base:{0u,1u,31u,32u,33u,4096u,16352u,16383u})for(unsigned width:{0u,1u,2u,8u,32u})for(unsigned i=0;i<16u;++i){
        unsigned x=(i*63u+7u)%2048u,y=(i*127u+31u)%2048u,w=sizes[i],h=sizes[(i*7u+3u)%16u];
        if(gpu::rectangle_pages(psm,base,width,x,y,w,h)!=referencePages(psm,base,width,x,y,w,h))throw std::runtime_error("GPU page footprint differs from the previous addressing walk");++cases;
    }
    if(!gpu::rectangle_pages(0u,0u,1u,0u,0u,4096u,4096u).all())throw std::runtime_error("Large GPU page footprint lost conservative coverage");
    std::cout<<"GPU page footprint references passed: "<<cases<<" states.\n";
}
struct Comparison {
    std::vector<uint8_t> expected,actual;
    std::unique_ptr<GSCpuBackend> cpu=std::make_unique<GSCpuBackend>();
    std::unique_ptr<GpuRasterBackend> gpu=std::make_unique<GpuRasterBackend>();
    explicit Comparison(bool pattern=true):expected(openkai2::gpu::VramBytes),actual(openkai2::gpu::VramBytes){
        if(pattern)for(unsigned i=0;i<expected.size();++i)expected[i]=uint8_t(i*13u^(i>>9u));
        actual=expected;cpu->Initialize(expected.data(),unsigned(expected.size()));gpu->Initialize(actual.data(),unsigned(actual.size()));
    }
    void write(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y,uint32_t value){cpu->WriteVram(psm,base,width,x,y,value);gpu->WriteVram(psm,base,width,x,y,value);}
    void draw(const GSPrimitiveBatch& p){cpu->Submit(p);gpu->Submit(p);}
    void textureFlush(){cpu->TextureFlush();gpu->TextureFlush();}
    void compare(const std::string& label){
        std::vector<uint8_t> snapshot;gpu->SnapshotVram(snapshot);
        if(snapshot.size()!=expected.size())throw std::runtime_error(label+": invalid GPU snapshot size");
        const auto mismatch=std::mismatch(expected.begin(),expected.end(),snapshot.begin());
        if(mismatch.first!=expected.end())throw std::runtime_error(label+": VRAM differs at byte "+std::to_string(mismatch.first-expected.begin())+" (CPU="+std::to_string(*mismatch.first)+", GPU="+std::to_string(*mismatch.second)+")");
    }
};
GSPrimitiveBatch material(unsigned mode,bool sprite){
    GSPrimitiveBatch b{};b.vertexCount=sprite ? 2u : 3u;auto& s=b.state;auto& c=s.context;
    s.prim.type=sprite ? GS_PRIM_SPRITE : GS_PRIM_TRIANGLE;s.prim.tme=s.prim.iip=s.prim.abe=true;
    s.prim.fst=(mode&16u)==0u;s.linearFilter=mode&1u;s.colclamp=1u;
    s.textureWidth=s.textureHeight=128u;s.texa.ta0=37u;s.texa.ta1=211u;
    c.frame.fbp=56u;c.frame.fbw=8u;c.zbuf.zbp=168u;c.zbuf.psm=GS_PSM_Z32;c.zbuf.zmask=true;
    c.scissor={0u,511u,0u,223u};c.test=0x30000u;c.alpha=0x44u;
    constexpr uint8_t formats[]={GS_PSM_CT32,GS_PSM_CT16,GS_PSM_T8,GS_PSM_T4};
    c.tex0.psm=formats[(mode/32u)%4u];c.tex0.tbp0=10240u;c.tex0.tbw=2u;
    c.tex0.tfx=(mode/2u)%4u;c.tex0.tcc=(mode&8u)!=0u;
    c.tex0.cbp=16000u;c.tex0.cpsm=GS_PSM_CT32;c.tex0.cld=1u;
    for(unsigned i=0;i<3u;++i){auto& v=b.vertices[i];
        v.r=uint8_t(i==0u ? 0u : i==1u ? 255u : 127u);v.g=uint8_t(i==0u ? 255u : i==1u ? 1u : 191u);
        v.b=uint8_t(i==0u ? 73u : i==1u ? 254u : 0u);v.a=uint8_t(i==0u ? 0u : i==1u ? 255u : 128u);
        v.z=1000u+i*117u;v.q=0.75f+float(i)*0.5f;v.u=i==1u ? 2044u : 8u;v.v=i==2u ? 2040u : 12u;
        v.s=float(v.u)/2048.0f;v.t=float(v.v)/2048.0f;v.fog=uint8_t(i*101u);
    }
    b.vertices[0].x=0.25f;b.vertices[0].y=0.5f;b.vertices[1].x=255.25f;b.vertices[1].y=sprite ? 191.25f : 0.75f;b.vertices[2].x=0.5f;b.vertices[2].y=191.25f;
    if(sprite){b.vertices[1].v=2040u;b.vertices[1].t=float(b.vertices[1].v)/2048.0f;}
    return b;
}
void checkDepthEnable(){
    unsigned cases=0u;
    for(unsigned framePsm:{0u,2u})for(unsigned depthPsm:{48u,49u,50u,58u}){
        Comparison pair(false);
        for(bool enabled:{false,true})for(unsigned method=0;method<4u;++method)for(bool masked:{false,true})
            for(unsigned primitive:{0u,1u,3u,6u}){
                GSPrimitiveBatch b{};auto& s=b.state;auto& c=s.context;
                s.prim.type=static_cast<GSPrimType>(primitive);s.colclamp=1u;
                b.vertexCount=primitive==0u ? 1u : primitive==3u ? 3u : 2u;
                c.frame.fbp=56u;c.frame.fbw=1u;c.frame.psm=framePsm;
                c.zbuf.zbp=168u;c.zbuf.psm=depthPsm;c.zbuf.zmask=masked;
                c.test=(uint64_t(enabled)<<16u)|(uint64_t(method)<<17u);c.scissor={0u,63u,0u,63u};
                for(unsigned i=0;i<3u;++i){
                    auto& v=b.vertices[i];v.x=16;v.y=16;v.z=enabled ? 101u : 1u;
                    v.r=16;v.g=32;v.b=64;v.a=128;v.q=1;
                }
                if(primitive<=1u){b.vertices[0].x=18;b.vertices[0].y=18;b.vertices[1].x=34;b.vertices[1].y=18;}
                else {b.vertices[1].x=40;b.vertices[1].y=primitive==6u ? 40 : 16;b.vertices[2].y=40;}
                pair.write(framePsm,56u*32u,1u,18u,18u,0u);
                pair.write(depthPsm,168u*32u,1u,18u,18u,100u);
                pair.draw(b);
                const bool passes=!enabled || method!=0u;
                const uint32_t color=passes ? (framePsm==0u ? 0x80402010u : 0xa082u) : 0u;
                const uint32_t depth=passes && enabled && !masked ? 101u : 100u;
                for(auto* backend:{static_cast<GSRasterBackend*>(pair.cpu.get()),static_cast<GSRasterBackend*>(pair.gpu.get())}){
                    if(backend->ReadVram(framePsm,56u*32u,1u,18u,18u)!=color ||
                       backend->ReadVram(depthPsm,168u*32u,1u,18u,18u)!=depth)
                        throw std::runtime_error("GS depth-enable state changed color or wrote depth while disabled: enable="+
                            std::to_string(enabled)+" method="+std::to_string(method)+" primitive="+std::to_string(primitive));
                }
                ++cases;
            }
        pair.compare("Depth-enable state");
    }
    std::cout<<"GPU depth-enable references passed: "<<cases<<" point/line/triangle/sprite, color/depth formats and write masks.\n";
}
void checkPaletteReuse(){
    unsigned cases=0u;
    for(unsigned psm:{19u,20u,44u})for(unsigned cpsm:{0u,2u})for(unsigned csm:{0u,1u}){
        Comparison pair;auto batch=material(64u,true);auto& t=batch.state.context.tex0;
        t.psm=psm;t.cpsm=cpsm;t.csm=csm;t.csa=0u;t.cld=1u;
        batch.state.prim.fst=true;batch.state.prim.abe=false;batch.vertices[0].x=batch.vertices[0].y=8;
        batch.vertices[1].x=40;batch.vertices[1].y=32;batch.vertices[0].u=batch.vertices[0].v=0;
        batch.vertices[1].u=32u*16u;batch.vertices[1].v=24u*16u;
        GSTexClutReg tc{};tc.cbw=1u;tc.cou=2u;tc.cov=3u;batch.state.texclut=tc;
        const auto load=[&]{pair.cpu->LoadClut(t,tc);pair.gpu->LoadClut(t,tc);};
        load();const auto initial=pair.gpu->stats();
        for(unsigned i=0;i<64u;++i){load();pair.draw(batch);}
        const auto repeated=pair.gpu->stats();
        if(repeated.paletteLoads!=initial.paletteLoads || repeated.paletteReuses-initial.paletteReuses!=64u)
            throw std::runtime_error("Unchanged CLD=1 palette loads were repeated");
        pair.compare("Repeated palette draws");
        t.cld=2u;load();t.cld=1u;const auto before=pair.gpu->stats();load();
        if(pair.gpu->stats().paletteLoads!=before.paletteLoads+1u)
            throw std::runtime_error("CBP-register load reused a prior palette key");
        const unsigned x=csm?unsigned(tc.cou)*16u:0u,y=csm?tc.cov:0u;
        pair.write(cpsm,t.cbp,1u,x,y,cpsm?0xffffu:0x80a07030u);
        auto prior=pair.gpu->stats().paletteLoads;load();pair.draw(batch);
        if(pair.gpu->stats().paletteLoads!=prior+1u)throw std::runtime_error("CPU palette write did not invalidate reuse");
        GSTransferCommand transfer{};transfer.direction=0u;transfer.bitbltbuf.dpsm=cpsm;
        transfer.bitbltbuf.dbp=t.cbp;transfer.bitbltbuf.dbw=1u;transfer.trxpos.dsax=x;transfer.trxpos.dsay=y;
        transfer.trxreg.rrw=1u;transfer.trxreg.rrh=1u;const uint32_t changed=0x80306090u;
        pair.cpu->BeginTransfer(transfer);pair.gpu->BeginTransfer(transfer);
        pair.cpu->UploadImage(reinterpret_cast<const uint8_t*>(&changed),cpsm?2u:4u);
        pair.gpu->UploadImage(reinterpret_cast<const uint8_t*>(&changed),cpsm?2u:4u);
        prior=pair.gpu->stats().paletteLoads;load();pair.draw(batch);
        if(pair.gpu->stats().paletteLoads!=prior+1u)throw std::runtime_error("Palette upload did not invalidate reuse");
        auto paint=batch;paint.state.prim.tme=false;auto& target=paint.state.context;
        target.frame.fbp=t.cbp/32u;target.frame.fbw=1u;target.frame.psm=cpsm;target.test=0x30000u;
        target.scissor={0u,63u,0u,31u};paint.vertices[0].x=float(x);paint.vertices[0].y=float(y);
        paint.vertices[1].x=float(x+1u);paint.vertices[1].y=float(y+1u);pair.draw(paint);
        prior=pair.gpu->stats().paletteLoads;load();pair.draw(batch);
        if(pair.gpu->stats().paletteLoads!=prior+1u)throw std::runtime_error("GPU palette write did not invalidate reuse");
        pair.compare("Palette edit and CBP transitions");
        pair.cpu->Reset();pair.gpu->Reset();prior=pair.gpu->stats().paletteLoads;load();pair.draw(batch);
        if(pair.gpu->stats().paletteLoads!=prior+1u)throw std::runtime_error("Palette reuse survived reset");
        pair.compare("Palette reset");++cases;
    }
    std::cout<<"GPU palette reuse comparisons passed: "<<cases<<" formats/addressing states, 768 reused loads, CPU/upload/GPU edits, CBP transitions and reset.\n";
}
void checkPresentationReadbacks(){
    for(unsigned variant=0;variant<19u;++variant){
        Comparison pair(false);auto draw=material(0u,true);
        draw.state.prim.tme=draw.state.prim.abe=false;
        auto& context=draw.state.context;context.scissor={0u,127u,0u,127u};context.zbuf.zmask=false;
        if(variant&1u)context.frame.psm=GS_PSM_CT16;
        draw.vertices[0].x=draw.vertices[0].y=0;
        draw.vertices[1].x=draw.vertices[1].y=128;
        draw.vertices[1].r=71;draw.vertices[1].g=97;draw.vertices[1].b=131;
        pair.draw(draw);
        auto other=draw;other.state.context.frame.fbp=112;other.state.context.zbuf.zbp=224;
        other.vertices[1].r=193;pair.draw(other);
        GSPresentationRequest request{};request.pmode=1;
        request.dispfb1=uint64_t(56)|(uint64_t(8)<<9)|(uint64_t(context.frame.psm)<<15);
        request.display1=(uint64_t(63)<<32)|(uint64_t(63)<<44);
        if(variant&2u)request.dispfb1|=(uint64_t(32)<<32)|(uint64_t(16)<<43);
        if(variant&4u)request.smode2=3;
        if(variant>=8u && variant<12u){request.pmode=3;request.dispfb2=112u|(uint64_t(8)<<9)|(uint64_t(context.frame.psm)<<15);request.display2=request.display1;}
        if(variant>=12u && variant<16u){request.smode2=0;request.hasPreferredSource=true;request.preferredDestFbp=56;
            request.preferredSource=other.state.context.frame;request.preferredSource.fbp*=32u;}
        if(variant==16u || variant==17u){request.smode2=0;request.dispfb1&=~511ull;
            request.contextFrames[0]=variant==16u ? context.frame : other.state.context.frame;
            request.contextFrames[1]=variant==16u ? other.state.context.frame : context.frame;}
        if(variant==18u){request.smode2=0;request.display1=0;}
        const auto before=pair.gpu->stats().readbacks;
        pair.gpu->Sync(GSSyncReason::Presentation);
        if(pair.gpu->stats().readbacks!=before)throw std::runtime_error("Presentation sync read unrelated GPU pages: "+std::to_string(pair.gpu->stats().readbacks-before));
        const auto actual=pair.gpu->Present(request),expected=pair.cpu->Present(request);
        if(actual.width!=expected.width || actual.height!=expected.height || actual.pixels!=expected.pixels)
            throw std::runtime_error("Selective presentation changed color, origin, field reconstruction or CRT merge");
        const auto copied=pair.gpu->stats().readbacks-before;
        if(!copied || copied>=64u*gpu::PageBytes)throw std::runtime_error("Presentation copied unrelated targets or depth");
        if(!variant){
            if(copied!=2u*gpu::PageBytes)throw std::runtime_error("Color presentation copied more than its two physical pages");
            std::cout<<"GPU presentation fixture: copied "<<copied<<" bytes; retained depth and offscreen targets.\n";
        }
        const auto depth=pair.gpu->ReadVram(GS_PSM_Z32,168u*32u,8u,4u,4u);
        if(depth!=pair.cpu->ReadVram(GS_PSM_Z32,168u*32u,8u,4u,4u) || pair.gpu->stats().readbacks-before!=copied+gpu::PageBytes)
            throw std::runtime_error("Presentation lost a deferred depth page: "+std::to_string(variant));
        pair.compare("Selective presentation retains offscreen GPU contents");
    }
}
void checkEmptyTriangles(){
    Comparison pair;auto draw=material(0u,false);draw.state.prim.tme=false;draw.state.prim.abe=false;
    draw.vertices[0].x=10.25f;draw.vertices[0].y=11.5f;
    draw.vertices[1].x=71.75f;draw.vertices[1].y=61.25f;
    draw.vertices[2].x=20.5f;draw.vertices[2].y=75.0f;
    const auto original=draw;
    for(unsigned mode=0;mode<5u;++mode){draw=original;
        if(mode==0u)draw.vertices[1]=draw.vertices[0];
        if(mode==1u)draw.vertices[2]=draw.vertices[0];
        if(mode==2u)draw.vertices[2]=draw.vertices[1];
        if(mode==3u)for(auto& v:draw.vertices)v.x=17.25f;
        if(mode==4u)for(auto& v:draw.vertices)v.y=25.75f;
        pair.draw(draw);pair.compare("Empty triangle "+std::to_string(mode));
    }
    const auto empty=pair.gpu->stats();
    if(empty.culled!=5u || empty.batches!=0u || empty.tiles!=0u)throw std::runtime_error("Empty triangles reached GPU tile processing");
    pair.draw(original);pair.compare("Visible triangle after empty strip vertices");
    if(pair.gpu->stats().batches!=1u)throw std::runtime_error("Empty-strip culling lost a subsequent visible draw");
}
void checkOffscreenTriangles(){
    unsigned cases=0;
    for(unsigned primitive:{3u,4u,5u})for(unsigned format:{0u,2u})for(bool offset:{false,true}){
        Comparison pair(false);
        auto original=material(0u,false);auto& state=original.state;
        state.prim.type=static_cast<GSPrimType>(primitive);state.prim.tme=state.prim.abe=false;
        state.context.frame.psm=format;state.context.scissor={32u,95u,32u,95u};
        state.context.xyoffset.ofx=offset ? 28673u : 0u;
        state.context.xyoffset.ofy=offset ? 29187u : 0u;
        for(unsigned side=0;side<4;++side)for(bool outside:{true,false}){
            auto draw=original;
            const float shift=side%2 ? (outside ? 72.0f : 48.0f) : (outside ? -24.0f : -12.0f);
            for(unsigned i=0;i<3;++i){
                auto& v=draw.vertices[i];
                v.x=40.0f+(i==1 ? 12.0f : 0.0f)+float(state.context.xyoffset.ofx>>4u);
                v.y=40.0f+(i==2 ? 12.0f : 0.0f)+float(state.context.xyoffset.ofy>>4u);
                if(side<2)v.x+=shift;else v.y+=shift;
            }
            const auto before=pair.gpu->stats();
            pair.draw(draw);pair.compare("Triangle scissor boundary "+std::to_string(cases++));
            const auto after=pair.gpu->stats();
            if(outside && (after.culled!=before.culled+1 || after.batches!=before.batches || after.tiles!=before.tiles))
                throw std::runtime_error("Offscreen triangle reached GPU tile processing");
            if(!outside && after.batches!=before.batches+1)
                throw std::runtime_error("Scissor culling removed a partially visible triangle");
        }
    }
    std::cout<<"GPU triangle scissor references passed: "<<cases<<" outside and intersecting draws.\n";
}
void checkMaterials(){
    for(unsigned type=0;type<2u;++type)for(unsigned mode=0;mode<128u;++mode){
        Comparison pair;auto p=material(mode,type!=0u);auto& s=p.state;auto& c=s.context;
        for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x)
            pair.write(0u,16000u,1u,x,y,((x*17u)&255u)|(((y*17u)&255u)<<8u)|(((x^y)*17u)<<16u)|(((x+y)*13u&255u)<<24u));
        pair.cpu->LoadClut(c.tex0,{});pair.gpu->LoadClut(c.tex0,{});
        for(unsigned i=0;i<3u;++i)pair.draw(p);
        pair.compare("GPU material "+std::to_string(type)+":"+std::to_string(mode));
        for(auto& v:p.vertices)v.u=v.v=8u;s.prim.fst=true;s.prim.iip=false;s.linearFilter=false;
        pair.write(c.tex0.psm,c.tex0.tbp0,c.tex0.tbw,0u,0u,0x80ff13a7u);pair.textureFlush();pair.draw(p);
        pair.compare("GPU texture update "+std::to_string(type)+":"+std::to_string(mode));
        const auto stats=pair.gpu->stats();if(stats.gpuDraws!=4u || !stats.batches)throw std::runtime_error("Material fixture did not execute on the GPU");
    }
}
void checkIntegerPipeline(){
    Comparison pair(false);constexpr unsigned cases=32768u;
    for(unsigned i=0;i<cases;++i){const unsigned x=i%512u,y=i/512u;pair.write(0u,56u*32u,8u,x,y,0x8013579bu^(i*0x10201u));pair.write(GS_PSM_Z32,168u*32u,8u,x,y,1000u);}
    for(unsigned i=0;i<cases;++i){
        GSPrimitiveBatch p{};p.vertexCount=2u;auto& s=p.state;auto& c=s.context;s.prim.type=GS_PRIM_SPRITE;s.prim.abe=true;s.pabe=(i&1024u)!=0u;s.colclamp=1u;
        c.frame.fbp=56u;c.frame.fbw=8u;c.zbuf.zbp=168u;c.zbuf.psm=GS_PSM_Z32;c.zbuf.zmask=(i&2048u)!=0u;c.scissor={0u,511u,0u,63u};
        const unsigned method=(i/256u)%8u,fail=(i/2048u)%4u,depth=(i/8192u)%4u;
        c.test=1u|(method<<1u)|(128u<<4u)|(fail<<12u)|(depth<<17u);
        if(i&4096u)c.test|=1u<<14u;if(i&512u)c.test|=1u<<15u;
        c.alpha=(i&255u)|(uint64_t((i*17u)&255u)<<32u);c.fba=(i&16384u)!=0u;c.frame.fbmsk=(i&8192u) ? 0x00ff00ffu : 0u;
        const unsigned x=i%512u,y=i/512u;p.vertices[0].x=float(x);p.vertices[0].y=float(y);p.vertices[1].x=float(x+1u);p.vertices[1].y=float(y+1u);
        auto& v=p.vertices[1];v.r=uint8_t(i*11u);v.g=uint8_t(i*37u);v.b=uint8_t(i*53u);constexpr uint8_t alpha[]={0u,127u,128u,255u};v.a=alpha[(i/64u)%4u];v.z=i%3u==0u ? 2000u : 1000u;pair.draw(p);
    }
    pair.compare("GPU integer alpha, depth and blend pipeline");if(pair.gpu->stats().gpuDraws!=cases)throw std::runtime_error("Integer pipeline fixture fell back to CPU rendering");
}
void checkFormats(){
    constexpr unsigned textures[]={0u,1u,2u,10u,19u,20u,27u,36u,44u,48u,49u,50u,58u};
    constexpr unsigned frames[]={0u,1u,2u,10u},depths[]={48u,49u,50u,58u};
    for(unsigned mode=0;mode<104u;++mode){
        Comparison pair;auto p=material(8u|(mode&1u),mode&1u);auto& s=p.state;auto& c=s.context;
        c.frame.psm=frames[(mode/2u)%4u];c.zbuf.psm=depths[(mode/8u)%4u];c.zbuf.zmask=false;
        c.tex0.psm=textures[mode/8u];c.tex0.cpsm=frames[(mode/4u)%4u];c.tex0.csa=(mode&16u) ? 17u : 0u;
        c.tex0.csm=(mode&32u)!=0u;s.texa.aem=(mode&4u)!=0u;
        const unsigned wrap=(mode/2u)%4u;
        c.clamp=wrap|(uint64_t(wrap)<<2u)|(uint64_t(3u)<<4u)|(uint64_t(31u)<<14u)|(uint64_t(5u)<<24u)|(uint64_t(63u)<<34u);
        s.prim.fst=false;s.prim.fge=(mode&2u)!=0u;s.fogR=23u;s.fogG=177u;s.fogB=89u;
        pair.cpu->LoadClut(c.tex0,{});pair.gpu->LoadClut(c.tex0,{});
        for(unsigned i=0;i<3u;++i)pair.draw(p);
        pair.compare("GPU format and addressing state "+std::to_string(mode));
        if(pair.gpu->stats().gpuDraws!=3u)throw std::runtime_error("Format fixture fell back to CPU rendering");
    }
    for(unsigned frame:frames)for(unsigned depth:depths){
        Comparison pair;constexpr unsigned cases=1024u;
        for(unsigned i=0;i<cases;++i){unsigned x=i%128u,y=i/128u;pair.write(frame,56u*32u,8u,x,y,0x80b3579bu^(i*0x10003u));pair.write(depth,168u*32u,8u,x,y,0xf0001234u);}
        for(unsigned i=0;i<cases;++i){
            auto p=material(0u,true);auto& s=p.state;auto& c=s.context;s.prim.tme=false;s.pabe=(i&1u)!=0u;
            c.frame.psm=frame;c.zbuf.psm=depth;c.zbuf.zmask=(i&2u)!=0u;c.fba=(i&4u)!=0u;
            c.frame.fbmsk=(i&8u) ? 0xf00f0ff0u : 0u;c.alpha=(i&255u)|(uint64_t(i&255u)<<32u);
            c.test=1u|(((i/4u)%8u)<<1u)|(128u<<4u)|(((i/32u)%4u)<<12u)|(((i/128u)%4u)<<17u);
            if(i&512u)c.test|=1u<<14u;if(i&256u)c.test|=1u<<15u;
            unsigned x=i%128u,y=i/128u;p.vertices[0].x=float(x);p.vertices[0].y=float(y);p.vertices[1].x=float(x+1u);p.vertices[1].y=float(y+1u);
            p.vertices[1].a=uint8_t(i*37u);p.vertices[1].z=(i&16u) ? 0xf0009876u : 0x1234u;
            pair.draw(p);pair.draw(p);
        }
        pair.compare("GPU target formats "+std::to_string(frame)+":"+std::to_string(depth));
        if(pair.gpu->stats().gpuDraws!=cases*2u)throw std::runtime_error("Target format fixture fell back to CPU rendering");
    }
}
void checkCoherence(){
    Comparison pair(false);auto p=material(8u,false);p.state.prim.tme=false;p.state.prim.iip=false;p.state.prim.abe=false;p.state.context.zbuf.zmask=false;
    pair.draw(p);pair.write(0u,56u*32u,8u,7u,11u,0x80112233u);pair.compare("GPU writeback before CPU framebuffer update");
    p.state.context.frame.fbp=112u;p.state.context.tex0.tbp0=56u*32u;p.state.context.tex0.tbw=8u;p.state.prim.tme=true;p.state.prim.fst=true;
    p.state.context.tex0.tfx=1u;p.state.context.tex0.tcc=true;p.state.textureWidth=p.state.textureHeight=128u;pair.textureFlush();pair.draw(p);pair.compare("GPU render-to-texture target transition");
    auto feedback=p;feedback.state.context.tex0.tbp0=112u*32u;pair.textureFlush();pair.draw(feedback);pair.compare("GPU texture feedback software fallback");
    if(!pair.gpu->stats().cpuDraws)throw std::runtime_error("Texture feedback did not use the compatibility fallback");
    GSTransferCommand transfer{};transfer.direction=0u;transfer.bitbltbuf.dbp=112u*32u;transfer.bitbltbuf.dbw=8u;transfer.bitbltbuf.dpsm=0u;transfer.trxpos.dsax=3u;transfer.trxpos.dsay=5u;transfer.trxreg.rrw=3u;transfer.trxreg.rrh=2u;
    const uint32_t data[]={0x80112233u,0x80445566u,0x80778899u,0x80aabbccu,0x80ddeeffu,0x8013579bu};
    pair.cpu->BeginTransfer(transfer);pair.gpu->BeginTransfer(transfer);pair.cpu->UploadImage(reinterpret_cast<const uint8_t*>(data),sizeof(data));pair.gpu->UploadImage(reinterpret_cast<const uint8_t*>(data),sizeof(data));pair.compare("GPU partial host upload preserves neighboring pixels");
    transfer.direction=2u;transfer.bitbltbuf.sbp=112u*32u;transfer.bitbltbuf.sbw=8u;transfer.bitbltbuf.spsm=0u;transfer.bitbltbuf.dbp=56u*32u;transfer.trxpos.ssax=3u;transfer.trxpos.ssay=5u;
    pair.cpu->BeginTransfer(transfer);pair.gpu->BeginTransfer(transfer);pair.compare("GPU local-to-local transfer");
}
void checkTransferPages(){
    Comparison pair;auto p=material(8u,false);p.state.prim.tme=false;p.state.prim.iip=false;p.state.prim.abe=false;
    pair.draw(p);
    auto sample=material(24u,true);sample.state.context.frame.fbp=112u;sample.state.context.tex0.psm=GS_PSM_CT32;
    sample.state.context.tex0.tbp0=200u*32u;sample.state.context.tex0.tbw=2u;sample.state.textureWidth=sample.state.textureHeight=64u;
    sample.state.prim.abe=false;pair.draw(sample);
    GSTransferCommand t{};t.direction=2u;t.bitbltbuf.sbp=56u*32u;t.bitbltbuf.sbw=8u;t.bitbltbuf.spsm=0u;
    t.bitbltbuf.dbp=200u*32u;t.bitbltbuf.dbw=2u;t.bitbltbuf.dpsm=0u;t.trxpos.ssax=10u;t.trxpos.ssay=12u;t.trxreg.rrw=64u;t.trxreg.rrh=40u;
    const auto before=pair.gpu->stats();
    pair.cpu->BeginTransfer(t);pair.gpu->BeginTransfer(t);pair.textureFlush();
    sample.state.context.frame.fbp=140u;pair.draw(sample);
    const auto after=pair.gpu->stats();
    if(after.uploads-before.uploads>=gpu::VramBytes/8u || after.readbacks-before.readbacks>=gpu::VramBytes/8u)
        throw std::runtime_error("Local-to-local transfer moved unrelated VRAM pages");
    pair.compare("GPU local-to-local transfer between a render target and a sampled texture");
    t.direction=1u;t.trxpos.ssax=0u;t.trxpos.ssay=0u;t.trxreg.rrw=32u;t.trxreg.rrh=8u;pair.draw(p);
    pair.cpu->BeginTransfer(t);pair.gpu->BeginTransfer(t);
    std::vector<uint8_t> expected(32u*8u*4u),actual(expected.size());
    if(pair.cpu->ConsumeLocalToHostBytes(expected.data(),uint32_t(expected.size()))!=expected.size() ||
       pair.gpu->ConsumeLocalToHostBytes(actual.data(),uint32_t(actual.size()))!=actual.size() || expected!=actual)
        throw std::runtime_error("GPU local-to-host transfer differs from the CPU renderer");
    pair.compare("GPU local-to-host transfer");
    std::cout<<"GPU transfer page checks passed: render-target copies, pending samplers and host readback.\n";
}
void checkGpuTransfers(){
    constexpr unsigned formats[]={GS_PSM_CT32,GS_PSM_CT24,GS_PSM_CT16,GS_PSM_CT16S,GS_PSM_T8,GS_PSM_T4,GS_PSM_T8H,GS_PSM_T4HL,GS_PSM_T4HH,48u,50u};
    unsigned cases=0u;
    for(unsigned psm:formats){
        Comparison pair;auto p=material(8u,false);p.state.prim.tme=false;p.state.prim.iip=false;p.state.prim.abe=false;
        pair.draw(p);pair.textureFlush();
        GSTransferCommand t{};t.direction=0u;t.bitbltbuf.dbp=56u*32u;t.bitbltbuf.dbw=8u;t.bitbltbuf.dpsm=uint8_t(psm);
        t.trxpos.dsax=3u;t.trxpos.dsay=5u;t.trxreg.rrw=37u;t.trxreg.rrh=11u;
        const unsigned bpp=gpu::address_tables()[(psm&63u)*8u+3u];
        std::vector<uint8_t> data((37u*11u*bpp/8u+15u)&~15u);for(unsigned i=0;i<data.size();++i)data[i]=uint8_t(i*37u+psm*11u+(i>>5u));
        const auto before=pair.gpu->stats();
        pair.cpu->BeginTransfer(t);pair.gpu->BeginTransfer(t);
        for(unsigned first=0;first<data.size();first+=64u){const unsigned size=std::min<unsigned>(64u,unsigned(data.size())-first);
            pair.cpu->UploadImage(data.data()+first,size);pair.gpu->UploadImage(data.data()+first,size);}
        const auto after=pair.gpu->stats();
        if(after.readbacks!=before.readbacks || after.gpuTransfers==before.gpuTransfers)
            throw std::runtime_error("Upload into drawn pages read GPU VRAM back: psm="+std::to_string(psm));
        pair.compare("GPU upload into drawn pages psm="+std::to_string(psm));
        if(bpp==24u){++cases;continue;}
        GSTransferCommand copy{};copy.direction=2u;copy.bitbltbuf.sbp=56u*32u;copy.bitbltbuf.sbw=8u;copy.bitbltbuf.spsm=uint8_t(psm);
        copy.bitbltbuf.dbp=300u*32u;copy.bitbltbuf.dbw=4u;copy.bitbltbuf.dpsm=uint8_t(psm);copy.trxpos.ssax=1u;copy.trxpos.ssay=2u;
        copy.trxpos.dsax=7u;copy.trxpos.dsay=3u;copy.trxreg.rrw=90u;copy.trxreg.rrh=21u;
        const auto beforeCopy=pair.gpu->stats();
        pair.cpu->BeginTransfer(copy);pair.gpu->BeginTransfer(copy);
        if(pair.gpu->stats().readbacks!=beforeCopy.readbacks)throw std::runtime_error("Copy out of drawn pages read GPU VRAM back");
        pair.compare("GPU copy out of drawn pages psm="+std::to_string(psm));cases+=2u;
    }
    std::cout<<"GPU VRAM transfer checks passed: "<<cases<<" uploads and copies across 11 formats without readbacks.\n";
}
void checkLargeTransfers(){
    for (unsigned height : {2047u, 2048u}) {
        Comparison pair;
        auto primitive = material(0u, false);
        primitive.state.prim.tme = false;
        pair.draw(primitive);
        pair.textureFlush();
        GSTransferCommand transfer{};
        transfer.direction = 0u;
        transfer.bitbltbuf.dbw = 32u;
        transfer.bitbltbuf.dpsm = GS_PSM_T8;
        transfer.trxreg.rrw = 2048u;
        transfer.trxreg.rrh = height;
        std::vector<uint8_t> image(2048u * height);
        for (size_t i = 0; i < image.size(); ++i)
            image[i] = uint8_t(i * 37u + (i >> 11u));
        const auto before = pair.gpu->stats();
        pair.cpu->BeginTransfer(transfer);
        pair.gpu->BeginTransfer(transfer);
        pair.cpu->UploadImage(image.data(), uint32_t(image.size()));
        pair.gpu->UploadImage(image.data(), uint32_t(image.size()));
        if (pair.gpu->stats().gpuTransfers != before.gpuTransfers + 1u)
            throw std::runtime_error("Large transfer did not exercise GPU image upload");
        pair.compare("Large GPU upload height=" + std::to_string(height));
    }
    std::cout << "GPU large-transfer checks passed: complete VRAM below and above 65535 workgroups.\n";
}
void checkLines(){
    constexpr unsigned frames[]={0u,1u,2u,10u};
    for(unsigned mode=0;mode<64u;++mode){
        Comparison pair;auto p=material(0u,false);auto& s=p.state;auto& c=s.context;
        s.prim.type=mode&1u ? GS_PRIM_LINE : GS_PRIM_LINESTRIP;p.vertexCount=2u;
        s.prim.iip=(mode&2u)!=0u;s.prim.fge=(mode&4u)!=0u;c.frame.psm=frames[(mode/8u)%4u];
        c.zbuf.psm=50u;c.zbuf.zmask=false;s.fogR=31u;s.fogG=79u;s.fogB=191u;
        const unsigned dx=(mode/2u)%8u,dy=(mode/16u)%4u;
        for(unsigned direction=0;direction<8u;++direction){
            const float x=float(19u+direction*29u),y=float(17u+direction*19u);
            p.vertices[0].x=x;p.vertices[0].y=y;
            p.vertices[1].x=x+float(direction&1u ? -int(dx) : int(dx));
            p.vertices[1].y=y+float(direction&2u ? -int(dy) : int(dy));
            if(direction&4u)std::swap(p.vertices[0],p.vertices[1]);
            pair.draw(p);
        }
        p.vertexCount=1u;s.prim.type=GS_PRIM_POINT;p.vertices[0].x=5.75f;p.vertices[0].y=6.25f;pair.draw(p);
        pair.compare("GPU line directions and point "+std::to_string(mode));
        if(pair.gpu->stats().gpuDraws!=9u)throw std::runtime_error("Line fixture fell back to CPU rendering");
    }
}
void checkRebind(){
    Comparison pair(false);auto p=material(0u,false);p.state.prim.tme=false;pair.draw(p);
    {std::vector<uint8_t> replacement(gpu::VramBytes,0x53u);pair.actual.swap(replacement);}
    std::fill(pair.expected.begin(),pair.expected.end(),0x53u);
    pair.cpu->Initialize(pair.expected.data(),unsigned(pair.expected.size()));pair.gpu->Initialize(pair.actual.data(),unsigned(pair.actual.size()));
    pair.draw(p);pair.compare("GPU host VRAM replacement");
    if(pair.gpu->stats().gpuDraws!=2u)throw std::runtime_error("Rebound VRAM fixture did not execute on GPU");
}
void checkClears(){
    constexpr unsigned formats[]={0u,1u,2u,10u};
    for(unsigned format:formats)for(unsigned mode=0;mode<4u;++mode){
        Comparison pair;auto p=material(0u,false);p.state.prim.tme=false;p.state.context.frame.psm=format;
        pair.draw(p);auto c=p.state.context;c.fba=mode&1u;c.frame.fbmsk=mode&2u ? 0x0ff00ff0u : 0u;
        c.scissor={3u,193u,5u,137u};
        if(!pair.cpu->ClearFramebuffer(c,0x3713a75bu) || !pair.gpu->ClearFramebuffer(c,0x3713a75bu))throw std::runtime_error("Framebuffer clear unexpectedly rejected");
        pair.compare("GPU framebuffer clear "+std::to_string(format)+":"+std::to_string(mode));
        if((format==0u || format==1u || !(mode&2u)) && pair.gpu->stats().gpuDraws!=2u)throw std::runtime_error("Eligible framebuffer clear did not execute on GPU");
    }
}
void checkSourceChanges(){
    Comparison pair;
    constexpr unsigned formats[]={0u,1u,2u,10u,19u,20u,27u,36u,44u,48u,49u,50u,58u};
    for(unsigned i=0;i<64u;++i){
        auto p=material(i%128u,false);auto& s=p.state;auto& c=s.context;
        c.tex0.psm=formats[i%13u];c.tex0.tbp0=10240u+(i%16u)*32u+((i&1u) ? 1u : 0u);c.tex0.tbw=1u+(i%3u);
        s.textureWidth=(i&2u) ? 64u : 128u;s.textureHeight=(i&4u) ? 64u : 128u;
        const unsigned wrap=(i/8u)%4u;c.clamp=wrap|(uint64_t(wrap)<<2u)|(uint64_t(3u)<<4u)|(uint64_t(31u)<<14u)|(uint64_t(7u)<<24u)|(uint64_t(63u)<<34u);
        p.vertices[1].x=15.25f;p.vertices[2].y=17.5f;
        pair.cpu->LoadClut(c.tex0,{});pair.gpu->LoadClut(c.tex0,{});pair.textureFlush();pair.draw(p);pair.draw(p);
        if((i&7u)==7u)pair.compare("GPU source footprint state changes "+std::to_string(i));
    }
    if(pair.gpu->stats().gpuDraws!=128u)throw std::runtime_error("Source footprint changes fell back to CPU rendering");
}
void checkPartialReadbacks(){
    Comparison pair;auto p=material(0u,false);p.state.prim.tme=false;p.state.prim.abe=false;p.state.prim.iip=false;
    pair.draw(p);p.state.context.frame.fbp=112u;pair.draw(p);
    auto before=pair.gpu->stats().readbacks;
    const auto value=pair.gpu->ReadVram(0u,56u*32u,8u,4u,4u);
    if(value!=pair.cpu->ReadVram(0u,56u*32u,8u,4u,4u) || pair.gpu->stats().readbacks-before!=gpu::PageBytes)throw std::runtime_error("Single-pixel GPU readback copied unrelated pages");
    before=pair.gpu->stats().readbacks;pair.write(0u,112u*32u,8u,4u,4u,0x8037a95bu);
    if(pair.gpu->stats().readbacks-before!=gpu::PageBytes)throw std::runtime_error("Partial framebuffer update copied unrelated pages");
    GSTransferCommand t{};t.direction=0u;t.bitbltbuf.dbp=112u*32u;t.bitbltbuf.dbw=8u;t.bitbltbuf.dpsm=0u;t.trxpos.dsax=70u;t.trxpos.dsay=4u;t.trxreg.rrw=1u;t.trxreg.rrh=1u;
    const uint32_t pixel=0x80231f7bu;before=pair.gpu->stats().readbacks;
    pair.cpu->BeginTransfer(t);pair.gpu->BeginTransfer(t);pair.cpu->UploadImage(reinterpret_cast<const uint8_t*>(&pixel),4u);pair.gpu->UploadImage(reinterpret_cast<const uint8_t*>(&pixel),4u);
    if(pair.gpu->stats().readbacks!=before)throw std::runtime_error("Partial image upload into a drawn page read GPU VRAM back");
    p.state.context.frame.fbp=500u;pair.draw(p);pair.textureFlush();
    GSTex0Reg texture{};texture.psm=19u;texture.cpsm=0u;texture.cbp=16000u;texture.cld=1u;
    before=pair.gpu->stats().readbacks;pair.cpu->LoadClut(texture,{});pair.gpu->LoadClut(texture,{});
    if(pair.gpu->stats().readbacks-before!=gpu::PageBytes)throw std::runtime_error("Palette readback copied unrelated pages");
    pair.compare("GPU partial readbacks preserve other dirty targets");
}
void checkFlatDepth(){
    for(uint32_t depth:{0x01000000u,0x0ffffff0u,0x7fffffffu,0xffff0000u,0xffffffffu})for(unsigned direction=0;direction<2u;++direction){
        Comparison pair(false);auto p=material(0u,false);p.state.prim.tme=false;p.state.prim.abe=false;p.state.prim.iip=false;
        auto& c=p.state.context;c.zbuf.zmask=false;c.test=0x30000u;
        p.vertices[0].x=3.25f;p.vertices[0].y=4.5f;p.vertices[1].x=57.75f;p.vertices[1].y=7.125f;p.vertices[2].x=8.5f;p.vertices[2].y=61.25f;
        for(auto& v:p.vertices)v.z=depth;if(direction)std::swap(p.vertices[1],p.vertices[2]);
        pair.draw(p);pair.compare("Constant depth CPU/GPU agreement");unsigned covered=0u;
        for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x)if(pair.cpu->ReadVram(0u,56u*32u,8u,x,y)){
            const auto actual=pair.cpu->ReadVram(48u,168u*32u,8u,x,y);
            if(actual!=depth)throw std::runtime_error("Flat triangle changed constant depth: expected "+std::to_string(depth)+", got "+std::to_string(actual));
            if(pair.gpu->ReadVram(48u,168u*32u,8u,x,y)!=depth)throw std::runtime_error("Flat GPU triangle changed constant depth");++covered;
        }
        if(!covered || pair.gpu->stats().gpuDraws!=1u)throw std::runtime_error("Flat triangle fixture did not render on GPU");
    }
}
void checkDepthPlanes(){
    for(uint32_t base:{0x01000000u,0x0ffffff0u,0x7fffffffu,0xf0000000u})for(unsigned direction=0;direction<2u;++direction){
        Comparison pair(false);auto p=material(0u,false);p.state.prim.tme=false;p.state.prim.abe=false;p.state.prim.iip=false;
        auto& c=p.state.context;c.zbuf.zmask=false;c.test=0x30000u;
        p.vertices[0].x=3.25f;p.vertices[0].y=4.5f;p.vertices[0].z=base;
        p.vertices[1].x=57.75f;p.vertices[1].y=7.125f;p.vertices[1].z=double(base)+3824.0;
        p.vertices[2].x=8.5f;p.vertices[2].y=61.25f;p.vertices[2].z=double(base)+7600.0;
        for(auto& v:p.vertices){v.r=71u;v.g=97u;v.b=131u;v.a=128u;}
        if(direction)std::swap(p.vertices[1],p.vertices[2]);
        pair.draw(p);pair.compare("Large depth plane agreement");
        auto back=p;std::swap(back.vertices[1],back.vertices[2]);back.state.context.test=0x70000u;
        for(auto& v:back.vertices){v.z-=2.0;v.r=199u;v.g=17u;v.b=31u;}
        pair.draw(back);pair.compare("Nearby depth plane occlusion");unsigned covered=0u;
        for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x)if(pair.cpu->ReadVram(0u,56u*32u,8u,x,y)){
            const auto expected=uint32_t(int64_t(base)+int64_t(x)*64+int64_t(y)*128-688);
            if(pair.cpu->ReadVram(48u,168u*32u,8u,x,y)!=expected || pair.gpu->ReadVram(48u,168u*32u,8u,x,y)!=expected)
                throw std::runtime_error("Large depth plane differs from its exact integer equation");
            if(pair.cpu->ReadVram(0u,56u*32u,8u,x,y)!=0x80836147u)throw std::runtime_error("A plane behind the character surface overwrote it");++covered;
        }
        if(!covered || pair.gpu->stats().gpuDraws!=2u)throw std::runtime_error("Depth plane fixture did not render on GPU");
    }
}

}
std::vector<std::vector<uint64_t>> gifStrips(unsigned count){
    std::vector<std::vector<uint64_t>> packets;
    for(unsigned k=0;k<count;++k){
        constexpr unsigned strip=12u;
        const uint64_t prim=4u|(1u<<3u)|(uint64_t(k&1u)<<4u);
        std::vector<uint64_t> packet{uint64_t(strip)|0x8000u|(1ull<<46u)|(prim<<47u)|(3ull<<60u),0x512u};
        const unsigned left=40u+(k*37u)%400u,top=30u+(k*53u)%380u;
        for(unsigned i=0;i<strip;++i){
            const unsigned x=left+(i/2u)*7u,y=top+(i&1u)*11u;
            packet.push_back(uint64_t((i*7u+k)&255u)|(uint64_t((i*13u+k*5u)&255u)<<32u));packet.push_back(uint64_t((k*29u)&255u)|(128ull<<32u));
            packet.push_back(uint64_t(0x3e000000u)|(uint64_t(0x3e800000u)<<32u));packet.push_back(0x3f800000u);
            packet.push_back(uint64_t(x*16u)|(uint64_t(y*16u)<<32u));packet.push_back(1000u+k*3u+i);
        }
        packets.push_back(std::move(packet));
    }
    return packets;
}
std::unique_ptr<GS> gifTarget(std::vector<uint8_t>& vram){
    auto gs=std::make_unique<GS>();
    gs->init(vram.data(),uint32_t(vram.size()));gs->setRasterBackend(std::make_unique<GpuRasterBackend>());
    gs->writeRegister(0x4cu,56u|(uint64_t(8u)<<16u));gs->writeRegister(0x4eu,168u|(1ull<<32u));
    gs->writeRegister(0x40u,(511ull<<16u)|(447ull<<48u));gs->writeRegister(0x47u,0x30000u);
    gs->writeRegister(0x06u,uint64_t(10240u)|(uint64_t(2u)<<14u)|(uint64_t(7u)<<26u)|(uint64_t(7u)<<30u)|(1ull<<34u));
    return gs;
}
void checkGsWorker(){
    const auto packets=gifStrips(600u);
    std::vector<uint8_t> direct(gpu::VramBytes,0x35u),threaded(gpu::VramBytes,0x35u);
    auto reference=gifTarget(direct),worker=gifTarget(threaded);worker->startWorker();
    for(GS* gs:{worker.get(),reference.get()})for(unsigned i=0;i<packets.size();++i){
        const auto& packet=packets[i];
        gs->processGIFPacket(reinterpret_cast<const uint8_t*>(packet.data()),uint32_t(packet.size()*8u));
        if(i%23u==0u){
            std::array<uint8_t,257> pixels{};
            for(unsigned j=0;j<pixels.size();++j)pixels[j]=uint8_t(i*17u+j*13u);
            const uint64_t psm=(i/23u)%2u ? 2u : 0u;
            gs->uploadImageNative((uint64_t(10240u)<<32u)|(uint64_t(2u)<<48u)|(psm<<56u),
                uint64_t(i%64u)<<32u,16ull|(4ull<<32u),0u,pixels.data(),uint32_t(pixels.size()));
            pixels.fill(0u);
        }
        if(i==300u)gs->writeRegister(0x47u,0x50000u);
    }
    worker->fence();
    unsigned differing=0,drawn=0;
    for(uint32_t y=0;y<448u;y+=3u)for(uint32_t x=0;x<512u;x+=3u){
        const auto expected=reference->ReadVram(0u,56u*32u,8u,x,y);
        differing+=worker->ReadVram(0u,56u*32u,8u,x,y)!=expected;drawn+=expected!=0x35353535u;
    }
    for(unsigned psm:{0u,2u})for(unsigned y=0;y<4u;++y)for(unsigned x=0;x<80u;++x)
        if(worker->ReadVram(psm,10240u,2u,x,y)!=reference->ReadVram(psm,10240u,2u,x,y))
            throw std::runtime_error("Queued native upload lost ordering, source ownership or its partial final pixel");
    if(!drawn)throw std::runtime_error("GS thread fixture drew nothing");
    if(differing)throw std::runtime_error("GS thread changed "+std::to_string(differing)+" sampled pixels");
    reference->setRasterBackend(std::make_unique<GSCpuBackend>());
    worker->setRasterBackend(std::make_unique<GSCpuBackend>());
    if(direct!=threaded)throw std::runtime_error("GS queued uploads and draws changed video-memory bytes");
}
void checkInternalResolution(){
    const auto saved=graphics_settings();auto scaled=saved;scaled.internalScale=2u;set_graphics_settings(scaled);
    struct Restore{GraphicsSettings value;~Restore(){set_graphics_settings(value);}} restore{saved};
    for(unsigned variant=0;variant<4u;++variant){
        Comparison pair(false);auto tri=material(0u,false);auto& s=tri.state;auto& c=s.context;
        s.prim.tme=s.prim.abe=s.prim.iip=false;c.scissor={0u,127u,0u,127u};
        if(variant&1u)c.frame.psm=GS_PSM_CT16;
        for(auto& v:tri.vertices){v.r=200;v.g=90;v.b=30;v.a=128;v.z=5000u;}
        tri.vertices[0].x=3.25f;tri.vertices[0].y=2.5f;tri.vertices[1].x=120.75f;tri.vertices[1].y=30.5f;tri.vertices[2].x=20.5f;tri.vertices[2].y=118.25f;
        if(variant&2u){c.zbuf.zmask=false;c.test=0x50000u;}
        pair.draw(tri);
        if(variant&2u){
            auto behind=tri;for(auto& v:behind.vertices){v.r=10;v.g=220;v.b=240;v.z=100u;}
            behind.vertices[0].x=60.25f;behind.vertices[0].y=10.5f;behind.vertices[1].x=126.5f;behind.vertices[1].y=120.25f;behind.vertices[2].x=8.75f;behind.vertices[2].y=100.5f;
            pair.draw(behind);
        }
        GSPresentationRequest request{};request.pmode=1;
        request.dispfb1=uint64_t(56)|(uint64_t(8)<<9)|(uint64_t(c.frame.psm)<<15);
        request.display1=(uint64_t(127)<<32)|(uint64_t(127)<<44);
        if(variant==3u)request.smode2=3;
        pair.gpu->Sync(GSSyncReason::Presentation);
        const auto hi=pair.gpu->Present(request),native=pair.cpu->Present(request);
        if(!native || hi.width!=native.width*2u || hi.height!=native.height*2u || hi.stride!=hi.width)
            throw std::runtime_error("Internal resolution did not present a 2x frame: "+std::to_string(variant));
        const unsigned stride=native.stride ? native.stride : 640u;
        const auto at=[&](const PresentationFrame& f,unsigned rowStride,unsigned x,unsigned y){uint32_t p;std::memcpy(&p,f.pixels.data()+(size_t(y)*rowStride+x)*4u,4u);return p&0xffffffu;};
        unsigned interior=0u,refined=0u;
        for(unsigned y=1u;y+1u<native.height;++y)for(unsigned x=1u;x+1u<native.width;++x){
            const uint32_t centre=at(native,stride,x,y);bool uniform=true;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)uniform=uniform && at(native,stride,x+dx,y+dy)==centre;
            for(unsigned sy=0;sy<2u;++sy)for(unsigned sx=0;sx<2u;++sx){
                const uint32_t value=at(hi,hi.width,x*2u+sx,y*2u+sy);
                if(uniform && value!=centre)throw std::runtime_error("Internal resolution changed a solid area at "+std::to_string(x)+","+std::to_string(y)+" variant "+std::to_string(variant)+" native "+std::to_string(centre)+" scaled "+std::to_string(value));
                if(!uniform && value!=centre)++refined;
            }
            interior+=uniform;
        }
        if(interior<4000u || refined<64u)throw std::runtime_error("Internal resolution did not refine edges: "+std::to_string(interior)+" solid, "+std::to_string(refined)+" refined");
        pair.compare("Internal resolution keeps native VRAM");
    }
    std::cout<<"GPU internal resolution checks passed: 2x color, 16-bit, depth-tested and interlaced presentation.\n";
}
void checkHdTextures(){
    const auto saved=graphics_settings();
    struct Restore{GraphicsSettings value;~Restore(){set_graphics_settings(value);hd::library().configure(hd::off,{},2u);}} restore{saved};
    const auto folder=std::filesystem::temp_directory_path()/"openkai2-hd-check";
    std::error_code ignored;std::filesystem::remove_all(folder,ignored);std::filesystem::create_directories(folder,ignored);
    const auto render=[&](unsigned mode,unsigned passes){
        auto scaled=saved;scaled.internalScale=2u;scaled.hdTextures=mode;scaled.textureFolder=folder;set_graphics_settings(scaled);
        Comparison pair(false);
        for(unsigned y=0;y<32u;++y)for(unsigned x=0;x<32u;++x)
            pair.write(0u,10240u,2u,x,y,0x80000000u|(((x/8u)*60u)&255u)|((((y/8u)*70u)&255u)<<8u)|(uint32_t(((x/8u+y/8u)*25u)&255u)<<16u));
        auto sprite=material(0u,true);auto& s=sprite.state;auto& c=s.context;
        s.prim.abe=s.prim.iip=false;s.prim.fst=true;s.linearFilter=false;s.textureWidth=s.textureHeight=32u;
        c.tex0.psm=GS_PSM_CT32;c.tex0.tfx=1u;c.tex0.tcc=1u;c.scissor={0u,127u,0u,127u};c.clamp=2u|(2u<<2u)|(uint64_t(4u)<<4u)|(uint64_t(27u)<<14u)|(uint64_t(2u)<<24u)|(uint64_t(29u)<<34u);
        sprite.vertices[0].x=0;sprite.vertices[0].y=0;sprite.vertices[1].x=64;sprite.vertices[1].y=64;
        sprite.vertices[0].u=sprite.vertices[0].v=0;sprite.vertices[1].u=sprite.vertices[1].v=32u*16u;
        PresentationFrame frame;
        GSPresentationRequest request{};request.pmode=1;
        request.dispfb1=uint64_t(56)|(uint64_t(8)<<9);request.display1=(uint64_t(127)<<32)|(uint64_t(127)<<44);
        for(unsigned pass=0;pass<passes;++pass){
            pair.draw(sprite);pair.gpu->Flush();hd::library().wait_idle();
            pair.gpu->Sync(GSSyncReason::Presentation);frame=pair.gpu->Present(request);
        }
        pair.compare("HD textures keep native VRAM");
        if(frame.width!=256u)throw std::runtime_error("HD texture fixture did not present at 2x");
        return frame;
    };
    const auto pixel=[](const PresentationFrame& f,unsigned x,unsigned y){uint32_t p;std::memcpy(&p,f.pixels.data()+(size_t(y)*f.width+x)*4u,4u);return p&0xffffffu;};
    const auto plain=render(hd::off,1u),upscaled=render(hd::upscaled,2u);
    if(plain.pixels!=upscaled.pixels)throw std::runtime_error("Upscaled blocky texture changed the picture");
    if(hd::library().counts().upscaled!=1u)throw std::runtime_error("Texture was not upscaled exactly once");
    render(hd::dump,2u);
    std::vector<std::filesystem::path> dumped;
    for(const auto& entry:std::filesystem::directory_iterator(folder/"dump"))dumped.push_back(entry.path());
    if(dumped.size()!=1u)throw std::runtime_error("Texture dump did not write one PNG");
    hd::Image image;if(!hd::load_image(dumped[0],image) || image.width!=32u || image.height!=32u)throw std::runtime_error("Dumped texture unreadable");
    hd::Image pack;pack.width=pack.height=128u;pack.texels.assign(128u*128u,0x80ff00ffu);
    if(!hd::save_image(folder/"pack"/dumped[0].filename(),pack))throw std::runtime_error("Could not write texture pack fixture");
    const auto packed=render(hd::packs,2u);
    if(pixel(packed,40u,40u)!=0xff00ffu || pixel(packed,120u,120u)!=0xff00ffu || pixel(packed,200u,200u)!=pixel(plain,200u,200u))
        throw std::runtime_error("Texture pack did not replace the texture");
    std::filesystem::remove_all(folder,ignored);
    std::cout<<"GPU HD texture checks passed: upscaled, dumped and texture pack replacement at 2x.\n";
}
int run_gpu_checks(bool required){
    try{
        gpu::verify_address_tables();checkFootprints();std::cout<<"GPU swizzle address references passed.\n";
        std::string error;
#ifdef _WIN32
        const char* wanted=std::getenv("OPENKAI2_RENDERER");
        try{set_renderer_preference(wanted && std::string(wanted)=="vulkan" ? "vulkan" : "gpu");initialize_window_gpu();}catch(const std::exception& e){error=e.what();}
        const bool initialized=bool(gpu::current_device());
#else
        const bool initialized=gpu::initialize_headless(error);
#endif
        if(!initialized){std::cout<<"GPU checks unavailable: "<<error<<'\n';return required ? 1 : 77;}
        std::cout<<"GPU checks device: "<<gpu::current_device()->description()<<'\n';
        checkDepthEnable();checkFlatDepth();checkDepthPlanes();checkPaletteReuse();checkPresentationReadbacks();checkEmptyTriangles();checkOffscreenTriangles();checkMaterials();checkIntegerPipeline();checkFormats();checkLines();checkCoherence();checkTransferPages();checkGpuTransfers();checkLargeTransfers();checkRebind();checkClears();checkSourceChanges();checkPartialReadbacks();checkGsWorker();checkInternalResolution();checkHdTextures();
        const auto before=gpu_render_statistics().gpuDraws;verify_vector_runtime_gpu();
        if(gpu_render_statistics().gpuDraws-before!=8u)throw std::runtime_error("VIF/PATH1 integration did not execute eight GPU triangles");
        auto device=gpu::current_device();device->post([](gpu::Host&){throw std::runtime_error("GPU failure propagation fixture");});
        try{device->invoke([](gpu::Host&){});}catch(const std::exception&){}
        bool propagated=false;
        try{device->invoke([](gpu::Host&){});}catch(const std::exception& e){propagated=std::string(e.what())=="GPU failure propagation fixture";}
        if(!propagated)throw std::runtime_error("GPU asynchronous failure did not propagate");
        gpu::shutdown_device();
        std::cout<<"GPU renderer comparisons passed: 10 constant-depth and 8 overlapping depth-plane cases, 256 material states, 104 format states, 65536 integer pipeline draws, 576 point/line draws, 128 source-state draws, 19 presentation readback cases, selective readbacks, writebacks, uploads, target transitions, feedback fallback, VRAM replacement, eight VIF/PATH1 GPU triangles, 600 GIF strips through the GS thread and asynchronous failure propagation.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"GPU regression failed: "<<e.what()<<'\n';gpu::shutdown_device();return 1;}
}
int benchmark_gpu_footprints(unsigned iterations){
    try{
        if(!iterations)throw std::runtime_error("Iteration count must be positive");
        checkFootprints();
        for(unsigned size:{8u,32u,128u,512u})for(unsigned psm:{0u,2u,19u,20u}){
            const auto start=std::chrono::steady_clock::now();uint64_t hash=0u;
            for(unsigned i=0;i<iterations;++i){auto pages=gpu::rectangle_pages(psm,10240u,8u,i%31u,i%17u,size,size);hash=hash*33u+pages.count();}
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"size="<<size<<" psm="<<psm<<" ns_per_call="<<double(ns)/iterations<<" hash="<<std::hex<<hash<<std::dec<<'\n';
        }return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
int benchmark_gpu_submission(unsigned draws){
    try{
        if(!draws)throw std::runtime_error("Draw count must be positive");
        std::string error;
#ifdef _WIN32
        set_renderer_preference("gpu");initialize_window_gpu();
#else
        if(!gpu::initialize_headless(error))throw std::runtime_error(error);
#endif
        for(unsigned mode:{0u,16u,64u,96u}){
            Comparison pair;auto p=material(mode,false);
            p.vertices[0].x=p.vertices[0].y=0.0f;p.vertices[1].x=1.5f;p.vertices[1].y=0.0f;p.vertices[2].x=0.0f;p.vertices[2].y=1.5f;
            pair.cpu->LoadClut(p.state.context.tex0,{});pair.gpu->LoadClut(p.state.context.tex0,{});
            pair.draw(p);pair.compare("GPU submission benchmark warmup");
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<draws;++i)pair.gpu->Submit(p);
            pair.gpu->Sync(GSSyncReason::Finish);
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            for(unsigned i=0;i<draws;++i)pair.cpu->Submit(p);
            pair.compare("GPU submission benchmark output");
            uint64_t hash=14695981039346656037ull;for(auto byte:pair.expected)hash=(hash^byte)*1099511628211ull;
            std::cout<<"mode="<<mode<<" ns_per_draw="<<double(ns)/draws<<" hash="<<std::hex<<hash<<std::dec<<'\n';
        }
        gpu::shutdown_device();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';gpu::shutdown_device();return 1;}
}
int benchmark_sprite_submission(unsigned frames){
    try{
        if(!frames)throw std::runtime_error("Frame count must be positive");
        std::string error;
#ifdef _WIN32
        set_renderer_preference("gpu");initialize_window_gpu();
#else
        if(!gpu::initialize_headless(error))throw std::runtime_error(error);
#endif
        std::vector<uint8_t> vram(gpu::VramBytes,0x35u);auto gs=std::make_unique<GS>();
        gs->init(vram.data(),uint32_t(vram.size()));gs->setRasterBackend(std::make_unique<GpuRasterBackend>());
        gs->writeRegister(0x4cu,56u|(uint64_t(8u)<<16u));gs->writeRegister(0x4eu,168u|(1ull<<32u));
        gs->writeRegister(0x40u,(511ull<<16u)|(447ull<<48u));gs->writeRegister(0x47u,0x50000u);
        gs->writeRegister(0x06u,uint64_t(10240u)|(uint64_t(2u)<<14u)|(uint64_t(5u)<<26u)|(uint64_t(5u)<<30u)|(1ull<<34u));
        gs->writeRegister(0x42u,uint64_t(0u)|(uint64_t(1u)<<2u)|(uint64_t(0u)<<4u)|(uint64_t(1u)<<6u));
        const char* sizeText=std::getenv("OPENKAI2_SPRITE_SIZE");const unsigned base=sizeText ? unsigned(std::stoul(sizeText)) : 4u;
        const char* countText=std::getenv("OPENKAI2_SPRITE_COUNT");const unsigned sprites=countText ? unsigned(std::stoul(countText)) : 1500u;
        std::vector<uint64_t> packet;
        const uint64_t prim=6u|(1u<<4u)|(1u<<6u)|(1u<<8u);
        packet.push_back(uint64_t(sprites*2u)|0x8000u|(1ull<<46u)|(prim<<47u)|(3ull<<60u));packet.push_back(0x531u);
        uint32_t seed=12345u;
        auto next=[&]{seed=seed*1664525u+1013904223u;return seed>>8u;};
        for(unsigned i=0;i<sprites;++i){
            const unsigned size=base+next()%(base+4u),x=next()%std::max(1u,640u-size),y=next()%std::max(1u,448u-size);
            for(unsigned corner=0;corner<2u;++corner){
                packet.push_back(uint64_t(255u)|(uint64_t(255u)<<32u));packet.push_back(uint64_t(255u)|(uint64_t(96u)<<32u));
                packet.push_back(uint64_t(corner*31u*16u)|(uint64_t(corner*31u*16u)<<32u));packet.push_back(0u);
                const unsigned px=x+corner*size,py=y+corner*size;
                packet.push_back(uint64_t(px*16u+2048u*16u-256u*16u)|(uint64_t(py*16u+2048u*16u-224u*16u)<<32u));packet.push_back(uint64_t(1000u)<<4u);
            }
        }
        const auto* bytes=reinterpret_cast<const uint8_t*>(packet.data());const auto size=uint32_t(packet.size()*8u);
        gs->processGIFPacket(bytes,size);
        const auto before=gpu_render_statistics().gpuDraws;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<frames;++i){gs->processGIFPacket(bytes,size);gs->writeRegister(0x61u,0u);}
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        const auto draws=gpu_render_statistics().gpuDraws-before;
        std::cout<<"sprite frames="<<frames<<" sprites="<<draws<<" ns_per_sprite="<<double(ns)/double(std::max<uint64_t>(draws,1u))
                 <<" ms_per_frame="<<double(ns)/1e6/frames<<'\n';
        gs.reset();gpu::shutdown_device();return draws ? 0 : 1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';gpu::shutdown_device();return 1;}
}
int benchmark_gs_replay(const char* path,unsigned passes){
    try{
        std::ifstream in(path,std::ios::binary);
        if(!in)throw std::runtime_error("Cannot open the GS recording");
        const std::vector<uint8_t> stream((std::istreambuf_iterator<char>(in)),{});
        std::string error;
#ifdef _WIN32
        set_renderer_preference("gpu");initialize_window_gpu();
#else
        if(!gpu::initialize_headless(error))throw std::runtime_error(error);
#endif
        std::vector<uint8_t> vram(gpu::VramBytes,0x35u);auto gs=std::make_unique<GS>();
        gs->init(vram.data(),uint32_t(vram.size()));gs->setRasterBackend(std::make_unique<GpuRasterBackend>());
        uint64_t packets=0,uploads=0;
        const auto play=[&]{
            for(size_t offset=0;offset+16u<=stream.size();){
                uint32_t size=0,kind=0;std::memcpy(&size,stream.data()+offset,4);std::memcpy(&kind,stream.data()+offset+4,4);
                if(offset+16u+size>stream.size())break;
                const uint8_t* payload=stream.data()+offset+16u;
                if(kind==1u && size>=32u){uint64_t r[4];std::memcpy(r,payload,32);gs->uploadImageNative(r[0],r[1],r[2],r[3],payload+32,size-32u);++uploads;}
                else{gs->processGIFPacket(payload,size);++packets;}
                offset+=16u+((size+15u)&~15u);
            }
        };
        play();
        const auto before=gpu_render_statistics();packets=uploads=0;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<passes;++i)play();
        gs->writeRegister(0x61u,0u);
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        const auto after=gpu_render_statistics();
        std::cout<<"replay bytes="<<stream.size()<<" packets="<<packets<<" uploads="<<uploads<<" gpu_draws="<<after.gpuDraws-before.gpuDraws
                 <<" cpu_draws="<<after.cpuDraws-before.cpuDraws<<" culled="<<after.culled-before.culled<<" batches="<<after.batches-before.batches
                 <<" ms="<<double(ns)/1e6<<" ns_per_draw="<<double(ns)/double(std::max<uint64_t>(after.gpuDraws-before.gpuDraws+after.culled-before.culled,1u))<<'\n';
        gs.reset();gpu::shutdown_device();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';gpu::shutdown_device();return 1;}
}
int benchmark_gif_submission(unsigned packets){
    try{
        if(!packets)throw std::runtime_error("Packet count must be positive");
        std::string error;
#ifdef _WIN32
        set_renderer_preference("gpu");initialize_window_gpu();
#else
        if(!gpu::initialize_headless(error))throw std::runtime_error(error);
#endif
        std::vector<uint8_t> vram(gpu::VramBytes,0x35u);auto gs=std::make_unique<GS>();
        gs->init(vram.data(),uint32_t(vram.size()));gs->setRasterBackend(std::make_unique<GpuRasterBackend>());
        gs->writeRegister(0x4cu,56u|(uint64_t(8u)<<16u));gs->writeRegister(0x4eu,168u|(1ull<<32u));
        gs->writeRegister(0x40u,(511ull<<16u)|(447ull<<48u));gs->writeRegister(0x47u,0x50000u);
        gs->writeRegister(0x06u,uint64_t(10240u)|(uint64_t(2u)<<14u)|(uint64_t(7u)<<26u)|(uint64_t(7u)<<30u)|(1ull<<34u));
        constexpr unsigned strip=24u;
        std::vector<uint64_t> packet;
        const uint64_t prim=4u|(1u<<3u)|(1u<<4u);
        packet.push_back(uint64_t(strip)|0x8000u|(1ull<<46u)|(prim<<47u)|(3ull<<60u));packet.push_back(0x512u);
        for(unsigned i=0;i<strip;++i){
            const unsigned x=200u+(i/2u)*6u,y=200u+(i&1u)*9u;
            packet.push_back(uint64_t(i*7u&255u)|(uint64_t(i*13u&255u)<<32u));packet.push_back(uint64_t(i*29u&255u)|(128ull<<32u));
            packet.push_back(uint64_t(0x3e000000u)|(uint64_t(0x3e800000u)<<32u));packet.push_back(0x3f800000u);
            packet.push_back(uint64_t(x*16u+2048u*16u-256u*16u)|(uint64_t(y*16u+2048u*16u-224u*16u)<<32u));packet.push_back(1000u+i);
        }
        const auto* bytes=reinterpret_cast<const uint8_t*>(packet.data());const auto size=uint32_t(packet.size()*8u);
        for(unsigned i=0;i<64u;++i)gs->processGIFPacket(bytes,size);
        const auto before=gpu_render_statistics().gpuDraws;
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<packets;++i)gs->processGIFPacket(bytes,size);
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        const auto draws=gpu_render_statistics().gpuDraws-before;
        std::cout<<"gif packets="<<packets<<" triangles="<<draws<<" ns_per_triangle="<<double(ns)/double(std::max<uint64_t>(draws,1u))<<'\n';
        gs.reset();gpu::shutdown_device();return draws ? 0 : 1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';gpu::shutdown_device();return 1;}
}
int benchmark_gpu_renderer(unsigned draws){
    try{
        if(!draws)throw std::runtime_error("Draw count must be positive");
        std::string error;
#ifdef _WIN32
        set_renderer_preference("gpu");initialize_window_gpu();
#else
        if(!gpu::initialize_headless(error))throw std::runtime_error(error);
#endif
        std::cout<<"GPU benchmark device: "<<gpu::current_device()->description()<<'\n';
        for(unsigned mode:{0u,1u,16u,64u,65u,96u})for(unsigned pass=0;pass<3u;++pass){
            Comparison pair;auto batch=material(mode,false);auto& c=batch.state.context;
            pair.cpu->LoadClut(c.tex0,{});pair.gpu->LoadClut(c.tex0,{});
            pair.draw(batch);pair.compare("GPU benchmark warmup");
            for(unsigned phase=0;phase<2u;++phase){
                const bool useGpu=(phase^unsigned(pass&1u))!=0u;
                auto* renderer=useGpu ? static_cast<GSRasterBackend*>(pair.gpu.get()) : static_cast<GSRasterBackend*>(pair.cpu.get());
                const auto start=std::chrono::steady_clock::now();
                for(unsigned i=0;i<draws;++i)renderer->Submit(batch);
                renderer->Sync(GSSyncReason::Finish);
                std::vector<uint8_t> snapshot;renderer->SnapshotVram(snapshot);
                const auto nanoseconds=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                uint64_t hash=14695981039346656037ull;for(auto byte:snapshot)hash=(hash^byte)*1099511628211ull;
                std::cout<<"mode="<<mode<<" pass="<<pass<<" backend="<<(useGpu ? "gpu" : "cpu")<<" ns_per_draw="<<double(nanoseconds)/draws<<" hash="<<std::hex<<hash<<std::dec<<'\n';
            }
            pair.compare("GPU benchmark rendered output");
        }
        gpu::shutdown_device();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';gpu::shutdown_device();return 1;}
}

}
