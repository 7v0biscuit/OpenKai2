// SPDX-License-Identifier: GPL-3.0-only
// Compare GS submission with full and sampled draw diagnostics. Timings cover
// synthetic host work and do not represent retail game frame rates.
#include "runtime/gs/gs_frontend.h"
#include "guest_timing.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
int main(int argc,char** argv){
    const bool sampled=argc>1 && std::string(argv[1])=="sampled";
    openkai2::enable_guest_timings(sampled);
    for(unsigned mode=0;mode<8u;++mode){
        constexpr unsigned sizes[]={2,8,32,128};const auto size=sizes[mode/2u];const bool triangle=mode&1u;
        std::vector<uint8_t> vram(4u*1024u*1024u,0x35u);auto gs=std::make_unique<GS>();
        gs->init(vram.data(),uint32_t(vram.size()));gs->setDebugHistoryPaused(false);
        gs->writeRegister(0x4cu,56u|(uint64_t(4u)<<16u));gs->writeRegister(0x4eu,168u|(1ull<<32u));
        gs->writeRegister(0x40u,(255ull<<16u)|(223ull<<48u));gs->writeRegister(0x47u,0x30000u);
        gs->writeRegister(0x46u,1u);gs->writeRegister(0x00u,triangle ? 3u : 6u);
        gs->writeRegister(0x01u,(uint64_t(0x3f800000u)<<32u)|0x805b7397u);
        auto vertex=[&](unsigned x,unsigned y){gs->writeRegister(0x05u,uint64_t(x*16u)|(uint64_t(y*16u)<<16u)|(1000ull<<32u));};
        auto draw=[&]{vertex(3,3);vertex(3+size,triangle ? 4u : 3u+size);if(triangle)vertex(4,3+size);};
        draw();
        const unsigned iterations=size<=8 ? 20000u : size<=32 ? 2000u : 128u;
        const auto started=std::chrono::steady_clock::now();for(unsigned i=0;i<iterations;++i)draw();
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
        uint64_t hash=14695981039346656037ull;for(auto byte:vram)hash=(hash^byte)*1099511628211ull;
        std::cout<<"mode="<<mode<<" history="<<(sampled ? "sampled" : "full")<<" wall_ms_per_draw="<<double(ns)/double(iterations*1000000ull)<<" vram="<<std::hex<<hash<<std::dec<<'\n';
    }
    openkai2::enable_guest_timings(false);
}
