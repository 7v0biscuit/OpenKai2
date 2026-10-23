// SPDX-License-Identifier: GPL-3.0-only
#include "host_input.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "Stubs/Pad.h"
#include <cstring>
#include <memory>
#include <stdexcept>

namespace openkai2 {
void verify_guest_input() {
    auto require=[](bool ok,const char* message) {
        if(!ok)throw std::runtime_error(std::string("Runtime guest input regression: ")+message);
    };
    auto owned=std::make_unique<PS2Runtime>();auto& runtime=*owned;
    require(runtime.memory().initialize(),"cannot allocate EE memory");
    auto* ram=runtime.memory().getRDRAM();R5900Context context{};
    reset_host_input();
    struct Scope {
        PS2Runtime& rt;
        ~Scope(){R5900Context c{};ps2_stubs::scePadEnd(rt.memory().getRDRAM(),&c,&rt);reset_host_input();}
    } scope{runtime};
    ps2_stubs::clearPadOverrideState();ps2_stubs::scePadInit(ram,&context,&runtime);
    for(unsigned port=0;port<2u;++port){
        context.r[4]=_mm_cvtsi32_si128(port);context.r[5]=_mm_setzero_si128();
        context.r[6]=_mm_cvtsi32_si128(0x24000u+port*256u);
        ps2_stubs::scePadPortOpen(ram,&context,&runtime);
        require(getRegU32(&context,2)==1u,"pad did not open");
    }
    auto read=[&](unsigned port) {
        constexpr uint32_t entry=0x00115400u,returned=0x0019de98u,data=0x25000u;
        require(runtime.hasFunction(entry),"original pad read is absent");
        std::memset(ram+data-16u,0xa5,64u);context={};context.pc=entry;
        context.r[4]=_mm_cvtsi32_si128(port);context.r[6]=_mm_cvtsi32_si128(data);
        context.r[31]=_mm_cvtsi32_si128(returned);
        runtime.lookupFunction(entry)(ram,&context,&runtime);
        require(context.pc==returned && getRegU32(&context,2)==1u,"compiled pad read failed");
        for(unsigned i=0;i<16u;++i)require(ram[data-16u+i]==0xa5 && ram[data+32u+i]==0xa5,"pad read crossed packet bounds");
        return uint16_t(ram[data+2u])|(uint16_t(ram[data+3u])<<8);
    };
    auto released=LatchedPadState::neutral(),cross=released;
    cross[3]&=~0x40u;
    publish_host_input(0,cross);publish_host_input(0,released);
    require(read(1)==0xffffu,"player one input leaked into player two");
    require((read(0)&0x4000u)==0u,"short Cross tap did not reach the game's pad SDK entry");
    require(read(0)==0xffffu,"released Cross tap repeated");
    const auto observed=host_input_stats(0);
    require((observed.pressed&0x4000u)!=0u && (observed.delivered&0x4000u)!=0u,
            "input diagnostics lost the delivered confirmation");
}
}
