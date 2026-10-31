// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/gs/gs_cpu_backend.h"
#include "raster_workers.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
int main(int argc,char** argv){
 const unsigned workers=argc>1 ? unsigned(std::atoi(argv[1])) : 1u;
 openkai2::set_raster_worker_limit(workers);
 for(unsigned mode=0;mode<40u;++mode){
  const unsigned group=mode/8u,index=(mode%8u)/2u;constexpr uint32_t formats[]={GS_PSM_CT32,GS_PSM_CT16,GS_PSM_T8,GS_PSM_T4};
  std::vector<uint8_t> vram(4u*1024u*1024u);for(unsigned i=0;i<vram.size();++i)vram[i]=uint8_t(i*13u^(i>>9u));
  auto backend=std::make_unique<GSCpuBackend>();backend->Initialize(vram.data(),uint32_t(vram.size()));
  const bool triangle=group==1u || group==2u || group==4u,small=group>=2u;
  GSPrimitiveBatch b{};b.vertexCount=triangle ? 3u : 2u;auto& s=b.state;auto& c=s.context;
  s.prim.type=triangle ? GS_PRIM_TRIANGLE : GS_PRIM_SPRITE;s.prim.tme=s.prim.fst=s.prim.abe=true;
  s.linearFilter=mode&1u;s.colclamp=1u;s.textureWidth=s.textureHeight=128u;s.texa.ta0=s.texa.ta1=128u;
  c.frame.fbp=56u;c.frame.fbw=8u;c.zbuf.zbp=168u;c.zbuf.psm=GS_PSM_Z32;c.zbuf.zmask=true;
  c.scissor={0,511,0,223};c.test=0x30000u;c.alpha=0x44u;
  c.tex0.psm=formats[index];c.tex0.tbp0=group==3u ? 56u*32u : 10240u;c.tex0.tbw=2u;c.tex0.tfx=1u;c.tex0.tcc=true;
  c.tex0.cbp=16000u;c.tex0.cpsm=GS_PSM_CT32;c.tex0.cld=1u;
  for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x)backend->WriteVram(GS_PSM_CT32,16000u,1u,x,y,0x80000000u|(x*17u)|((y*17u)<<8u));
  backend->LoadClut(c.tex0,{});
  for(auto& v:b.vertices){v.r=v.g=v.b=v.a=128u;v.z=1000;v.u=v.v=12u;v.q=1.0f;}
  b.vertices[1].x=small ? 32.0f : 256.0f;b.vertices[1].y=small ? 14.0f : 128.0f;
  b.vertices[1].u=b.vertices[1].v=128u*16u+12u;
  if(triangle){b.vertices[1].y=0.75f;b.vertices[1].v=12u;b.vertices[2].x=0.5f;b.vertices[2].y=small ? 13.25f : 127.25f;b.vertices[2].v=128u*16u+12u;}
  if(group==4u){s.prim.fst=false;for(unsigned i=0;i<3u;++i){auto& v=b.vertices[i];v.q=0.75f+float(i)*0.5f;v.s=float(v.u)/2048.0f;v.t=float(v.v)/2048.0f;}}
  backend->Submit(b);const unsigned iterations=small ? 4096u : 128u;
  const auto started=std::chrono::steady_clock::now();for(unsigned i=0;i<iterations;++i)backend->Submit(b);
  const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
  uint64_t hash=14695981039346656037ull;for(auto byte:vram)hash=(hash^byte)*1099511628211ull;
  std::cout<<"mode="<<mode<<" workers="<<workers<<" wall_ms_per_draw="<<double(ns)/double(iterations*1000000ull)<<" vram="<<std::hex<<hash<<std::dec<<'\n';
 }
}
