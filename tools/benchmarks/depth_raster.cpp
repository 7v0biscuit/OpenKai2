// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/gs/gs_cpu_backend.h"
#include "raster_workers.h"
#include <vector>
#include <memory>
#include <chrono>
#include <iostream>
int main(){for(unsigned mode=0;mode<18u;++mode){
 std::vector<uint8_t> vram(4u*1024u*1024u);for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*13u^(i>>9u));
 auto backend=std::make_unique<GSCpuBackend>();backend->Initialize(vram.data(),unsigned(vram.size()));
 GSPrimitiveBatch b{};b.vertexCount=mode<6u ? 2u : 3u;auto& s=b.state;auto& c=s.context;
 s.prim.type=mode<6u ? GS_PRIM_SPRITE : GS_PRIM_TRIANGLE;s.prim.tme=s.prim.fst=s.prim.abe=true;
 s.colclamp=1;s.linearFilter=(mode&1u)==0u;s.textureWidth=s.textureHeight=512;
 c.alpha=0x44u;c.frame.fbp=56;c.frame.fbw=8;c.zbuf.zbp=168;c.zbuf.psm=GS_PSM_Z32;c.zbuf.zmask=false;
 c.test=0x50000;c.scissor={0,511,0,223};c.tex0.tbp0=10240;c.tex0.tbw=8;c.tex0.tfx=1;c.tex0.tcc=true;
 c.tex0.psm=(mode&1u) ? GS_PSM_T4 : GS_PSM_T8;c.tex0.cbp=16000;c.tex0.cpsm=GS_PSM_CT32;c.tex0.cld=1;
 for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)backend->WriteVram(GS_PSM_CT32,16000,1,x,y,0x80000000u|(x*17u)|((y*17u)<<8u));
 backend->LoadClut(c.tex0,{});
 for(auto& v:b.vertices){v.r=v.g=v.b=v.a=128;v.z=1000;v.u=v.v=12;}
 b.vertices[1].x=512;b.vertices[1].y=224;b.vertices[1].u=512*16+12;b.vertices[1].v=224*16+12;
 if(mode>=6u){b.vertices[1].y=8.75;b.vertices[1].v=12;b.vertices[2].x=12.5;b.vertices[2].y=223.25;b.vertices[2].v=224*16+12;}
 if(mode>=12u)for(auto& v:b.vertices){v.x*=0.0625f;v.y*=0.0625f;}
 const unsigned rejection=(mode%6u)/2u;
 for(unsigned y=0;y<224u;++y)for(unsigned x=0;x<512u;++x)
  backend->WriteVram(GS_PSM_Z32,168u*32u,8u,x,y,rejection==2u || (rejection==1u && (x&1u)) ? 2000u : 0u);
 openkai2::set_raster_worker_limit(4);backend->Submit(b);
 const unsigned iterations=mode>=12u ? 4096u : 64u;
 auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<iterations;++i)backend->Submit(b);
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
 uint64_t hash=14695981039346656037ull;for(auto v:vram)hash=(hash^v)*1099511628211ull;
 std::cout<<"mode="<<std::dec<<mode<<" wall_ms_per_draw="<<double(us)/double(iterations*1000u)<<" vram="<<std::hex<<hash<<std::dec<<"\n";
}}
