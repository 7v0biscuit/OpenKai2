// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_checks.h"
#include "gpu_backend.h"
#include "gpu_device.h"
#include "gpu_layout.h"
#include "runtime_layout.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
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
        // CPU writes, transfer uploads and GPU draws may all edit a palette.
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
        // The explicit 640x448 default-size case spans the depth page as
        // color; every other fixture leaves it outside the presented surfaces.
        const auto depth=pair.gpu->ReadVram(GS_PSM_Z32,168u*32u,8u,4u,4u);
        if(depth!=pair.cpu->ReadVram(GS_PSM_Z32,168u*32u,8u,4u,4u) || pair.gpu->stats().readbacks-before!=copied+(variant==18u ? 0u : gpu::PageBytes))
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
            pair.draw(p);pair.draw(p); // Read the format-truncated destination on the next draw.
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
// Copies between render targets and textures move only their own pages, while
// draws issued before the copy still observe the previous texture contents.
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
    // Release the previous host allocation while its draw remains queued.
    // Rebinding must never read back through the released pointer.
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
    if(pair.gpu->stats().readbacks-before!=gpu::PageBytes)throw std::runtime_error("Partial image upload copied unrelated pages");
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
            // Vertex depths lie on z=base+64*(x-3.25)+128*(y-4.5).
            // At pixel centres its exact value is an integer, independent of
            // barycentric evaluation or triangle winding.
            const auto expected=uint32_t(int64_t(base)+int64_t(x)*64+int64_t(y)*128-688);
            if(pair.cpu->ReadVram(48u,168u*32u,8u,x,y)!=expected || pair.gpu->ReadVram(48u,168u*32u,8u,x,y)!=expected)
                throw std::runtime_error("Large depth plane differs from its exact integer equation");
            if(pair.cpu->ReadVram(0u,56u*32u,8u,x,y)!=0x80836147u)throw std::runtime_error("A plane behind the character surface overwrote it");++covered;
        }
        if(!covered || pair.gpu->stats().gpuDraws!=2u)throw std::runtime_error("Depth plane fixture did not render on GPU");
    }
}

}
int run_gpu_checks(bool required){
    try{
        gpu::verify_address_tables();checkFootprints();std::cout<<"GPU swizzle address references passed.\n";
        std::string error;
#ifdef _WIN32
        try{set_renderer_preference("gpu");initialize_window_gpu();}catch(const std::exception& e){error=e.what();}
        const bool initialized=bool(gpu::current_device());
#else
        const bool initialized=gpu::initialize_headless(error);
#endif
        if(!initialized){std::cout<<"GPU checks unavailable: "<<error<<'\n';return required ? 1 : 77;}
        checkFlatDepth();checkDepthPlanes();checkPaletteReuse();checkPresentationReadbacks();checkEmptyTriangles();checkMaterials();checkIntegerPipeline();checkFormats();checkLines();checkCoherence();checkTransferPages();checkRebind();checkClears();checkSourceChanges();checkPartialReadbacks();
        const auto before=gpu_render_statistics().gpuDraws;verify_vector_runtime_gpu();
        if(gpu_render_statistics().gpuDraws-before!=8u)throw std::runtime_error("VIF/PATH1 integration did not execute eight GPU triangles");
        // A failed asynchronous submission must propagate to its caller and
        // still allow context teardown, rather than leaving a waiting future.
        auto device=gpu::current_device();device->post([](gpu::Api&){throw std::runtime_error("GPU failure propagation fixture");});
        try{device->invoke([](gpu::Api&){});}catch(const std::exception&){}
        bool propagated=false;
        try{device->invoke([](gpu::Api&){});}catch(const std::exception& e){propagated=std::string(e.what())=="GPU failure propagation fixture";}
        if(!propagated)throw std::runtime_error("GPU asynchronous failure did not propagate");
        gpu::shutdown_device();
        std::cout<<"GPU renderer comparisons passed: 10 constant-depth and 8 overlapping depth-plane cases, 256 material states, 104 format states, 65536 integer pipeline draws, 576 point/line draws, 128 source-state draws, 19 presentation readback cases, selective readbacks, writebacks, uploads, target transitions, feedback fallback, VRAM replacement, eight VIF/PATH1 GPU triangles and asynchronous failure propagation.\n";return 0;
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
