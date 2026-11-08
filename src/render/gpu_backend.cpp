// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_backend.h"
#include "gpu_device.h"
#include "gpu_layout.h"
#include "graphics_settings.h"
#include "hd_textures.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "work_timings.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <future>
#include <map>
#include <unordered_map>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace openkai2 {
namespace gpu {
std::bitset<PageCount> rectangle_pages(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y,unsigned w,unsigned h){
    std::bitset<PageCount> pages;static const auto& lut=gpu::address_tables();if(!lut[(psm&63u)*8u] || !w || !h)return pages;
    if(uint64_t(w)*h>4u*1024u*1024u){pages.set();return pages;}
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
std::atomic<uint64_t> totalPaletteLoads{},totalPaletteReuses{},totalGpuTransfers{};
bool indexed(unsigned psm){return psm==19u || psm==20u || psm==27u || psm==36u || psm==44u;}
enum WaitReason{waitPresent,waitPalette,waitTransfer,waitUpload,waitVram,waitSnapshot,waitOther,waitReasons};
const char* const waitNames[waitReasons]={"present","palette","transfer","upload","vram","snapshot","other"};
std::array<std::atomic<uint64_t>,waitReasons> waitCounts{},waitMicros{};
struct WaitTimer{
    WaitReason reason;std::chrono::steady_clock::time_point began=std::chrono::steady_clock::now();
    ~WaitTimer(){waitCounts[reason].fetch_add(1u,std::memory_order_relaxed);
        waitMicros[reason].fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count()),std::memory_order_relaxed);}
};
std::mutex fallbackMutex;
std::map<std::string,uint64_t> fallbackCounts;
void countFallback(const GSPrimitiveBatch& batch,const char* reason){
    const auto& c=batch.state.context;
    char key[160];
    std::snprintf(key,sizeof key,"reason=%s prim=%u frame_psm=0x%x z_psm=0x%x tex_psm=0x%x tex=%ux%u",reason,unsigned(batch.state.prim.type),
                  unsigned(c.frame.psm),unsigned(c.zbuf.psm),unsigned(c.tex0.psm),batch.state.textureWidth,batch.state.textureHeight);
    std::lock_guard lock(fallbackMutex);
    if(++fallbackCounts[key]==1u && fallbackCounts.size()<=64u)std::cout<<"[renderer:fallback] "<<key<<std::endl;
}
bool fourBit(unsigned psm){return psm==20u || psm==36u || psm==44u;}
uint32_t bits(float value){uint32_t result;std::memcpy(&result,&value,4u);return result;}
uint32_t rgba(const GSVertex& v){return uint32_t(v.r)|(uint32_t(v.g)<<8u)|(uint32_t(v.b)<<16u)|(uint32_t(v.a)<<24u);}
Pages rectanglePages(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y,unsigned w,unsigned h){
    return gpu::rectangle_pages(psm,base,width,x,y,w,h);
}
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
struct Resources {std::unique_ptr<gpu::Compute> compute;};
std::vector<gpu::Range> pageRanges(const Pages& pages){
    std::vector<gpu::Range> ranges;
    for(unsigned p=0;p<gpu::PageCount;){if(!pages[p]){++p;continue;}const unsigned start=p;while(p<gpu::PageCount && pages[p])++p;ranges.push_back({start*gpu::PageBytes,(p-start)*gpu::PageBytes});}
    return ranges;
}
}
struct GpuRasterBackend::Impl {
    mutable std::recursive_mutex mutex;GSCpuBackend cpu;uint8_t* vram=nullptr;uint32_t size=0;
    const std::vector<uint32_t>& lut=gpu::address_tables();
    std::shared_ptr<gpu::Device> device;std::shared_ptr<Resources> resources;
    Pages cpuDirty,gpuDirty,batchWrites,batchSources;bool disabled=false,cpuFrame=false;
    GSTransferCommand transfer{};GpuRenderStats statistics{};
    std::array<uint16_t,512> clut{};std::array<uint32_t,2> cbp{};uint32_t clutGeneration=0;
    std::array<uint32_t,8> loadedPaletteKey{};Pages loadedPalettePages;bool loadedPaletteValid=false;
    std::vector<uint32_t> commands,palette,activeTiles;
    gpu::Command pendingCommand{};
    std::array<std::vector<uint32_t>,65536> tileCommands;
    std::map<std::array<uint32_t,3>,uint32_t> paletteOffsets;
    std::array<uint32_t,5> target{},sourceKey{};Pages sourcePages;bool sourceValid=false;size_t references=0;
    std::array<uint32_t,12> footprintKey{};int covered[4]{};bool footprintValid=false;
    struct Shadow{uint32_t base=0,width=0,psm=0;bool used=false,valid=false;uint64_t touched=0;Pages pages,stale;};
    static constexpr unsigned shadowWidth=1024u,shadowHeight=512u,colorSlots=6u,depthSlots=3u;
    unsigned scale=1u;bool hires=false,batchDepth=false;uint64_t shadowClock=0;
    std::array<Shadow,colorSlots> colorShadows{};std::array<Shadow,depthSlots> depthShadows{};
    size_t slotWords()const{return size_t(shadowWidth*scale)*size_t(shadowHeight*scale);}
    static constexpr size_t hdWords=24u<<20u,hdLargest=4u<<20u;
    size_t hdCapacity=0;
    struct HdSlot{uint32_t offset=0,width=0,height=0;};
    bool hdOn=false,hdFull=false;size_t hdHead=0;uint64_t hiresFrames=0,nativeFrames=0;std::array<uint64_t,6> hdSkips{};std::array<uint64_t,6> nativeWhy{};std::unordered_map<uint64_t,HdSlot> hdSlots;
    struct KeyHash{size_t operator()(const std::array<uint64_t,4>& k)const{uint64_t h=k[0];for(unsigned i=1;i<4u;++i)h=(h^k[i])*0x9e3779b97f4a7c15ull;return size_t(h^(h>>29u));}};
    std::unordered_map<std::array<uint64_t,4>,uint64_t,KeyHash> hdHashes;std::array<uint32_t,gpu::PageCount> pageVersion{},pageHashed{};std::array<uint64_t,gpu::PageCount> pageHash{};
    struct HdMemo{std::array<uint64_t,2> key{};uint64_t generation=0,result=0;Pages pages;};
    std::array<HdMemo,64> hdMemo{};uint64_t hdGeneration=1;
    static constexpr size_t hdDecodePerFrame=256u*1024u;size_t hdDecodeBudget=hdDecodePerFrame;
    uint64_t pageContent(unsigned p){
        if(!cpuDirty[p] && pageHashed[p]==pageVersion[p]+1u)return pageHash[p];
        uint64_t h=0x9e3779b97f4a7c15ull^p;const uint8_t* bytes=vram+size_t(p)*gpu::PageBytes;
        for(unsigned i=0;i<gpu::PageBytes;i+=8u){uint64_t w;std::memcpy(&w,bytes+i,8u);h=(h^w)*0x100000001b3ull;h^=h>>32u;}
        if(!cpuDirty[p]){pageHash[p]=h;pageHashed[p]=pageVersion[p]+1u;}
        return h;
    }
    size_t hdBase()const{return colorSlots*slotWords();}
    uint64_t hdContent(const GSDrawState& state,const uint32_t* cmd){
        const auto& t=state.context.tex0;const unsigned w=state.textureWidth,h=state.textureHeight;
        const std::array<uint64_t,2> memoKey={uint64_t(t.psm)|(uint64_t(t.tbp0)<<8u)|(uint64_t(t.tbw)<<24u)|(uint64_t(w)<<32u)|(uint64_t(h)<<48u),
            (indexed(t.psm) ? uint64_t(cmd[28]) : 0u)|(uint64_t(state.texa.ta0)<<32u)|(uint64_t(state.texa.ta1)<<40u)|(uint64_t(state.texa.aem)<<48u)};
        auto& memo=hdMemo[size_t((memoKey[0]*0x9e3779b97f4a7c15ull)^memoKey[1])%hdMemo.size()];
        if(memo.generation==hdGeneration && memo.key==memoKey){
            if((memo.pages&gpuDirty).any())return 0u;
            if(memo.result && !(memo.pages&cpuDirty).any())return memo.result;
        }
        const auto pages=rectanglePages(t.psm,t.tbp0,std::max<unsigned>(t.tbw,1u),0u,0u,w,h);
        const auto remember=[&](uint64_t result){memo.key=memoKey;memo.generation=hdGeneration;memo.pages=pages;memo.result=result;return result;};
        if((pages&gpuDirty).any())return remember(0u);
        uint64_t stamp=1469598103934665603ull;
        for(size_t p=pages._Find_first();p<gpu::PageCount;p=pages._Find_next(p)){stamp^=pageContent(unsigned(p));stamp*=1099511628211ull;}
        uint64_t colors=0u;
        if(indexed(t.psm)){const uint32_t first=cmd[28];for(unsigned i=0;i<(fourBit(t.psm) ? 16u : 256u);++i){colors^=palette[first+i];colors*=1099511628211ull;}}
        const std::array<uint64_t,4> key={uint64_t(t.psm)|(uint64_t(t.tbp0)<<8u)|(uint64_t(t.tbw)<<24u)|(uint64_t(w)<<32u)|(uint64_t(h)<<48u),stamp,colors,
            uint64_t(state.texa.ta0)|(uint64_t(state.texa.ta1)<<8u)|(uint64_t(state.texa.aem)<<16u)};
        if(const auto found=hdHashes.find(key);found!=hdHashes.end())return remember(found->second);
        if(size_t(w)*h>hdDecodeBudget){++hdSkips[5];return 0u;}
        hdDecodeBudget-=size_t(w)*h;
        hd::Image image;image.width=w;image.height=h;image.texels.resize(size_t(w)*h);
        const uint32_t first=cmd[28];const bool sixteen=t.psm==2u || t.psm==10u;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const uint32_t raw=cpu.ReadVram(t.psm,t.tbp0,std::max<unsigned>(t.tbw,1u),x,y);
            image.texels[size_t(y)*w+x]=indexed(t.psm) ? palette[first+(raw&255u)] : textureAlpha(state.texa,t.psm,sixteen ? color16(raw) : t.psm==1u ? raw&0xffffffu : raw);
        }
        const uint64_t hash=hd::texture_hash(image.texels.data(),w,h);
        if(hdHashes.size()>16384u)hdHashes.clear();
        hdHashes.emplace(key,hash);
        if(!hd::library().known(hash))hd::library().submit(hash,std::move(image));
        return remember(hash);
    }
    void applyHd(const GSDrawState& state,uint32_t* cmd){
        const auto& c=state.context;const unsigned w=state.textureWidth,h=state.textureHeight;
        if((c.clamp&3u)==3u || ((c.clamp>>2u)&3u)==3u){++hdSkips[0];return;}
        if(w<8u || h<8u || w>1024u || h>1024u){++hdSkips[1];return;}
        const uint64_t hash=hdContent(state,cmd);if(!hash){++hdSkips[2];return;}
        if(!hd::library().replacing())return;
        auto slot=hdSlots.find(hash);
        if(slot==hdSlots.end()){
            const auto image=hd::library().find(hash);
            if(!image){++hdSkips[3];return;}
            const size_t words=size_t(image->width)*image->height;
            if(!words || words>hdLargest)return;
            if(hdHead+words>hdCapacity){hdFull=true;return;}
            slot=hdSlots.emplace(hash,HdSlot{uint32_t(hdHead),image->width,image->height}).first;
            device->post([r=resources,image,offset=hdBase()+hdHead](gpu::Host&){r->compute->hires_upload(offset,image->texels.data(),image->texels.size());});
            hdHead+=(words+63u)&~size_t(63u);
        }
        ++hdSkips[4];cmd[89]=slot->second.offset+1u;cmd[90]=slot->second.width;cmd[91]=slot->second.height;
    }
    void invalidateShadows(const Pages& pages){
        if(!hires || !pages.any())return;
        for(auto& shadow:colorShadows)if(shadow.used)shadow.stale|=shadow.pages&pages;
        for(auto& shadow:depthShadows)if(shadow.used)shadow.stale|=shadow.pages&pages;
    }
    void dropShadows(){for(auto& shadow:colorShadows)shadow=Shadow{};for(auto& shadow:depthShadows)shadow=Shadow{};}
    template<size_t N> unsigned acquireShadow(std::array<Shadow,N>& shadows,uint32_t base,uint32_t width,uint32_t psm){
        unsigned chosen=N;
        for(unsigned i=0;i<N;++i)if(shadows[i].used && shadows[i].base==base && shadows[i].width==width && shadows[i].psm==psm){chosen=i;break;}
        if(chosen==N){
            chosen=0;
            for(unsigned i=0;i<N;++i){if(!shadows[i].used){chosen=i;break;}if(shadows[i].touched<shadows[chosen].touched)chosen=i;}
            auto& shadow=shadows[chosen];shadow=Shadow{};shadow.used=true;shadow.base=base;shadow.width=width;shadow.psm=psm;
            shadow.pages=gpu::rectangle_pages(psm,base,width,0u,0u,width*64u,shadowHeight);
        }
        shadows[chosen].touched=++shadowClock;return chosen;
    }
    std::vector<uint32_t> shadowParams(unsigned color,unsigned depth,bool hasDepth,uint32_t width)const{
        const uint32_t stride=width*64u*scale;
        return {scale,uint32_t(color*slotWords()),stride,uint32_t(depth*slotWords()),stride,hasDepth ? 1u : 0u,shadowHeight*scale,0u,
                0u,4294967295u,width,0u,0u,uint32_t(hdBase())};
    }
    bool ready(){
        if(disabled || size!=gpu::VramBytes)return false;
        if(resources)return true;
        device=gpu::current_device();if(!device)return false;
        try{
            resources=std::make_shared<Resources>();scale=std::clamp(graphics_settings().internalScale,1u,4u);
            bool supported=false;size_t capacity=0;const unsigned hdMode=scale>1u ? graphics_settings().hdTextures : 0u;
            device->invoke([r=resources,&supported,&capacity,words=slotWords(),scaled=scale>1u,extra=hdMode ? hdWords : 0u](gpu::Host& host){
                r->compute=host.create();r->compute->create(gpu::address_tables());
                supported=scaled && r->compute->hires_supported();
                const size_t limit=r->compute->storage_word_limit();
                supported=supported && words*(colorSlots+1u)<=limit && words*depthSlots<=limit;
                if(supported){
                    capacity=std::min(extra,limit-words*(colorSlots+1u));
                    r->compute->hires_reserve(words*colorSlots+capacity+words,words*depthSlots);
                }
            });
            hires=supported;hdCapacity=capacity;dropShadows();
            hdOn=hires && hdMode!=0u && hdCapacity!=0u;
            if(hdOn)hd::library().configure(hdMode,graphics_settings().textureFolder,scale>=3u ? 4u : 2u);
            if(hdOn && hdMode==hd::packs && !hd::library().has_packs()){hdOn=false;std::cout<<"[renderer] hd_textures: no texture packs found in "<<graphics_settings().textureFolder.string()<<std::endl;}
            if(scale>1u)std::cout<<"[renderer] internal_resolution="<<scale<<"x "<<(hires ? "enabled" : "unavailable")<<" hd_textures="<<(hdOn ? hdMode : 0u)<<std::endl;
            cpuDirty.set();return true;
        }
        catch(const std::exception& e){resources.reset();disabled=true;if(renderer_preference()=="gpu")throw;std::cerr<<"[renderer] GPU initialization failed: "<<e.what()<<"\n";return false;}
    }
    ~Impl(){
        try{if(device && device->running()){submit();device->invoke([r=resources](gpu::Host&){if(r && r->compute){r->compute->destroy();r->compute.reset();}});}}catch(...){}

    }
    void upload(){
        if(!cpuDirty.any())return;
        invalidateShadows(cpuDirty);
        for(unsigned p=0;p<gpu::PageCount;++p)if(cpuDirty[p])++pageVersion[p];
        ++hdGeneration;
        struct Segment{unsigned offset;std::vector<uint8_t> bytes;};std::vector<Segment> segments;
        for(unsigned page=0;page<gpu::PageCount;){
            if(!cpuDirty[page]){++page;continue;}const unsigned first=page;while(page<gpu::PageCount && cpuDirty[page])++page;
            Segment s{first*gpu::PageBytes,{}};s.bytes.assign(vram+s.offset,vram+page*gpu::PageBytes);statistics.uploads+=s.bytes.size();totalUploads.fetch_add(s.bytes.size(),std::memory_order_relaxed);segments.push_back(std::move(s));
        }
        device->post([r=resources,segments=std::move(segments)](gpu::Host&){
            for(const auto& s:segments)r->compute->upload(s.offset,s.bytes.data(),s.bytes.size());
        });cpuDirty.reset();
    }
    void submit(){
        footprintValid=false;
        if(commands.empty())return;
        RuntimeWorkTimer timing(RuntimeWork::gsBatch);
        std::vector<uint32_t> bins,refs;bins.reserve(activeTiles.size()*4u);refs.reserve(references);
        for(auto id:activeTiles){auto& list=tileCommands[id];bins.insert(bins.end(),{id&255u,id>>8u,uint32_t(refs.size()),uint32_t(list.size())});refs.insert(refs.end(),list.begin(),list.end());list.clear();}
        if(palette.empty())palette.push_back(0u);
        const size_t commandWords=commands.size();
        statistics.tiles+=activeTiles.size();++statistics.batches;totalTiles.fetch_add(activeTiles.size(),std::memory_order_relaxed);totalBatches.fetch_add(1u,std::memory_order_relaxed);
        std::vector<uint32_t> hiresParams,seedColor,seedDepth;unsigned groups=0u,seedX=0u,seedY=0u;
        if(hires){
            if(target[2]>0u && target[2]*64u<=shadowWidth){
                const unsigned color=acquireShadow(colorShadows,target[0],target[2],target[4]);
                const bool useDepth=batchDepth;
                const unsigned depth=useDepth ? acquireShadow(depthShadows,target[1],target[2],target[3]) : 0u;
                hiresParams=shadowParams(color,depth,useDepth,target[2]);
                seedX=(target[2]*64u*scale+7u)/8u;seedY=(shadowHeight*scale+7u)/8u;
                auto& colorShadow=colorShadows[color];
                if(!colorShadow.valid || (colorShadow.stale&batchWrites).any()){
                    seedColor=hiresParams;seedColor[5]=0u;seedColor[8]=target[4];seedColor[9]=target[0];colorShadow.valid=true;colorShadow.stale.reset();
                }
                if(useDepth && (!depthShadows[depth].valid || depthShadows[depth].stale.any())){
                    seedDepth=hiresParams;seedDepth[5]=1u;seedDepth[9]=4294967295u;seedDepth[11]=target[3];seedDepth[12]=target[1];depthShadows[depth].valid=true;depthShadows[depth].stale.reset();
                }
                for(unsigned i=0;i<colorSlots;++i)if(i!=color && colorShadows[i].used)colorShadows[i].stale|=colorShadows[i].pages&batchWrites;
                for(unsigned i=0;i<depthSlots;++i)if((!useDepth || i!=depth) && depthShadows[i].used)depthShadows[i].stale|=depthShadows[i].pages&batchWrites;
                groups=unsigned(bins.size()/4u)*scale*scale;
            }else invalidateShadows(batchWrites);
        }
        device->post([r=resources,cmd=std::move(commands),bins=std::move(bins),refs=std::move(refs),colors=std::move(palette),
                      hiresParams=std::move(hiresParams),seedColor=std::move(seedColor),seedDepth=std::move(seedDepth),groups,seedX,seedY](gpu::Host&){
            if(!seedColor.empty())r->compute->hires_seed(seedColor,seedX,seedY);
            if(!seedDepth.empty())r->compute->hires_seed(seedDepth,seedX,seedY);
            r->compute->raster(cmd,bins,refs,colors);
            if(groups)r->compute->hires_raster(hiresParams,groups);
        });commands.clear();commands.reserve(commandWords);palette.clear();++hdGeneration;activeTiles.clear();paletteOffsets.clear();references=0u;batchWrites.reset();batchSources.reset();batchDepth=false;
        if(hdFull){hdSlots.clear();hdHead=0u;hdFull=false;}
    }
    void materialize(const Pages& requested,WaitReason reason=waitOther){
        footprintValid=false;
        const auto pages=gpuDirty&requested;
        if(!resources || !pages.any())return;submit();
        statistics.readbacks+=pages.count()*gpu::PageBytes;totalReadbacks.fetch_add(pages.count()*gpu::PageBytes,std::memory_order_relaxed);
        RuntimeWorkTimer wait(RuntimeWork::gpuWait);WaitTimer timed{reason};
        device->invoke([r=resources,ranges=pageRanges(pages),ram=vram](gpu::Host&){r->compute->read(ranges,ram);});gpuDirty&=~pages;
    }
    void materialize(WaitReason reason=waitOther){Pages all;all.set();materialize(all,reason);}
    bool transferFormat(unsigned psm)const{const unsigned bpp=lut[(psm&63u)*8u+3u];return lut[(psm&63u)*8u] && (bpp==4u || bpp==8u || bpp==16u || bpp==24u || bpp==32u);}
    void transferOnGpu(std::vector<uint32_t> job){
        const unsigned pixels=job[1];if(!pixels)return;
        submit();upload();++statistics.gpuTransfers;totalGpuTransfers.fetch_add(1u,std::memory_order_relaxed);
        device->post([r=resources,job=std::move(job),pixels](gpu::Host&){r->compute->transfer(job,pixels);});
    }
    bool transferPages(const GSTransferCommand& c,Pages& source,Pages& destination){
        const auto& b=c.bitbltbuf;const auto& p=c.trxpos;const auto& r=c.trxreg;const auto& lut=gpu::address_tables();
        const bool copy=c.direction==2u;
        if(!r.rrw || !r.rrh || !lut[(b.spsm&63u)*8u] || (copy && !lut[(b.dpsm&63u)*8u]))return false;
        if(p.ssax+r.rrw>2048u || p.ssay+r.rrh>2048u || (copy && (p.dsax+r.rrw>2048u || p.dsay+r.rrh>2048u)))return false;
        source=rectanglePages(b.spsm,b.sbp,std::max<unsigned>(b.sbw,1u),p.ssax,p.ssay,r.rrw,r.rrh);
        destination=copy ? rectanglePages(b.dpsm,b.dbp,std::max<unsigned>(b.dbw,1u),p.dsax,p.dsay,r.rrw,r.rrh) : Pages{};
        return true;
    }
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
    bool encode(const GSPrimitiveBatch& batch,uint32_t* cmd,Pages& writes,Pages& sources,bool& footprint){
        const auto& s=batch.state;const auto& c=s.context;const auto& t=c.tex0;
        const bool sprite=s.prim.type==GS_PRIM_SPRITE,point=s.prim.type==GS_PRIM_POINT;
        const bool line=s.prim.type==GS_PRIM_LINE || s.prim.type==GS_PRIM_LINESTRIP;
        const bool triangle=s.prim.type==GS_PRIM_TRIANGLE || s.prim.type==GS_PRIM_TRISTRIP || s.prim.type==GS_PRIM_TRIFAN;
        const bool textured=s.prim.tme && (sprite || triangle);
        const bool flatDepth=triangle && batch.vertices[0].z==batch.vertices[1].z && batch.vertices[1].z==batch.vertices[2].z;
        if(gpu_draw_unsupported(batch))return false;
        float minX=0,maxX=0,minY=0,maxY=0;const unsigned width=std::max<unsigned>(c.frame.fbw,1u);
        for(unsigned i=0;i<batch.vertexCount;++i){const auto& v=batch.vertices[i];
            if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.s) || !std::isfinite(v.t) || !std::isfinite(v.q) || std::abs(v.x)>65536.0f || std::abs(v.y)>65536.0f || v.z<0.0 || v.z>(triangle && !flatDepth ? 4294901760.0 : 4294967295.0))return false;
            const float x=(point || line ? float(int(v.x)) : v.x)-(sprite ? float(c.xyoffset.ofx)/16.0f : float(c.xyoffset.ofx>>4u));
            const float y=(point || line ? float(int(v.y)) : v.y)-(sprite ? float(c.xyoffset.ofy)/16.0f : float(c.xyoffset.ofy>>4u));
            if(!i){minX=maxX=x;minY=maxY=y;}else{minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);}
            const unsigned at=gpu::VertexWord+i*gpu::VertexWords;cmd[at]=bits(x);cmd[at+1u]=bits(y);std::memcpy(&cmd[at+2u],&v.z,8u);
            cmd[at+4u]=rgba(v);cmd[at+5u]=v.fog;cmd[at+6u]=bits(v.s);cmd[at+7u]=bits(v.t);cmd[at+8u]=bits(v.q);cmd[at+9u]=v.u;cmd[at+10u]=v.v;
        }
        if(triangle){
            const auto same=[](const GSVertex& a,const GSVertex& b){return a.x==b.x && a.y==b.y;};
            if(minX==maxX || minY==maxY || same(batch.vertices[0],batch.vertices[1]) ||
               same(batch.vertices[0],batch.vertices[2]) || same(batch.vertices[1],batch.vertices[2])){
                cmd[2u]=1u;cmd[4u]=0u;return true;
            }
        }
        if(c.scissor.x0>c.scissor.x1 || c.scissor.y0>c.scissor.y1)return false;
        // Clamping both ends of an offscreen triangle collapses its bounds to
        // the scissor edge and still queues work for that tile. Keep an extra
        // pixel at the right and bottom edges for the rasterizer's fill rules.
        if(triangle && (maxX<float(c.scissor.x0) || maxY<float(c.scissor.y0) ||
                        minX>float(c.scissor.x1)+1.0f || minY>float(c.scissor.y1)+1.0f)){
            cmd[2u]=1u;cmd[4u]=0u;return true;
        }
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
        const bool depthEnabled=(c.test&0x10000u)!=0u;
        // ZTE=0 bypasses ZTST and blocks writes even when ZMSK is clear.
        // Normalize the compute command so native and scaled targets agree.
        cmd[9]=depthEnabled ? uint32_t(c.test) : (uint32_t(c.test)&~(3u<<17u))|(1u<<17u);
        cmd[10]=uint32_t(c.alpha);cmd[11]=uint32_t(c.alpha>>32u)&255u;cmd[12]=c.frame.fbmsk;cmd[13]=c.zbuf.psm;cmd[14]=c.zbuf.zmask || !depthEnabled;cmd[15]=uint32_t(c.fba)&1u;
        cmd[16]=t.psm;cmd[17]=t.tbp0;cmd[18]=t.tbw;cmd[19]=s.textureWidth;cmd[20]=s.textureHeight;cmd[21]=uint32_t(c.clamp);cmd[22]=uint32_t(c.clamp>>32u);cmd[23]=t.tfx;cmd[24]=t.tcc;
        cmd[25]=s.texa.ta0;cmd[26]=s.texa.ta1;cmd[27]=s.texa.aem;cmd[29]=uint32_t(s.fogR)|(uint32_t(s.fogG)<<8u)|(uint32_t(s.fogB)<<16u);
        cmd[38]=c.frame.psm;cmd[39]=flatDepth;
        if(point || sprite || flatDepth){
            const double z=batch.vertices[sprite ? 1u : 0u].z;
            cmd[88]=uint32_t(std::min(z+(flatDepth ? 0.5 : 0.0),4294967295.0));
        }
        const bool usesDepth=((cmd[9]>>17u)&3u)>=2u || cmd[14]==0u;
        unsigned mx=0u,my=0u;
        if(textured){
            const auto maximum=[](unsigned size,unsigned mode,unsigned low,unsigned high){return mode==0u || mode==1u ? size-1u : mode==2u ? std::max(low,high) : low|high;};
            mx=maximum(s.textureWidth,c.clamp&3u,(c.clamp>>4u)&1023u,(c.clamp>>14u)&1023u);
            my=maximum(s.textureHeight,(c.clamp>>2u)&3u,(c.clamp>>24u)&1023u,(c.clamp>>34u)&1023u);
        }
        const std::array<uint32_t,12> key={cmd[6],cmd[7],width,c.frame.psm,c.zbuf.psm,unsigned(usesDepth)|(cmd[14]<<1u)|(unsigned(textured)<<2u),
            textured ? t.psm : 0u,textured ? t.tbp0 : 0u,textured ? t.tbw : 0u,mx,my,0u};
        footprint=footprintValid && key==footprintKey && x0>=covered[0] && y0>=covered[1] && x1<=covered[2] && y1<=covered[3];
        if(footprint){
            if(sprite){
                const auto& a=batch.vertices[0];const auto& b=batch.vertices[1];const auto q=[](float q){return std::fabs(q)>1.0e-8f ? q : 1.0f;};
                cmd[30]=bits(s.prim.fst ? float(a.u)/16.0f : (a.s/q(a.q))*s.textureWidth);cmd[31]=bits(s.prim.fst ? float(a.v)/16.0f : (a.t/q(a.q))*s.textureHeight);
                cmd[32]=bits(s.prim.fst ? float(b.u)/16.0f : (b.s/q(b.q))*s.textureWidth);cmd[33]=bits(s.prim.fst ? float(b.v)/16.0f : (b.t/q(b.q))*s.textureHeight);
                cmd[34]=bits(minX);cmd[35]=bits(minY);cmd[36]=bits(maxX-minX);cmd[37]=bits(maxY-minY);
            }return true;
        }
        const auto frame=rectanglePages(c.frame.psm,cmd[6],width,x0,y0,unsigned(x1-x0+1),unsigned(y1-y0+1));
        const auto depth=usesDepth ? rectanglePages(c.zbuf.psm,cmd[7],width,x0,y0,unsigned(x1-x0+1),unsigned(y1-y0+1)) : Pages{};
        if(usesDepth && (frame&depth).any())return false;writes=frame;if(cmd[14]==0u)writes|=depth;
        if(textured){
            const std::array<uint32_t,5> key={t.psm,t.tbp0,t.tbw,mx,my};
            if(!sourceValid || sourceKey!=key){sourcePages=rectanglePages(t.psm,t.tbp0,t.tbw,0u,0u,mx+1u,my+1u);sourceKey=key;sourceValid=true;}
            sources=sourcePages;
            if((sources&writes).any())return false;
        }
        footprintKey=key;footprintValid=(cmd[6]&31u)==0u && (!usesDepth || (cmd[7]&31u)==0u);
        if(footprintValid){
            const auto align=[&](unsigned psm,int* r){const int pw=int(lut[(psm&63u)*8u]),ph=int(lut[(psm&63u)*8u+1u]);
                r[0]=std::max(r[0],x0/pw*pw);r[1]=std::max(r[1],y0/ph*ph);r[2]=std::min(r[2],(x1/pw+1)*pw-1);r[3]=std::min(r[3],(y1/ph+1)*ph-1);};
            covered[0]=covered[1]=std::numeric_limits<int>::min();covered[2]=covered[3]=std::numeric_limits<int>::max();
            align(c.frame.psm,covered);if(usesDepth)align(c.zbuf.psm,covered);
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
    if(vram==m->vram)m->materialize();
    else if(m->resources){m->submit();m->device->invoke([r=m->resources](gpu::Host&){r->compute->finish();});}
    m->vram=vram;m->size=size;m->dropShadows();m->cpu.Initialize(vram,size);m->cpuDirty.set();m->gpuDirty.reset();m->clut.fill(0u);m->cbp.fill(0u);++m->clutGeneration;m->cpuFrame=false;m->loadedPaletteValid=false;
}
void GpuRasterBackend::Reset(){std::lock_guard lock(m->mutex);m->materialize();m->dropShadows();m->cpu.Reset();m->clut.fill(0u);m->cbp.fill(0u);++m->clutGeneration;m->cpuFrame=false;m->loadedPaletteValid=false;}
void GpuRasterBackend::Submit(const GSPrimitiveBatch& batch){
    thread_local unsigned sampled=0;
    struct Sample{bool on;std::chrono::steady_clock::time_point began;
        ~Sample(){if(on)record_runtime_work(RuntimeWork::gsDraw,16u*uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-began).count()));}
    } timing{(++sampled&15u)==0u,{}};if(timing.on)timing.began=std::chrono::steady_clock::now();
    std::lock_guard lock(m->mutex);Pages writes,sources;bool footprint=false;
    if(!m->vram || !batch.vertexCount)return;
    // Encode before appending: culled primitives must not grow or clear the
    // command buffer. Every used field is overwritten; HD slots are optional.
    auto& pending=m->pendingCommand;pending[89]=pending[90]=pending[91]=0u;
    if(m->cpuFrame || !m->encode(batch,pending.data(),writes,sources,footprint) || !m->ready()){
        const char* reason=gpu_draw_unsupported(batch);
        countFallback(batch,reason ? reason : m->cpuFrame ? "after-earlier-fallback" : !m->ready() ? "device-not-ready" : "draw-area");
        m->footprintValid=false;m->loadedPaletteValid=false;
        m->materialize();m->cpu.Submit(batch);m->cpuDirty.set();m->cpuFrame=true;++m->statistics.cpuDraws;totalCpuDraws.fetch_add(1u,std::memory_order_relaxed);return;
    }
    ++m->statistics.gpuDraws;totalGpuDraws.fetch_add(1u,std::memory_order_relaxed);
    if(pending[2u]>pending[4u]){++m->statistics.culled;totalCulled.fetch_add(1u,std::memory_order_relaxed);return;}
    const size_t at=m->commands.size();m->commands.insert(m->commands.end(),pending.begin(),pending.end());
    if(!footprint){
        if((writes&m->loadedPalettePages).any())m->loadedPaletteValid=false;
        const uint32_t* encoded=m->commands.data()+at;
        const std::array<uint32_t,5> target={encoded[6],encoded[7],encoded[8],encoded[13],encoded[38]};
        if(at && (m->target!=target || (sources&m->batchWrites).any() || (writes&m->batchSources).any())){
            gpu::Command pending;std::copy_n(encoded,gpu::CommandWords,pending.begin());m->commands.resize(at);
            const auto key=m->footprintKey;const bool valid=m->footprintValid;int area[4];std::copy_n(m->covered,4,area);
            m->submit();m->commands.insert(m->commands.end(),pending.begin(),pending.end());
            m->footprintKey=key;m->footprintValid=valid;std::copy_n(area,4,m->covered);
        }
        m->target=target;
    }
    m->upload();
    const size_t base=m->commands.size()-gpu::CommandWords;
    if((m->commands[base+1u]&1u) && indexed(batch.state.context.tex0.psm))m->commands[base+28u]=m->paletteFor(batch.state);
    if(m->hdOn && (m->commands[base+1u]&1u))m->applyHd(batch.state,m->commands.data()+base);
    const uint32_t* command=m->commands.data()+base;
    const auto index=uint32_t(base/gpu::CommandWords);
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
    if(!footprint){m->gpuDirty|=writes;m->batchWrites|=writes;m->batchSources|=sources;}
    if(command[14]==0u || ((command[9]>>17u)&3u)>=2u)m->batchDepth=true;
    if(index>=4095u || m->references>=(1u<<20u))m->submit();
}
void GpuRasterBackend::LoadClut(const GSTex0Reg& t,const GSTexClutReg& tc){
    std::lock_guard lock(m->mutex);m->footprintValid=false;if(!m->vram)return;bool load=indexed(t.psm);
    if(t.cld==0u || t.cld>=6u)load=false;
    if(t.cld==2u)m->cbp[0]=t.cbp;if(t.cld==3u)m->cbp[1]=t.cbp;
    if(t.cld==4u || t.cld==5u){const unsigned slot=t.cld-4u;load=load && m->cbp[slot]!=t.cbp;m->cbp[slot]=t.cbp;}
    const std::array<uint32_t,8> key={t.psm,t.cpsm,t.csm,t.csa,t.cbp,tc.cbw,tc.cou,tc.cov};
    if(load && t.cld==1u && m->loadedPaletteValid && key==m->loadedPaletteKey){
        ++m->statistics.paletteReuses;totalPaletteReuses.fetch_add(1u,std::memory_order_relaxed);return;
    }
    Pages pages;
    if(load){pages=rectanglePages(t.cpsm,t.cbp,t.csm==0u ? 1u : std::max<unsigned>(tc.cbw,1u),t.csm==0u ? 0u : unsigned(tc.cou)*16u,t.csm==0u ? 0u : tc.cov,t.csm==0u ? 16u : 256u,t.csm==0u ? 16u : 1u);m->materialize(pages,waitPalette);m->loadedPaletteValid=false;}
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
    }++m->clutGeneration;++m->statistics.paletteLoads;totalPaletteLoads.fetch_add(1u,std::memory_order_relaxed);
    m->loadedPaletteKey=key;m->loadedPalettePages=pages;m->loadedPaletteValid=t.cld==1u;
}
void GpuRasterBackend::BeginTransfer(const GSTransferCommand& c){
    std::lock_guard lock(m->mutex);m->transfer=c;Pages source,destination;
    const unsigned pixels=unsigned(c.trxreg.rrw)*c.trxreg.rrh;
    if(c.direction==2u && m->ready() && m->transferPages(c,source,destination) && ((source|destination)&m->gpuDirty).any() &&
       !(source&destination).any() && m->transferFormat(c.bitbltbuf.spsm) && m->transferFormat(c.bitbltbuf.dpsm) && pixels<=65535u*64u){
        const auto& b=c.bitbltbuf;const auto& p=c.trxpos;
        m->transferOnGpu({1u,pixels,0u,c.trxreg.rrw,b.dpsm,b.dbp,std::max<unsigned>(b.dbw,1u),p.dsax,p.dsay,b.spsm,b.sbp,std::max<unsigned>(b.sbw,1u),p.ssax,p.ssay});
        auto finished=c;finished.trxreg.rrh=0u;m->cpu.BeginTransfer(finished);
        m->gpuDirty|=destination;m->cpuDirty&=~destination;m->invalidateShadows(destination);if((destination&m->loadedPalettePages).any())m->loadedPaletteValid=false;return;
    }
    if((c.direction==1u || c.direction==2u) && m->transferPages(c,source,destination)){
        m->materialize(source|destination,waitTransfer);m->submit();m->cpu.BeginTransfer(c);
        m->cpuDirty|=destination;if((destination&m->loadedPalettePages).any())m->loadedPaletteValid=false;return;
    }
    if(c.direction==1u || c.direction==2u)m->materialize(waitTransfer);
    if(c.direction==2u)m->submit();m->cpu.BeginTransfer(c);if(c.direction==2u){m->cpuDirty.set();m->loadedPaletteValid=false;}
}
void GpuRasterBackend::UploadImage(const uint8_t* data,uint32_t bytes){
    std::lock_guard lock(m->mutex);const auto before=m->cpu.GetTransferSnapshot();
    if(!data || !bytes || before.direction!=0u){m->cpu.UploadImage(data,bytes);return;}
    const auto& t=m->transfer;const auto& lut=gpu::address_tables();const unsigned bpp=lut[(t.bitbltbuf.dpsm&63u)*8u+3u];
    if(!bpp || !t.trxreg.rrw){m->materialize(waitUpload);m->submit();m->cpu.UploadImage(data,bytes);m->cpuDirty.set();m->loadedPaletteValid=false;return;}
    unsigned count=std::min<unsigned>(uint64_t(bytes)*8u/bpp,before.totalPixels-before.copiedPixels),first=before.copiedPixels;Pages pages;
    while(count){const unsigned x=first%t.trxreg.rrw,y=first/t.trxreg.rrw,span=std::min<unsigned>(count,t.trxreg.rrw-x);
        pages|=rectanglePages(t.bitbltbuf.dpsm,t.bitbltbuf.dbp,std::max<unsigned>(t.bitbltbuf.dbw,1u),t.trxpos.dsax+x,t.trxpos.dsay+y,span,1u);first+=span;count-=span;}
    if((pages&m->gpuDirty).any() && m->ready() && m->transferFormat(t.bitbltbuf.dpsm)){
        m->cpu.UploadImage(data,bytes);const auto after=m->cpu.GetTransferSnapshot();
        const unsigned moved=after.copiedPixels-before.copiedPixels,used=unsigned((uint64_t(moved)*bpp+7u)/8u);
        std::vector<uint32_t> job={0u,moved,before.copiedPixels,t.trxreg.rrw,t.bitbltbuf.dpsm,t.bitbltbuf.dbp,std::max<unsigned>(t.bitbltbuf.dbw,1u),t.trxpos.dsax,t.trxpos.dsay,0u,0u,0u,0u,0u};
        job.resize(job.size()+(used+3u)/4u+1u,0u);std::memcpy(job.data()+14u,data,std::min(used,bytes));
        m->transferOnGpu(std::move(job));m->invalidateShadows(pages);
        m->cpuDirty|=pages&~m->gpuDirty;if((pages&m->loadedPalettePages).any())m->loadedPaletteValid=false;return;
    }
    if((pages&m->gpuDirty).any())m->materialize(pages,waitUpload);else if((pages&m->batchSources).any())m->submit();
    m->cpu.UploadImage(data,bytes);m->cpuDirty|=pages;if((pages&m->loadedPalettePages).any())m->loadedPaletteValid=false;
}
void GpuRasterBackend::Flush(){std::lock_guard lock(m->mutex);m->submit();}
void GpuRasterBackend::TextureFlush(){std::lock_guard lock(m->mutex);m->submit();m->cpu.TextureFlush();}
void GpuRasterBackend::Sync(GSSyncReason reason){std::lock_guard lock(m->mutex);if(reason==GSSyncReason::Presentation)m->submit();else if(reason==GSSyncReason::DebugReadback || reason==GSSyncReason::LocalToHost || reason==GSSyncReason::Reset)m->materialize();
    else m->submit();m->cpu.Sync(reason);}
