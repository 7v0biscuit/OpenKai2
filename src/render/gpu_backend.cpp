// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_backend.h"
#include "gpu_device.h"
#include "gpu_layout.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "openkai2_gpu_shader.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace openkai2 {
namespace gpu {
std::bitset<PageCount> rectangle_pages(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y,unsigned w,unsigned h){
    std::bitset<PageCount> pages;const auto& lut=gpu::address_tables();if(!lut[(psm&63u)*8u] || !w || !h)return pages;
    if(uint64_t(w)*h>4u*1024u*1024u){pages.set();return pages;}
    // Aligned bases place every logical format page entirely inside one
    // physical 8 KiB page. Its swizzle cannot change the physical page ID.
    if((base&31u)==0u){
        const unsigned pw=lut[(psm&63u)*8u],ph=lut[(psm&63u)*8u+1u],stride=(width*64u)/pw;
        for(unsigned row=y/ph;row<=(y+h-1u)/ph;++row)
            for(unsigned col=x/pw;col<=(x+w-1u)/pw;++col)pages.set(((base>>5u)+row*stride+col)&(PageCount-1u));
        return pages;
    }
    for(unsigned row=y;row<y+h;){
        for(unsigned col=x;col<x+w;){pages.set(gpu::bit_address(psm,base,width,col,row)/(gpu::PageBytes*8u));col=std::min(x+w,(col|7u)+1u);}
        row=std::min(y+h,(row|7u)+1u);
    }
    return pages;
}
}
namespace {
using Pages=std::bitset<gpu::PageCount>;
std::atomic<uint64_t> totalGpuDraws{},totalCpuDraws{},totalBatches{},totalTiles{},totalUploads{},totalReadbacks{},totalCulled{};
bool indexed(unsigned psm){return psm==19u || psm==20u || psm==27u || psm==36u || psm==44u;}
bool fourBit(unsigned psm){return psm==20u || psm==36u || psm==44u;}
uint32_t bits(float value){uint32_t result;std::memcpy(&result,&value,4u);return result;}
uint32_t rgba(const GSVertex& v){return uint32_t(v.r)|(uint32_t(v.g)<<8u)|(uint32_t(v.b)<<16u)|(uint32_t(v.a)<<24u);}
Pages rectanglePages(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y,unsigned w,unsigned h){
    return gpu::rectangle_pages(psm,base,width,x,y,w,h);
}
// Presentation consumes color surfaces only. Retain depth and unrelated render
// targets on the GPU until a CPU consumer explicitly requests their pages.
Pages presentationPages(const GSPresentationRequest& request){
    Pages pages;
    const bool halfHeight=(request.smode2&3u)==3u;
    const auto add=[&](const GSFrameReg& frame,unsigned x,unsigned y,unsigned w,unsigned h,bool blocks){
        if(!gpu::address_tables()[(frame.psm&63u)*8u]){pages.set();return;}
        pages|=rectanglePages(frame.psm,blocks ? frame.fbp : frame.fbp*32u,
                             frame.fbw ? frame.fbw : 10u,x,y,w,h);
    };
    for(unsigned circuit=0;circuit<2u;++circuit){
        if(!(request.pmode&(1ull<<circuit)))continue;
        const uint64_t buffer=circuit ? request.dispfb2 : request.dispfb1;
        const uint64_t display=circuit ? request.display2 : request.display1;
        GSFrameReg frame{};frame.fbp=buffer&511u;frame.fbw=(buffer>>9u)&63u;frame.psm=(buffer>>15u)&31u;
        unsigned dw=(display>>32u)&4095u,dh=(display>>44u)&2047u,magnification=(display>>23u)&15u;
        if(!frame.fbw && !dw && !dh && !magnification)continue;
        unsigned w=(dw+1u)/(magnification+1u),h=dh+1u;
        if(w<64u || h<64u){w=640u;h=448u;}
        w=std::min(w,640u);h=std::min(h,512u);
        add(frame,(buffer>>32u)&2047u,(buffer>>43u)&2047u,w,halfHeight ? (h+1u)/2u : h,false);
        if(!halfHeight){
            if(request.hasPreferredSource && request.preferredDestFbp==frame.fbp)
                add(request.preferredSource,0u,0u,w,h,true);
            // The CPU presenter may inspect either context when display zero
            // is black. Include both candidates before running that selection.
            if(!frame.fbp)for(const auto& candidate:request.contextFrames)add(candidate,0u,0u,w,h,false);
        }
    }
    return pages;
}
uint32_t color16(uint32_t p){return ((p&31u)<<3u)|(((p>>5u)&31u)<<11u)|(((p>>10u)&31u)<<19u)|(((p>>15u)&1u)<<31u);}
uint32_t textureAlpha(const GSTexaReg& t,unsigned psm,uint32_t p){
    const bool zero=(p&0xffffffu)==0u;unsigned a=p>>24u;
    if(psm==1u)a=t.aem && zero ? 0u : t.ta0;
    if(psm==2u || psm==10u)a=(a&128u) ? t.ta1 : t.aem && zero ? 0u : t.ta0;
    return (p&0xffffffu)|(a<<24u);
}
struct Resources {std::array<gpu::UInt,6> buffers{};gpu::UInt program=0u;};
void compileResources(gpu::Api& gl,Resources& r){
    const auto shader=gl.CreateShader(gpu::ComputeShader);const char* source=gpu::shaderSource;
    gl.ShaderSource(shader,1,&source,nullptr);gl.CompileShader(shader);int ok=0,length=0;gl.GetShaderiv(shader,0x8b81,&ok);
    if(!ok){gl.GetShaderiv(shader,0x8b84,&length);std::vector<char> log(std::max(length,1));gl.GetShaderInfoLog(shader,int(log.size()),nullptr,log.data());gl.DeleteShader(shader);throw std::runtime_error("GPU shader: "+std::string(log.data()));}
    r.program=gl.CreateProgram();gl.AttachShader(r.program,shader);gl.LinkProgram(r.program);gl.DeleteShader(shader);gl.GetProgramiv(r.program,0x8b82,&ok);
    if(!ok){gl.GetProgramiv(r.program,0x8b84,&length);std::vector<char> log(std::max(length,1));gl.GetProgramInfoLog(r.program,int(log.size()),nullptr,log.data());gl.DeleteProgram(r.program);r.program=0u;throw std::runtime_error("GPU program: "+std::string(log.data()));}
    gl.GenBuffers(int(r.buffers.size()),r.buffers.data());
    gl.BindBuffer(gpu::ShaderStorageBuffer,r.buffers[0]);gl.BufferData(gpu::ShaderStorageBuffer,gpu::VramBytes,nullptr,gpu::DynamicDraw);
    const auto& lut=gpu::address_tables();gl.BindBuffer(gpu::ShaderStorageBuffer,r.buffers[5]);gl.BufferData(gpu::ShaderStorageBuffer,gpu::Size(lut.size()*4u),lut.data(),gpu::DynamicDraw);
    gl.check("GPU resource creation");
}
}
struct GpuRasterBackend::Impl {
    mutable std::recursive_mutex mutex;GSCpuBackend cpu;uint8_t* vram=nullptr;uint32_t size=0;
    std::shared_ptr<gpu::Device> device;std::shared_ptr<Resources> resources;
    Pages cpuDirty,gpuDirty,batchWrites,batchSources;bool disabled=false,cpuFrame=false;
    GSTransferCommand transfer{};GpuRenderStats statistics{};
    std::array<uint16_t,512> clut{};std::array<uint32_t,2> cbp{};uint32_t clutGeneration=0;
    std::vector<uint32_t> commands,palette,activeTiles;
    std::array<std::vector<uint32_t>,65536> tileCommands;
    std::map<std::array<uint32_t,3>,uint32_t> paletteOffsets;
    std::array<uint32_t,5> target{},sourceKey{};Pages sourcePages;bool sourceValid=false;size_t references=0;
    bool ready(){
        if(disabled || size!=gpu::VramBytes)return false;
        if(resources)return true;
        device=gpu::current_device();if(!device)return false;
        try{resources=std::make_shared<Resources>();device->invoke([r=resources](gpu::Api& gl){compileResources(gl,*r);});cpuDirty.set();return true;}
        catch(const std::exception& e){resources.reset();disabled=true;if(renderer_preference()=="gpu")throw;std::cerr<<"[renderer] GPU initialization failed: "<<e.what()<<"\n";return false;}
    }
    ~Impl(){
        try{if(device && device->running()){submit();device->invoke([r=resources](gpu::Api& gl){if(r){gl.DeleteBuffers(int(r->buffers.size()),r->buffers.data());if(r->program)gl.DeleteProgram(r->program);}});}}catch(...){}

    }
    void upload(){
        if(!cpuDirty.any())return;
        struct Segment{unsigned offset;std::vector<uint8_t> bytes;};std::vector<Segment> segments;
        for(unsigned page=0;page<gpu::PageCount;){
            if(!cpuDirty[page]){++page;continue;}const unsigned first=page;while(page<gpu::PageCount && cpuDirty[page])++page;
            Segment s{first*gpu::PageBytes,{}};s.bytes.assign(vram+s.offset,vram+page*gpu::PageBytes);statistics.uploads+=s.bytes.size();totalUploads.fetch_add(s.bytes.size(),std::memory_order_relaxed);segments.push_back(std::move(s));
        }
        device->post([r=resources,segments=std::move(segments)](gpu::Api& gl){
            gl.MemoryBarrier(gpu::ShaderStorageBarrier|gpu::BufferUpdateBarrier);gl.BindBuffer(gpu::ShaderStorageBuffer,r->buffers[0]);
            for(const auto& s:segments)gl.BufferSubData(gpu::ShaderStorageBuffer,s.offset,gpu::Size(s.bytes.size()),s.bytes.data());gl.check("GPU VRAM upload");
        });cpuDirty.reset();
    }
    void submit(){
        if(commands.empty())return;
        std::vector<uint32_t> bins,refs;bins.reserve(activeTiles.size()*4u);refs.reserve(references);
        for(auto id:activeTiles){auto& list=tileCommands[id];bins.insert(bins.end(),{id&255u,id>>8u,uint32_t(refs.size()),uint32_t(list.size())});refs.insert(refs.end(),list.begin(),list.end());list.clear();}
        if(palette.empty())palette.push_back(0u);
        statistics.tiles+=activeTiles.size();++statistics.batches;totalTiles.fetch_add(activeTiles.size(),std::memory_order_relaxed);totalBatches.fetch_add(1u,std::memory_order_relaxed);
        device->post([r=resources,cmd=std::move(commands),bins=std::move(bins),refs=std::move(refs),colors=std::move(palette)](gpu::Api& gl){
            const std::array<const std::vector<uint32_t>*,4> arrays={&cmd,&bins,&refs,&colors};
            for(unsigned i=0;i<4u;++i){gl.BindBuffer(gpu::ShaderStorageBuffer,r->buffers[i+1u]);gl.BufferData(gpu::ShaderStorageBuffer,gpu::Size(arrays[i]->size()*4u),arrays[i]->data(),gpu::DynamicDraw);}
            for(unsigned i=0;i<6u;++i)gl.BindBufferBase(gpu::ShaderStorageBuffer,i,r->buffers[i]);
            gl.UseProgram(r->program);gl.DispatchCompute(unsigned(bins.size()/4u),1u,1u);gl.MemoryBarrier(gpu::ShaderStorageBarrier|gpu::BufferUpdateBarrier);gl.check("GPU raster dispatch");
        });commands.clear();palette.clear();activeTiles.clear();paletteOffsets.clear();references=0u;batchWrites.reset();batchSources.reset();
    }
    void materialize(const Pages& requested){
        const auto pages=gpuDirty&requested;
        if(!resources || !pages.any())return;submit();
        statistics.readbacks+=pages.count()*gpu::PageBytes;totalReadbacks.fetch_add(pages.count()*gpu::PageBytes,std::memory_order_relaxed);
        device->invoke([r=resources,pages,ram=vram](gpu::Api& gl){
            gl.MemoryBarrier(gpu::ShaderStorageBarrier|gpu::BufferUpdateBarrier);gl.BindBuffer(gpu::ShaderStorageBuffer,r->buffers[0]);
            for(unsigned p=0;p<gpu::PageCount;){if(!pages[p]){++p;continue;}const unsigned start=p;while(p<gpu::PageCount && pages[p])++p;const unsigned offset=start*gpu::PageBytes;gl.GetBufferSubData(gpu::ShaderStorageBuffer,offset,(p-start)*gpu::PageBytes,ram+offset);}
            gl.check("GPU VRAM readback");
        });gpuDirty&=~pages;
    }
    void materialize(){Pages all;all.set();materialize(all);}
    uint32_t paletteFor(const GSDrawState& state){
        const auto& t=state.context.tex0;
        const std::array<uint32_t,3> key={clutGeneration,uint32_t(t.psm)|(uint32_t(t.cpsm)<<8u)|(uint32_t(t.csm)<<16u)|(uint32_t(t.csa)<<24u),uint32_t(state.texa.ta0)|(uint32_t(state.texa.ta1)<<8u)|(uint32_t(state.texa.aem)<<16u)};
        if(auto i=paletteOffsets.find(key);i!=paletteOffsets.end())return i->second;
        const auto start=uint32_t(palette.size());paletteOffsets.emplace(key,start);
        const bool sixteen=t.cpsm==2u || t.cpsm==10u;const unsigned base=(unsigned(t.csa)&(sixteen ? 31u : 15u))*16u;
        for(unsigned i=0;i<256u;++i){
            const unsigned source=fourBit(t.psm) ? i&15u : i;unsigned at=(base+source)&(sixteen ? 511u : 255u);
            if(!sixteen && t.csm==0u && !fourBit(t.psm))at=std::min((source&240u)+base,240u)+(source&15u);
            uint32_t raw=sixteen ? color16(clut[at]) : uint32_t(clut[at])|(uint32_t(clut[at+256u])<<16u);
            if(t.cpsm==1u)raw&=0xffffffu;palette.push_back(textureAlpha(state.texa,t.cpsm,raw));
        }return start;
    }
    bool encode(const GSPrimitiveBatch& batch,gpu::Command& cmd,Pages& writes,Pages& sources){
        const auto& s=batch.state;const auto& c=s.context;const auto& t=c.tex0;
        const bool sprite=s.prim.type==GS_PRIM_SPRITE,point=s.prim.type==GS_PRIM_POINT;
        const bool line=s.prim.type==GS_PRIM_LINE || s.prim.type==GS_PRIM_LINESTRIP;
        const bool triangle=s.prim.type==GS_PRIM_TRIANGLE || s.prim.type==GS_PRIM_TRISTRIP || s.prim.type==GS_PRIM_TRIFAN;
        const bool textured=s.prim.tme && (sprite || triangle);
        const bool flatDepth=triangle && batch.vertices[0].z==batch.vertices[1].z && batch.vertices[1].z==batch.vertices[2].z;
        if((!sprite && !point && !line && !triangle)
            || batch.vertexCount!=(point ? 1u : triangle ? 3u : 2u) || (c.frame.psm!=0u && c.frame.psm!=1u && c.frame.psm!=2u && c.frame.psm!=10u)
            || (c.zbuf.psm!=0u && c.zbuf.psm!=1u && c.zbuf.psm!=2u && c.zbuf.psm!=10u && c.zbuf.psm!=48u && c.zbuf.psm!=49u && c.zbuf.psm!=50u && c.zbuf.psm!=58u) || s.scanmsk!=0u)return false;
        if(textured && (!gpu::address_tables()[(t.psm&63u)*8u] || !s.textureWidth || !s.textureHeight || s.textureWidth>1024u || s.textureHeight>1024u))return false;
        if(textured && indexed(t.psm) && t.cpsm!=0u && t.cpsm!=1u && t.cpsm!=2u && t.cpsm!=10u)return false;
        float minX=0,maxX=0,minY=0,maxY=0;const unsigned width=std::max<unsigned>(c.frame.fbw,1u);
        for(unsigned i=0;i<batch.vertexCount;++i){const auto& v=batch.vertices[i];
            if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.s) || !std::isfinite(v.t) || !std::isfinite(v.q) || std::abs(v.x)>65536.0f || std::abs(v.y)>65536.0f || v.z<0.0 || v.z>(triangle && !flatDepth ? 4294901760.0 : 4294967295.0))return false;
            const float x=(point || line ? float(int(v.x)) : v.x)-(sprite ? float(c.xyoffset.ofx)/16.0f : float(c.xyoffset.ofx>>4u));
            const float y=(point || line ? float(int(v.y)) : v.y)-(sprite ? float(c.xyoffset.ofy)/16.0f : float(c.xyoffset.ofy>>4u));
            if(!i){minX=maxX=x;minY=maxY=y;}else{minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);}
            const unsigned at=gpu::VertexWord+i*gpu::VertexWords;cmd[at]=bits(x);cmd[at+1u]=bits(y);std::memcpy(&cmd[at+2u],&v.z,8u);
            cmd[at+4u]=rgba(v);cmd[at+5u]=v.fog;cmd[at+6u]=bits(v.s);cmd[at+7u]=bits(v.t);cmd[at+8u]=bits(v.q);cmd[at+9u]=v.u;cmd[at+10u]=v.v;
        }
        // Repeated strip vertices and axis-flat triangles have no covered
        // samples. Reject them before page footprints and GPU tile binning.
        if(triangle){
            const auto same=[](const GSVertex& a,const GSVertex& b){return a.x==b.x && a.y==b.y;};
            if(minX==maxX || minY==maxY || same(batch.vertices[0],batch.vertices[1]) ||
               same(batch.vertices[0],batch.vertices[2]) || same(batch.vertices[1],batch.vertices[2])){
                cmd[2u]=1u;cmd[4u]=0u;return true;
            }
        }
        if(c.scissor.x0>c.scissor.x1 || c.scissor.y0>c.scissor.y1)return false;
        int x0=std::max<int>(int(sprite ? std::ceil(minX) : std::floor(minX)),c.scissor.x0),y0=std::max<int>(int(sprite ? std::ceil(minY) : std::floor(minY)),c.scissor.y0);
        int x1=std::min<int>(int(std::ceil(maxX))-(sprite ? 1 : 0),c.scissor.x1),y1=std::min<int>(int(std::ceil(maxY))-(sprite ? 1 : 0),c.scissor.y1);
        if(triangle){
            x0=std::clamp(int(std::floor(minX)),int(c.scissor.x0),int(c.scissor.x1));
            x1=std::clamp(int(std::ceil(maxX)),int(c.scissor.x0),int(c.scissor.x1));
            y0=std::clamp(int(std::floor(minY)),int(c.scissor.y0),int(c.scissor.y1));
            y1=std::clamp(int(std::ceil(maxY)),int(c.scissor.y0),int(c.scissor.y1));
        }
        if(x0>x1 || y0>y1){cmd[2u]=1u;cmd[4u]=0u;return true;}
        if(x0<0 || y0<0 || x1>=int(width*64u) || y1>=2048 || width*((unsigned(y1)+32u)/32u)>gpu::PageCount)return false;
        if(line && (maxX-minX>4096.0f || maxY-minY>4096.0f))return false;
        cmd[0]=point ? 0u : line ? 1u : sprite ? 6u : 3u;cmd[1]=unsigned(textured)|(unsigned(s.prim.fst)<<1u)|(unsigned(s.prim.iip)<<2u)|(unsigned(s.prim.abe)<<3u)|(unsigned(s.prim.fge)<<4u)|(unsigned(s.linearFilter)<<5u)|(unsigned(s.pabe)<<6u);
        cmd[2]=unsigned(x0);cmd[3]=unsigned(y0);cmd[4]=unsigned(x1);cmd[5]=unsigned(y1);cmd[6]=c.frame.fbp*32u;cmd[7]=c.zbuf.zbp*32u;cmd[8]=width;
        cmd[9]=uint32_t(c.test);cmd[10]=uint32_t(c.alpha);cmd[11]=uint32_t(c.alpha>>32u)&255u;cmd[12]=c.frame.fbmsk;cmd[13]=c.zbuf.psm;cmd[14]=c.zbuf.zmask;cmd[15]=uint32_t(c.fba)&1u;
        cmd[16]=t.psm;cmd[17]=t.tbp0;cmd[18]=t.tbw;cmd[19]=s.textureWidth;cmd[20]=s.textureHeight;cmd[21]=uint32_t(c.clamp);cmd[22]=uint32_t(c.clamp>>32u);cmd[23]=t.tfx;cmd[24]=t.tcc;
        cmd[25]=s.texa.ta0;cmd[26]=s.texa.ta1;cmd[27]=s.texa.aem;cmd[29]=uint32_t(s.fogR)|(uint32_t(s.fogG)<<8u)|(uint32_t(s.fogB)<<16u);
        cmd[38]=c.frame.psm;cmd[39]=flatDepth;
        const auto frame=rectanglePages(c.frame.psm,cmd[6],width,x0,y0,unsigned(x1-x0+1),unsigned(y1-y0+1));
        const bool usesDepth=((c.test>>17u)&3u)>=2u || !c.zbuf.zmask;
        const auto depth=usesDepth ? rectanglePages(c.zbuf.psm,cmd[7],width,x0,y0,unsigned(x1-x0+1),unsigned(y1-y0+1)) : Pages{};
        if(usesDepth && (frame&depth).any())return false;writes=frame;if(!c.zbuf.zmask)writes|=depth;
        if(textured){
            const auto maximum=[](unsigned size,unsigned mode,unsigned low,unsigned high){return mode==0u || mode==1u ? size-1u : mode==2u ? std::max(low,high) : low|high;};
            const unsigned mx=maximum(s.textureWidth,c.clamp&3u,(c.clamp>>4u)&1023u,(c.clamp>>14u)&1023u);
            const unsigned my=maximum(s.textureHeight,(c.clamp>>2u)&3u,(c.clamp>>24u)&1023u,(c.clamp>>34u)&1023u);
            // This footprint depends on addressing state, never texture bytes.
            // Uploads and TEXFLUSH must retain their usual data ordering.
            const std::array<uint32_t,5> key={t.psm,t.tbp0,t.tbw,mx,my};
            if(!sourceValid || sourceKey!=key){sourcePages=rectanglePages(t.psm,t.tbp0,t.tbw,0u,0u,mx+1u,my+1u);sourceKey=key;sourceValid=true;}
            sources=sourcePages;
            if((sources&writes).any())return false;
        }
        if(sprite){
            const auto& a=batch.vertices[0];const auto& b=batch.vertices[1];const auto q=[](float q){return std::fabs(q)>1.0e-8f ? q : 1.0f;};
            cmd[30]=bits(s.prim.fst ? float(a.u)/16.0f : (a.s/q(a.q))*s.textureWidth);cmd[31]=bits(s.prim.fst ? float(a.v)/16.0f : (a.t/q(a.q))*s.textureHeight);
            cmd[32]=bits(s.prim.fst ? float(b.u)/16.0f : (b.s/q(b.q))*s.textureWidth);cmd[33]=bits(s.prim.fst ? float(b.v)/16.0f : (b.t/q(b.q))*s.textureHeight);
            cmd[34]=bits(minX);cmd[35]=bits(minY);cmd[36]=bits(maxX-minX);cmd[37]=bits(maxY-minY);
        }return true;
    }
};
GpuRasterBackend::GpuRasterBackend():m(std::make_unique<Impl>()){}
GpuRasterBackend::~GpuRasterBackend()=default;
void GpuRasterBackend::Initialize(uint8_t* vram,uint32_t size){
    std::lock_guard lock(m->mutex);
    // A replacement allocation may already have released the previous CPU
    // buffer. Complete its GPU work without writing back through that pointer.
    if(vram==m->vram)m->materialize();
    else if(m->resources){m->submit();m->device->invoke([](gpu::Api& gl){gl.Finish();});}
    m->vram=vram;m->size=size;m->cpu.Initialize(vram,size);m->cpuDirty.set();m->gpuDirty.reset();m->clut.fill(0u);m->cbp.fill(0u);++m->clutGeneration;m->cpuFrame=false;
}
void GpuRasterBackend::Reset(){std::lock_guard lock(m->mutex);m->materialize();m->cpu.Reset();m->clut.fill(0u);m->cbp.fill(0u);++m->clutGeneration;m->cpuFrame=false;}
void GpuRasterBackend::Submit(const GSPrimitiveBatch& batch){
    std::lock_guard lock(m->mutex);gpu::Command command{};Pages writes,sources;
    if(!m->vram || !batch.vertexCount)return;
    if(m->cpuFrame || !m->encode(batch,command,writes,sources) || !m->ready()){
        m->materialize();m->cpu.Submit(batch);m->cpuDirty.set();m->cpuFrame=true;++m->statistics.cpuDraws;totalCpuDraws.fetch_add(1u,std::memory_order_relaxed);return;
    }
    ++m->statistics.gpuDraws;totalGpuDraws.fetch_add(1u,std::memory_order_relaxed);if(command[2u]>command[4u]){++m->statistics.culled;totalCulled.fetch_add(1u,std::memory_order_relaxed);return;}
    const std::array<uint32_t,5> target={command[6],command[7],command[8],command[13],command[38]};
    if(!m->commands.empty() && (m->target!=target || (sources&m->batchWrites).any() || (writes&m->batchSources).any()))m->submit();
    m->upload();m->target=target;
    if((command[1]&1u) && indexed(batch.state.context.tex0.psm))command[28]=m->paletteFor(batch.state);
    const auto index=uint32_t(m->commands.size()/gpu::CommandWords);m->commands.insert(m->commands.end(),command.begin(),command.end());
    const auto addTile=[&](unsigned tile){auto& list=m->tileCommands[tile];if(list.empty())m->activeTiles.push_back(tile);list.push_back(index);++m->references;};
    if(command[0]==1u){
        int x=int(batch.vertices[0].x)-int(batch.state.context.xyoffset.ofx>>4u),y=int(batch.vertices[0].y)-int(batch.state.context.xyoffset.ofy>>4u);
        const int endX=int(batch.vertices[1].x)-int(batch.state.context.xyoffset.ofx>>4u),endY=int(batch.vertices[1].y)-int(batch.state.context.xyoffset.ofy>>4u);
        const int dx=std::abs(endX-x),dy=-std::abs(endY-y),sx=x<endX ? 1 : -1,sy=y<endY ? 1 : -1;
        int error=dx+dy;unsigned previous=~0u;
        for(;;){
            if(x>=int(command[2]) && x<=int(command[4]) && y>=int(command[3]) && y<=int(command[5])){
                const unsigned tile=unsigned(y/8)*256u+unsigned(x/8);if(tile!=previous){addTile(tile);previous=tile;}
            }
            if(x==endX && y==endY)break;const int twice=2*error;
            if(twice>=dy){error+=dy;x+=sx;}if(twice<=dx){error+=dx;y+=sy;}
        }
    }else for(unsigned y=command[3]/8u;y<=command[5]/8u;++y)for(unsigned x=command[2]/8u;x<=command[4]/8u;++x)addTile(y*256u+x);
    m->gpuDirty|=writes;m->batchWrites|=writes;m->batchSources|=sources;
    if(index>=511u || m->references>=65536u)m->submit();
}
void GpuRasterBackend::LoadClut(const GSTex0Reg& t,const GSTexClutReg& tc){
    std::lock_guard lock(m->mutex);if(!m->vram)return;bool load=indexed(t.psm);
    if(t.cld==0u || t.cld>=6u)load=false;
    if(t.cld==2u)m->cbp[0]=t.cbp;if(t.cld==3u)m->cbp[1]=t.cbp;
    if(t.cld==4u || t.cld==5u){const unsigned slot=t.cld-4u;load=load && m->cbp[slot]!=t.cbp;m->cbp[slot]=t.cbp;}
    if(load)m->materialize(rectanglePages(t.cpsm,t.cbp,t.csm==0u ? 1u : std::max<unsigned>(tc.cbw,1u),t.csm==0u ? 0u : unsigned(tc.cou)*16u,t.csm==0u ? 0u : tc.cov,t.csm==0u ? 16u : 256u,t.csm==0u ? 16u : 1u));
    m->cpu.LoadClut(t,tc);if(!load)return;
    const bool sixteen=t.cpsm==2u || t.cpsm==10u;if(!sixteen && t.cpsm!=0u && t.cpsm!=1u)return;
    const unsigned count=fourBit(t.psm) ? 16u : 256u,base=(unsigned(t.csa)&(sixteen ? 31u : 15u))*16u;
    const bool suffix=t.csm==0u && !sixteen && !fourBit(t.psm);
    for(unsigned i=suffix ? base : 0u;i<count;++i){
        const unsigned swizzle=(i&~24u)|((i&8u)<<1u)|((i&16u)>>1u);
        const unsigned x=t.csm==0u ? swizzle&15u : unsigned(tc.cou)*16u+i,y=t.csm==0u ? swizzle>>4u : tc.cov;
        const uint32_t raw=m->cpu.ReadVram(t.cpsm,t.cbp,t.csm==0u ? 1u : std::max<unsigned>(tc.cbw,1u),x,y);
        const unsigned destination=(suffix ? i : base+i)&(sixteen ? 511u : 255u);
        m->clut[destination]=uint16_t(raw);if(!sixteen)m->clut[destination+256u]=uint16_t(raw>>16u);
    }++m->clutGeneration;
}
void GpuRasterBackend::BeginTransfer(const GSTransferCommand& c){
    std::lock_guard lock(m->mutex);m->transfer=c;
    if(c.direction==1u || c.direction==2u)m->materialize();
    if(c.direction==2u)m->submit();m->cpu.BeginTransfer(c);if(c.direction==2u)m->cpuDirty.set();
}
void GpuRasterBackend::UploadImage(const uint8_t* data,uint32_t bytes){
    std::lock_guard lock(m->mutex);const auto before=m->cpu.GetTransferSnapshot();
    if(!data || !bytes || before.direction!=0u){m->cpu.UploadImage(data,bytes);return;}
    const auto& t=m->transfer;const auto& lut=gpu::address_tables();const unsigned bpp=lut[(t.bitbltbuf.dpsm&63u)*8u+3u];
    if(!bpp || !t.trxreg.rrw){m->materialize();m->submit();m->cpu.UploadImage(data,bytes);m->cpuDirty.set();return;}
    unsigned count=std::min<unsigned>(uint64_t(bytes)*8u/bpp,before.totalPixels-before.copiedPixels),first=before.copiedPixels;Pages pages;
    while(count){const unsigned x=first%t.trxreg.rrw,y=first/t.trxreg.rrw,span=std::min<unsigned>(count,t.trxreg.rrw-x);
        pages|=rectanglePages(t.bitbltbuf.dpsm,t.bitbltbuf.dbp,std::max<unsigned>(t.bitbltbuf.dbw,1u),t.trxpos.dsax+x,t.trxpos.dsay+y,span,1u);first+=span;count-=span;}
    if((pages&m->gpuDirty).any())m->materialize(pages);else m->submit();
    m->cpu.UploadImage(data,bytes);m->cpuDirty|=pages;
}
void GpuRasterBackend::Flush(){std::lock_guard lock(m->mutex);m->submit();}
void GpuRasterBackend::TextureFlush(){std::lock_guard lock(m->mutex);m->submit();m->cpu.TextureFlush();}
void GpuRasterBackend::Sync(GSSyncReason reason){std::lock_guard lock(m->mutex);if(reason==GSSyncReason::Presentation)m->submit();else if(reason==GSSyncReason::DebugReadback || reason==GSSyncReason::LocalToHost || reason==GSSyncReason::Reset)m->materialize();else {m->submit();if(m->device && m->resources)m->device->invoke([](gpu::Api& gl){gl.Finish();});}m->cpu.Sync(reason);}
PresentationFrame GpuRasterBackend::Present(const GSPresentationRequest& request){std::lock_guard lock(m->mutex);m->materialize(presentationPages(request));auto frame=m->cpu.Present(request);m->cpuFrame=false;return frame;}
bool GpuRasterBackend::ClearFramebuffer(const GSContext& c,uint32_t rgba){
    std::lock_guard lock(m->mutex);
    if(!m->cpuFrame && c.frame.fbw && (c.frame.psm==0u || c.frame.psm==1u || ((c.frame.psm==2u || c.frame.psm==10u) && !c.frame.fbmsk))){
        GSPrimitiveBatch batch{};batch.vertexCount=2u;auto& s=batch.state;s.prim.type=GS_PRIM_SPRITE;s.context=c;
        s.context.xyoffset={};s.context.test=0x30000u;s.context.zbuf.psm=48u;s.context.zbuf.zmask=true;
        s.context.scissor.x1=std::max(c.scissor.x0,c.scissor.x1);s.context.scissor.y1=std::max(c.scissor.y0,c.scissor.y1);
        batch.vertices[0].x=float(c.scissor.x0);batch.vertices[0].y=float(c.scissor.y0);
        auto& v=batch.vertices[1];v.x=float(s.context.scissor.x1+1u);v.y=float(s.context.scissor.y1+1u);
        v.r=uint8_t(rgba);v.g=uint8_t(rgba>>8u);v.b=uint8_t(rgba>>16u);v.a=uint8_t(rgba>>24u);
        gpu::Command command{};Pages writes,sources;
        if(m->encode(batch,command,writes,sources) && m->ready()){Submit(batch);return true;}
    }
    m->materialize();const bool result=m->cpu.ClearFramebuffer(c,rgba);if(result)m->cpuDirty.set();return result;
}
uint32_t GpuRasterBackend::ConsumeLocalToHostBytes(uint8_t* out,uint32_t count){std::lock_guard lock(m->mutex);m->materialize();return m->cpu.ConsumeLocalToHostBytes(out,count);}
uint32_t GpuRasterBackend::ReadVram(uint32_t psm,uint32_t base,uint32_t width,uint32_t x,uint32_t y)const{std::lock_guard lock(m->mutex);if(!gpu::address_tables()[(psm&63u)*8u]){m->materialize();return m->cpu.ReadVram(psm,base,width,x,y);}const auto page=gpu::bit_address(psm,base,width,x,y)/(gpu::PageBytes*8u);if(m->gpuDirty[page]){Pages requested;requested.set(page);m->materialize(requested);}return m->cpu.ReadVram(psm,base,width,x,y);}
void GpuRasterBackend::WriteVram(uint32_t psm,uint32_t base,uint32_t width,uint32_t x,uint32_t y,uint32_t value){std::lock_guard lock(m->mutex);if(!gpu::address_tables()[(psm&63u)*8u]){m->materialize();m->cpu.WriteVram(psm,base,width,x,y,value);m->cpuDirty.set();return;}const auto page=gpu::bit_address(psm,base,width,x,y)/(gpu::PageBytes*8u);if(m->gpuDirty[page]){Pages requested;requested.set(page);m->materialize(requested);}else m->submit();m->cpu.WriteVram(psm,base,width,x,y,value);m->cpuDirty.set(page);}
void GpuRasterBackend::SnapshotVram(std::vector<uint8_t>& out)const{std::lock_guard lock(m->mutex);m->materialize();m->cpu.SnapshotVram(out);}
GSTransferSnapshot GpuRasterBackend::GetTransferSnapshot()const{std::lock_guard lock(m->mutex);return m->cpu.GetTransferSnapshot();}
GpuRenderStats GpuRasterBackend::stats()const{std::lock_guard lock(m->mutex);return m->statistics;}
GpuRenderStats gpu_render_statistics(){return {totalGpuDraws.load(std::memory_order_relaxed),totalCpuDraws.load(std::memory_order_relaxed),totalBatches.load(std::memory_order_relaxed),totalTiles.load(std::memory_order_relaxed),totalUploads.load(std::memory_order_relaxed),totalReadbacks.load(std::memory_order_relaxed),totalCulled.load(std::memory_order_relaxed)};}
std::unique_ptr<GSRasterBackend> make_raster_backend(){return std::make_unique<GpuRasterBackend>();}
}