namespace {
struct HiresPlan{bool ok=false,half=false;unsigned slot=0,width=0,height=0,rows=0,x=0,stride=0,fbp=0,psm=0,scale=1,outWidth=0,outHeight=0;size_t offset=0,resolveBase=0;};
uint32_t hiresPixel(uint32_t value,unsigned psm){
    if(psm==2u || psm==10u){
        const auto channel=[&](unsigned shift){const uint32_t c=(value>>shift)&31u;return (c<<3u)|(c>>2u);};
        return channel(0u)|(channel(5u)<<8u)|(channel(10u)<<16u)|0xff000000u;
    }
    return (value&0xffffffu)|0xff000000u;
}
}
PresentationFrame GpuRasterBackend::Present(const GSPresentationRequest& request){
    thread_local std::vector<uint8_t> snapshot;thread_local GSCpuBackend presenter;
    std::future<void> readback;PresentationFrame frame;bool ready=false;HiresPlan plan;
    thread_local auto hiresBuffer=std::make_shared<std::vector<uint32_t>>();auto hiresWords=hiresBuffer;size_t hiresCount=0;auto resolved=std::make_shared<bool>(false);
    {
        std::lock_guard lock(m->mutex);
        const Pages pages=presentationPages(request),dirty=m->gpuDirty&pages;m->cpuFrame=false;
        if(!m->resources || (!dirty.any() && !m->hires))return m->cpu.Present(request);
        m->submit();
        if(m->hires){
            const unsigned enabled=unsigned(request.pmode&3u),mode=unsigned(request.smode2&3u);
            if(!(enabled==1u || enabled==2u) || mode==1u)++m->nativeWhy[0];
            else{
                const uint64_t buffer=enabled==1u ? request.dispfb1 : request.dispfb2,display=enabled==1u ? request.display1 : request.display2;
                const unsigned fbp=unsigned(buffer&511u),fbw=unsigned((buffer>>9u)&63u),psm=unsigned((buffer>>15u)&31u);
                const unsigned dbx=unsigned((buffer>>32u)&2047u),dby=unsigned((buffer>>43u)&2047u);
                const unsigned magh=unsigned((display>>23u)&15u),dw=unsigned((display>>32u)&4095u),dh=unsigned((display>>44u)&2047u);
                const unsigned width=(dw+1u)/(magh+1u),height=dh+1u,half=mode==3u,rows=half ? (height+1u)/2u : height;
                unsigned why=1u;
                for(unsigned i=0;i<Impl::colorSlots && fbw;++i){
                    const auto& shadow=m->colorShadows[i];
                    if(!shadow.used || shadow.base!=fbp*32u || shadow.width!=fbw || shadow.psm!=psm)continue;
                    if(dbx+width>fbw*64u || dby+rows>Impl::shadowHeight || width<16u || rows<16u){why=4u;break;}
                    const auto shown=rectanglePages(psm,fbp*32u,fbw,dbx,dby,width,rows);
                    if(!shadow.valid || (shadow.stale&shown).any()){why=2u;break;}
                    if((shown&m->cpuDirty).any()){why=3u;break;}
                    why=0u;
                    plan.ok=true;plan.half=half;plan.slot=i;plan.width=width;plan.height=height;plan.rows=rows;plan.x=dbx;plan.fbp=fbp;plan.psm=psm;
                    plan.scale=m->scale;plan.stride=fbw*64u*m->scale;plan.offset=i*m->slotWords()+size_t(dby)*m->scale*plan.stride;
                    {const unsigned sourceHeight=height*m->scale,limit=std::max(graphics_settings().height,height);
                     plan.outHeight=std::min(sourceHeight,limit);plan.outWidth=std::max(1u,unsigned((uint64_t(width)*m->scale*plan.outHeight+sourceHeight/2u)/sourceHeight));
                     plan.resolveBase=m->hdBase()+m->hdCapacity;}
                    hiresCount=size_t(rows)*m->scale*plan.stride;if(hiresWords->size()<std::max(hiresCount,size_t(plan.outWidth)*plan.outHeight))hiresWords->resize(std::max(hiresCount,size_t(plan.outWidth)*plan.outHeight));break;
                }
                if(why)++m->nativeWhy[why];
            }
        }
        if(!dirty.any()){
            if(!plan.ok)return m->cpu.Present(request);
            frame=m->cpu.Present(request);ready=true;
        }else{
            if(snapshot.size()!=m->size || pages.all())snapshot.assign(m->vram,m->vram+m->size);
            else for(unsigned p=0;p<gpu::PageCount;){if(!pages[p]){++p;continue;}const unsigned start=p;while(p<gpu::PageCount && pages[p])++p;
                const size_t offset=size_t(start)*gpu::PageBytes,end=std::min<size_t>(size_t(p)*gpu::PageBytes,m->size);
                if(offset<end)std::memcpy(snapshot.data()+offset,m->vram+offset,end-offset);}
            m->statistics.readbacks+=dirty.count()*gpu::PageBytes;totalReadbacks.fetch_add(dirty.count()*gpu::PageBytes,std::memory_order_relaxed);
        }
        auto done=std::make_shared<std::promise<void>>();readback=done->get_future();
        std::vector<uint32_t> resolveParams;
        if(hiresCount)resolveParams={plan.width*plan.scale,plan.height*plan.scale,uint32_t(plan.offset+size_t(plan.x)*plan.scale),plan.stride,plan.psm,plan.half ? 1u : 0u,
            plan.rows*plan.scale,plan.outWidth,plan.outHeight,uint32_t(plan.resolveBase)};
        m->device->post([r=m->resources,ranges=pageRanges(dirty),ram=snapshot.data(),done,hiresWords,hiresCount,offset=plan.offset,resolveParams=std::move(resolveParams),resolved](gpu::Host&){
            try{
                if(!ranges.empty())r->compute->read(ranges,ram);
                if(hiresCount && r->compute->hires_resolve(resolveParams,(resolveParams[7]+7u)/8u,(resolveParams[8]+7u)/8u)){
                    *resolved=true;r->compute->hires_read(resolveParams[9],size_t(resolveParams[7])*resolveParams[8],hiresWords->data());
                }else if(hiresCount)r->compute->hires_read(offset,hiresCount,hiresWords->data());
                done->set_value();
            }catch(...){done->set_exception(std::current_exception());throw;}
        });
    }
    {RuntimeWorkTimer wait(RuntimeWork::gpuWait);WaitTimer timed{waitPresent};readback.get();}
    if(!ready){presenter.Initialize(snapshot.data(),uint32_t(snapshot.size()));frame=presenter.Present(request);}
    const bool scaledFrame=plan.ok && frame && frame.width==plan.width && frame.height==plan.height && !frame.usedPreferred && frame.sourceFbp==plan.fbp;
    if(m->hires && frame){
        std::lock_guard lock(m->mutex);++(scaledFrame ? m->hiresFrames : m->nativeFrames);m->hdDecodeBudget=Impl::hdDecodePerFrame;
        if(plan.ok && !scaledFrame)++m->nativeWhy[5];
        if((m->hiresFrames+m->nativeFrames)%600u==0u){
            const auto counts=hd::library().counts();
            std::cout<<"[renderer:hires] frames_scaled="<<m->hiresFrames<<" frames_native="<<m->nativeFrames<<" hd_upscaled="<<counts.upscaled<<" hd_packed="<<counts.packed<<" hd_dumped="<<counts.dumped<<" hd_resident="<<m->hdSlots.size()<<" hd_draws="<<m->hdSkips[4]<<" hd_skip_region="<<m->hdSkips[0]<<" hd_skip_size="<<m->hdSkips[1]<<" hd_skip_target="<<m->hdSkips[2]<<" hd_pending="<<m->hdSkips[3]<<" hd_deferred="<<m->hdSkips[5]<<" native_display="<<m->nativeWhy[0]<<" native_no_shadow="<<m->nativeWhy[1]<<" native_stale="<<m->nativeWhy[2]<<" native_cpu="<<m->nativeWhy[3]<<" native_bounds="<<m->nativeWhy[4]<<" native_source="<<m->nativeWhy[5]<<std::endl;
        }
    }
    if(!scaledFrame)return frame;
    if(*resolved){
        PresentationFrame scaled;scaled.width=plan.outWidth;scaled.height=plan.outHeight;scaled.stride=plan.outWidth;
        scaled.displayFbp=frame.displayFbp;scaled.sourceFbp=frame.sourceFbp;scaled.usedPreferred=false;
        scaled.pixels.resize(size_t(plan.outWidth)*plan.outHeight*4u);
        std::memcpy(scaled.pixels.data(),hiresWords->data(),scaled.pixels.size());
        return scaled;
    }
    const unsigned s=plan.scale,outWidth=plan.width*s,outHeight=plan.height*s,sourceRows=plan.rows*s;
    PresentationFrame scaled;scaled.width=outWidth;scaled.height=outHeight;scaled.stride=outWidth;
    scaled.displayFbp=frame.displayFbp;scaled.sourceFbp=frame.sourceFbp;scaled.usedPreferred=false;
    scaled.pixels.resize(size_t(outWidth)*outHeight*4u);
    const auto& words=*hiresWords;
    const auto convert=[&](unsigned firstRow,unsigned lastRow){
        for(unsigned y=firstRow;y<lastRow;++y){
            uint8_t* out=scaled.pixels.data()+size_t(y)*outWidth*4u;
            if(plan.half){
                const unsigned a=std::min(y/2u,sourceRows-1u),b=std::min(a+(y&1u),sourceRows-1u);
                const uint32_t* rowA=words.data()+size_t(a)*plan.stride+plan.x*s;const uint32_t* rowB=words.data()+size_t(b)*plan.stride+plan.x*s;
                for(unsigned x=0;x<outWidth;++x){
                    const uint32_t p=hiresPixel(rowA[x],plan.psm),q=hiresPixel(rowB[x],plan.psm);
                    const uint32_t mixed=((p|q)-(((p^q)&0xfefefefeu)>>1u))|0xff000000u;std::memcpy(out+x*4u,&mixed,4u);
                }
            }else{
                const uint32_t* row=words.data()+size_t(std::min(y,sourceRows-1u))*plan.stride+plan.x*s;
                for(unsigned x=0;x<outWidth;++x){const uint32_t p=hiresPixel(row[x],plan.psm);std::memcpy(out+x*4u,&p,4u);}
            }
        }
    };
    const unsigned parts=size_t(outWidth)*outHeight>=(1u<<20u) ? 4u : 1u;
    std::vector<std::future<void>> pending;
    for(unsigned i=1;i<parts;++i)pending.push_back(std::async(std::launch::async,convert,outHeight*i/parts,outHeight*(i+1u)/parts));
    convert(0u,outHeight/parts);
    for(auto& part:pending)part.get();
    return scaled;
}
bool GpuRasterBackend::ClearFramebuffer(const GSContext& c,uint32_t rgba){
    std::lock_guard lock(m->mutex);
    if(!m->cpuFrame && c.frame.fbw && (c.frame.psm==0u || c.frame.psm==1u || ((c.frame.psm==2u || c.frame.psm==10u) && !c.frame.fbmsk))){
        GSPrimitiveBatch batch{};batch.vertexCount=2u;auto& s=batch.state;s.prim.type=GS_PRIM_SPRITE;s.context=c;
        s.context.xyoffset={};s.context.test=0x30000u;s.context.zbuf.psm=48u;s.context.zbuf.zmask=true;
        s.context.scissor.x1=std::max(c.scissor.x0,c.scissor.x1);s.context.scissor.y1=std::max(c.scissor.y0,c.scissor.y1);
        batch.vertices[0].x=float(c.scissor.x0);batch.vertices[0].y=float(c.scissor.y0);
        auto& v=batch.vertices[1];v.x=float(s.context.scissor.x1+1u);v.y=float(s.context.scissor.y1+1u);
        v.r=uint8_t(rgba);v.g=uint8_t(rgba>>8u);v.b=uint8_t(rgba>>16u);v.a=uint8_t(rgba>>24u);
        gpu::Command command{};Pages writes,sources;bool footprint=false;
        const bool encodable=m->encode(batch,command.data(),writes,sources,footprint);m->footprintValid=false;
        if(encodable && m->ready()){Submit(batch);return true;}
    }
    m->materialize();const bool result=m->cpu.ClearFramebuffer(c,rgba);if(result){m->cpuDirty.set();m->loadedPaletteValid=false;}return result;
}
uint32_t GpuRasterBackend::ConsumeLocalToHostBytes(uint8_t* out,uint32_t count){std::lock_guard lock(m->mutex);return m->cpu.ConsumeLocalToHostBytes(out,count);}
uint32_t GpuRasterBackend::ReadVram(uint32_t psm,uint32_t base,uint32_t width,uint32_t x,uint32_t y)const{std::lock_guard lock(m->mutex);if(!gpu::address_tables()[(psm&63u)*8u]){m->materialize();return m->cpu.ReadVram(psm,base,width,x,y);}const auto page=gpu::bit_address(psm,base,width,x,y)/(gpu::PageBytes*8u);if(m->gpuDirty[page]){Pages requested;requested.set(page);m->materialize(requested,waitVram);}return m->cpu.ReadVram(psm,base,width,x,y);}
void GpuRasterBackend::WriteVram(uint32_t psm,uint32_t base,uint32_t width,uint32_t x,uint32_t y,uint32_t value){std::lock_guard lock(m->mutex);m->loadedPaletteValid=false;if(!gpu::address_tables()[(psm&63u)*8u]){m->materialize();m->cpu.WriteVram(psm,base,width,x,y,value);m->cpuDirty.set();return;}const auto page=gpu::bit_address(psm,base,width,x,y)/(gpu::PageBytes*8u);if(m->gpuDirty[page]){Pages requested;requested.set(page);m->materialize(requested,waitVram);}else m->submit();m->cpu.WriteVram(psm,base,width,x,y,value);m->cpuDirty.set(page);}
void GpuRasterBackend::SnapshotVram(std::vector<uint8_t>& out)const{std::lock_guard lock(m->mutex);m->materialize(waitSnapshot);m->cpu.SnapshotVram(out);}
GSTransferSnapshot GpuRasterBackend::GetTransferSnapshot()const{std::lock_guard lock(m->mutex);return m->cpu.GetTransferSnapshot();}
GpuRenderStats GpuRasterBackend::stats()const{std::lock_guard lock(m->mutex);return m->statistics;}
const char* gpu_draw_unsupported(const GSPrimitiveBatch& batch){
    const auto& s=batch.state;const auto& c=s.context;const auto& t=c.tex0;const auto& lut=gpu::address_tables();
    const bool sprite=s.prim.type==GS_PRIM_SPRITE,point=s.prim.type==GS_PRIM_POINT;
    const bool line=s.prim.type==GS_PRIM_LINE || s.prim.type==GS_PRIM_LINESTRIP;
    const bool triangle=s.prim.type==GS_PRIM_TRIANGLE || s.prim.type==GS_PRIM_TRISTRIP || s.prim.type==GS_PRIM_TRIFAN;
    const bool textured=s.prim.tme && (sprite || triangle);
    if((!sprite && !point && !line && !triangle) || batch.vertexCount!=(point ? 1u : triangle ? 3u : 2u))return "primitive";
    if(c.frame.psm!=0u && c.frame.psm!=1u && c.frame.psm!=2u && c.frame.psm!=10u)return "frame-format";
    if(c.zbuf.psm!=0u && c.zbuf.psm!=1u && c.zbuf.psm!=2u && c.zbuf.psm!=10u && c.zbuf.psm!=48u && c.zbuf.psm!=49u && c.zbuf.psm!=50u && c.zbuf.psm!=58u)return "depth-format";
    if(s.scanmsk!=0u)return "scan-mask";
    if(textured && !lut[(t.psm&63u)*8u])return "texture-format";
    if(textured && (!s.textureWidth || !s.textureHeight || s.textureWidth>1024u || s.textureHeight>1024u))return "texture-size";
    if(textured && indexed(t.psm) && t.cpsm!=0u && t.cpsm!=1u && t.cpsm!=2u && t.cpsm!=10u)return "palette-format";
    return nullptr;
}
std::vector<std::array<uint64_t,3>> gpu_wait_counts(){
    std::vector<std::array<uint64_t,3>> result;
    for(unsigned i=0;i<waitReasons;++i)result.push_back({i,waitCounts[i].load(std::memory_order_relaxed),waitMicros[i].load(std::memory_order_relaxed)});
    return result;
}
const char* gpu_wait_name(unsigned reason){return reason<waitReasons ? waitNames[reason] : "unknown";}
std::vector<std::pair<std::string,uint64_t>> gpu_fallback_counts(){
    std::lock_guard lock(fallbackMutex);return {fallbackCounts.begin(),fallbackCounts.end()};
}
GpuRenderStats gpu_render_statistics(){return {totalGpuDraws.load(std::memory_order_relaxed),totalCpuDraws.load(std::memory_order_relaxed),totalBatches.load(std::memory_order_relaxed),totalTiles.load(std::memory_order_relaxed),totalUploads.load(std::memory_order_relaxed),totalReadbacks.load(std::memory_order_relaxed),totalCulled.load(std::memory_order_relaxed),totalPaletteLoads.load(std::memory_order_relaxed),totalPaletteReuses.load(std::memory_order_relaxed),totalGpuTransfers.load(std::memory_order_relaxed)};}
std::unique_ptr<GSRasterBackend> make_raster_backend(){return std::make_unique<GpuRasterBackend>();}
}
