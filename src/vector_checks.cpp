// SPDX-License-Identifier: GPL-3.0-only
#include "vector_checks.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"
#include <algorithm>
#include <chrono>
#include <cfenv>
#if defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace openkai2 {
namespace {
constexpr uint32_t nop=0x2ffu;
uint32_t integer(unsigned to,unsigned from,unsigned imm){return 0x10000000u|(to<<16u)|(from<<11u)|(imm&2047u);}
uint32_t special(unsigned op,unsigned source){return 0x80000000u|(source<<11u)|((op&0x7cu)<<4u)|(op&3u)|0x3cu;}
uint32_t upper(unsigned op,unsigned to,unsigned from,unsigned other){return (15u<<21u)|(other<<16u)|(from<<11u)|(to<<6u)|op;}
}
VectorWorkResult vector_workload(unsigned mode,unsigned iterations) {
    if(mode>=16u || !iterations)throw std::runtime_error("Invalid vector workload");
    auto memory=std::make_unique<PS2Memory>();if(!memory->initialize())throw std::runtime_error("Vector memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(memory->getGSVRAM(),PS2_GS_VRAM_SIZE,&memory->gs());
    const bool vu0=mode==11u || mode==15u,untracked=mode==12u;
    auto vu=std::make_unique<VU1Interpreter>(vu0 ? VU1Interpreter::Unit::VU0 : VU1Interpreter::Unit::VU1);
    auto* code=vu0 ? memory->getVU0Code() : memory->getVU1Code();auto* data=vu0 ? memory->getVU0Data() : memory->getVU1Data();
    const unsigned codeSize=vu0 ? PS2_VU0_CODE_SIZE : PS2_VU1_CODE_SIZE;
    unsigned dataSize=vu0 ? PS2_VU0_DATA_SIZE : PS2_VU1_DATA_SIZE;
    std::memset(data,0,dataSize);
    const bool kick=mode>=4u && mode<=7u || mode==14u;
    const unsigned pairs=mode==0u || kick || mode==15u ? 4u : 32u,start=mode==13u ? 4u : 0u;
    for(unsigned pc=0;pc+8u<=codeSize;pc+=8u){uint64_t v=uint64_t(nop)<<32u;std::memcpy(code+pc,&v,8u);}
    auto pair=[&](unsigned at,uint32_t lo,uint32_t hi){const uint64_t v=lo|(uint64_t(hi)<<32u);std::memcpy(code+at,&v,8u);};
    for(unsigned i=0;i<pairs;++i){
        uint32_t lo=0u,hi=nop;
        if(mode==1u || mode==8u || mode==9u)lo=integer(1u+(i%7u),1u+((i+6u)%7u),i+1u);
        else if(!kick && mode!=0u && mode!=15u){
            lo=integer(1u+(i%7u),0u,i+1u);
            hi=upper(i&1u ? 0x28u : 0x2au,3u+(i%8u),1u,2u);
            if(mode==3u)lo=i&1u ? 0x02000000u|(15u<<21u)|(2u<<16u)|(3u<<11u) : (15u<<21u)|(3u<<16u)|(2u<<11u);
        }
        if(kick && i==0u)lo=special(0x6cu,1u);
        if(i+2u==pairs)hi|=0x40000000u;
        pair(start+i*8u,lo,hi);
    }
    uint64_t first;std::memcpy(&first,code,8u);memory->write64(vu0 ? PS2_VU0_CODE_BASE : PS2_VU1_CODE_BASE,first);
    auto seed=[&](){for(unsigned reg=1u;reg<32u;++reg)for(unsigned lane=0;lane<4u;++lane)vu->state().vf[reg][lane]=float((reg+lane)%7u+1u)*0.125f;
        for(unsigned reg=1;reg<16u;++reg)vu->state().vi[reg]=int32_t(reg);};seed();
    uint64_t capture=14695981039346656037ull,packets=0u;
    memory->setGifPacketCallback([&](const uint8_t* bytes,uint32_t count){++packets;
        const auto hashBytes=iterations<=64u ? count : std::min(count,32u);
        for(unsigned i=0;i<hashBytes;++i)capture=(capture^bytes[i])*1099511628211ull;capture=(capture^count)*1099511628211ull;
    });
    if(kick){
        const unsigned loops=mode==5u ? 15u : mode==6u ? 255u : 0u;
        const uint64_t tag=loops|0x8000u|(uint64_t(1u)<<60u),registers=0xeu;
        const unsigned packetBytes=16u+loops*16u;
        std::vector<uint8_t> packet(packetBytes);
        std::memcpy(packet.data(),&tag,8u);std::memcpy(packet.data()+8u,&registers,8u);
        for(unsigned i=0;i<loops;++i){const uint64_t value=i*0x12347u,reg=0x3du;std::memcpy(packet.data()+16u+i*16u,&value,8u);std::memcpy(packet.data()+24u+i*16u,&reg,8u);}
        if(mode==7u)dataSize=31u;
        const unsigned source=mode==7u ? 16u : 0u;
        for(unsigned i=0;i<packetBytes;++i)data[(source+i)%dataSize]=packet[i];
        vu->state().vi[1]=mode==7u ? 1 : 0;
    }
    std::vector<uint8_t> mpg(4u+pairs*8u);const uint32_t command=0x4a000000u|(pairs<<16u);
    std::memcpy(mpg.data(),&command,4u);std::memcpy(mpg.data()+4u,code,pairs*8u);
    const auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i){
        if(mode==14u){vu->reset();seed();vu->state().vi[1]=0;}
        if(mode==8u || mode==9u){
            if(mode==9u){const auto value=integer(1u,0u,(i&1u)+1u);std::memcpy(mpg.data()+4u,&value,4u);}
            memory->processVIF1Data(mpg.data(),unsigned(mpg.size()));
        }
        vu->execute(code,codeSize,data,dataSize,*gs,untracked ? nullptr : memory.get(),start,3u,7u,mode==10u ? 4u : 8192u);
        if(mode==10u)vu->resume(code,codeSize,data,dataSize,*gs,memory.get(),3u,7u,8192u);
    }
    const double elapsed=double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count());
    uint64_t hash=14695981039346656037ull;
    auto add=[&](const auto& value){const auto* bytes=reinterpret_cast<const uint8_t*>(&value);for(unsigned i=0;i<sizeof(value);++i)hash=(hash^bytes[i])*1099511628211ull;};
    const auto& state=vu->state();add(state.vf);add(state.vi);add(state.acc);add(state.q);add(state.p);add(state.i);add(state.r);
    add(state.pc);add(state.mac);add(state.clip);add(state.status);add(state.cycles);add(state.ebit);add(state.haltAfterDelaySlot);
    add(state.stoppedByD);add(state.stoppedByT);add(state.branchPending);add(state.branchTarget);add(state.branchDelay);add(state.top);add(state.itop);
    add(capture);add(packets);for(unsigned i=0;i<dataSize;++i)hash=(hash^data[i])*1099511628211ull;
    return {hash,state.cycles,packets,elapsed/double(iterations)};
}
void verify_vector_zero_register() {
    for(bool vu0:{false,true})for(unsigned operation=0;operation<3u;++operation)for(unsigned lower=0;lower<3u;++lower){
        auto m=std::make_unique<PS2Memory>();if(!m->initialize())throw std::runtime_error("Zero register memory initialization failed");
        auto gs=std::make_unique<GS>();gs->init(m->getGSVRAM(),PS2_GS_VRAM_SIZE,&m->gs());
        auto vu=std::make_unique<VU1Interpreter>(vu0 ? VU1Interpreter::Unit::VU0 : VU1Interpreter::Unit::VU1);
        auto* code=vu0 ? m->getVU0Code() : m->getVU1Code();auto* data=vu0 ? m->getVU0Data() : m->getVU1Data();
        const unsigned codeSize=vu0 ? PS2_VU0_CODE_SIZE : PS2_VU1_CODE_SIZE,dataSize=vu0 ? PS2_VU0_DATA_SIZE : PS2_VU1_DATA_SIZE;
        const uint32_t upperWord=operation==0u ? upper(0x28u,0u,1u,2u) : operation==1u ? upper(0x2bu,0u,1u,2u)
            : (15u<<21u)|(1u<<11u)|0x13cu;
        const uint32_t lowerWord=lower==0u ? 0x02000000u|(15u<<21u) : lower==1u ? 0x8000033cu|(15u<<21u)|(4u<<16u) : 0x806303bcu;
        const uint64_t pair=lowerWord|(uint64_t(upperWord|0x40000000u)<<32u),delay=uint64_t(nop)<<32u;
        std::memcpy(code,&pair,8u);std::memcpy(code+8u,&delay,8u);
        constexpr float a[]={-2,0,3,4},b[]={1,0,-3,-8},constant[]={0,0,0,1};
        std::memcpy(vu->state().vf[1],a,16u);std::memcpy(vu->state().vf[2],b,16u);vu->state().vf[3][0]=2.0f;std::memset(data,0xa5,32u);
        vu->execute(code,codeSize,data,dataSize,*gs,m.get(),0u,0u,0u,64u);
        const auto* actual=lower==0u ? data : reinterpret_cast<const uint8_t*>(vu->state().vf[4]);
        if((lower==2u && vu->state().q!=0.5f) || (lower!=2u && std::memcmp(actual,constant,16u)!=0))throw std::runtime_error("Upper VF00 write leaked into the paired lower read");
        if(std::memcmp(vu->state().vf[0],constant,16u)!=0)throw std::runtime_error("VF00 constant changed");
        if(operation==0u && (vu->state().mac!=0x96u || vu->state().status!=0xc3u))throw std::runtime_error("Discarding a VF00 destination also discarded FMAC flags");
        for(unsigned i=16u;i<32u;++i)if(data[i]!=0xa5u)throw std::runtime_error("VF00 fixture changed its destination guard");
    }
}
void verify_vector_rounding() {
#if defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__x86_64__)
    struct Restore {std::fenv_t saved;Restore(){std::fegetenv(&saved);}~Restore(){std::fesetenv(&saved);}} restore;
    auto memory=std::make_unique<PS2Memory>();if(!memory->initialize())throw std::runtime_error("Rounding fixture memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(memory->getGSVRAM(),PS2_GS_VRAM_SIZE,&memory->gs());
    auto vu=std::make_unique<VU1Interpreter>();auto* code=memory->getVU1Code();
    memory->write64(PS2_VU1_CODE_BASE,uint64_t(nop|0x40000000u)<<32u);
    memory->write64(PS2_VU1_CODE_BASE+8u,uint64_t(nop)<<32u);
    constexpr int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    constexpr unsigned flags[]={0u,1u,4u,8u,16u,32u,33u,63u};
    unsigned cases=0;
    for(unsigned x87=0;x87<4u;++x87)for(unsigned sse=0;sse<4u;++sse)for(auto sticky:flags){
        std::fenv_t held;std::feholdexcept(&held);std::fesetround(modes[x87]);
        const unsigned incoming=(_mm_getcsr()&~0x603fu)|0x1f80u|(sse<<13u)|sticky;
        _mm_setcsr(incoming);
        const int originalMode=std::fegetround();
        const int expectedMode=originalMode<0 ? FE_TOWARDZERO : originalMode;
        unsigned expectedSse=0u;while(expectedSse<4u && modes[expectedSse]!=expectedMode)++expectedSse;
        if(expectedSse==4u)throw std::runtime_error("Unsupported host rounding mode");
        vu->execute(code,PS2_VU1_CODE_SIZE,memory->getVU1Data(),PS2_VU1_DATA_SIZE,*gs,memory.get(),0u,0u,0u,8u);
        const unsigned outgoing=_mm_getcsr();
        if(std::fegetround()!=expectedMode || (outgoing&0x6000u)!=(expectedSse<<13u)
            || (outgoing&~0x6000u)!=(incoming&~0x6000u))
            throw std::runtime_error("Vector host floating-point controls changed: "+std::to_string(cases));
        ++cases;
    }
    if(cases!=128u)throw std::runtime_error("Vector rounding coverage incomplete");
#endif
}
void verify_vector_live_code() {
    auto memory=std::make_unique<PS2Memory>();if(!memory->initialize())throw std::runtime_error("Live-code fixture memory initialization failed");
    auto gs=std::make_unique<GS>();gs->init(memory->getGSVRAM(),PS2_GS_VRAM_SIZE,&memory->gs());
    auto vu=std::make_unique<VU1Interpreter>();vu->state().vi[1]=0;
    const uint64_t tag=0x8000u;std::memcpy(memory->getVU1Data(),&tag,8u);
    memory->write64(PS2_VU1_CODE_BASE,special(0x6cu,1u)|(uint64_t(nop)<<32u));
    memory->write64(PS2_VU1_CODE_BASE+8u,integer(2u,0u,1u)|(uint64_t(nop)<<32u));
    memory->write64(PS2_VU1_CODE_BASE+16u,integer(3u,0u,7u)|(uint64_t(nop)<<32u));
    unsigned callbacks=0u;
    memory->setGifPacketCallback([&](const uint8_t*,uint32_t count){
        if(count!=16u || ++callbacks!=1u)throw std::runtime_error("Live-code fixture received an invalid packet");
        memory->write64(PS2_VU1_CODE_BASE,uint64_t(0xffffffffu)<<32u);
        memory->write64(PS2_VU1_CODE_BASE+8u,integer(2u,0u,99u)|(uint64_t(nop|0x40000000u)<<32u));
    });
    vu->execute(memory->getVU1Code(),PS2_VU1_CODE_SIZE,memory->getVU1Data(),PS2_VU1_DATA_SIZE,*gs,memory.get(),0u,0u,0u,32u);
    if(callbacks!=1u || vu->state().vi[2]!=99 || vu->state().vi[3]!=7 || vu->state().pc!=24u)
        throw std::runtime_error("Vector fetch did not observe a host code update at its original instruction boundary");
}
void verify_vector_execution() {
    verify_vector_rounding();verify_vector_live_code();
    constexpr uint64_t expected[]={
        0xd6d2f3974219e088ull,0xc616eb640647885cull,0x301b4e8c9e01ead0ull,0x06043d9053426324ull,
        0x25ac3c709bd80ac0ull,0xe8c6742e3896b33eull,0x5cf63bffc0edef85ull,0xbea2dd534137754cull,
        0xcbcc894bf964acf9ull,0x3333f1486fde5ec3ull,0x8cc111540a577a72ull,0xcec6ab6854fc354eull,
        0x3a2cd5c050e01edbull,0xe08ecb6b3d78e0b2ull,0xb174863fa1f9f605ull,0xe49bdbf71e60ec6full,
        0xad58842c14205e27ull,0x22ced556849d84d8ull,0xbb2df8e4de11d3c6ull,0x9994336ef1cde997ull,
        0x415de44cd8e194bbull,0x15fca2942658fd81ull,0xcaf3ee6d07318a84ull,0xc9a8cf96832f0c82ull,
        0x61539dced9afb1cdull,0x2b75f008198493e7ull,0xd7a21b0767ac3158ull,0xace7168fbb2b50a7ull,
        0x719d22d92a758628ull,0xf193c3f21711b7c9ull,0xfedbc51e328854d7ull,0xadac8fc4d1e7d508ull,
        0x25ac3c709bd80ac0ull,0xe8c6742e3896b33eull,0x5cf63bffc0edef85ull,0xbea2dd534137754cull,
        0xc6db6a97ec59c4bcull,0x7feaf34ef99d45f4ull,0x11b0dc9e277428fdull,0x8c7f4f079c33b38full,
        0xcbcc894bf964acf9ull,0x3333f1486fde5ec3ull,0x8cc111540a577a72ull,0xcec6ab6854fc354eull,
        0xfe2f4b59c7f9ecf9ull,0x6e8b8322893c1ec3ull,0xeb2fbb3a7721fa72ull,0x9eb278847061b54eull,
        0xcbcc894bf964acf9ull,0x3333f1486fde5ec3ull,0x8cc111540a577a72ull,0xcec6ab6854fc354eull,
        0x87cb2418c6d0342dull,0x421b00b5e42eb267ull,0x1e34721aa4fce06eull,0xbedc8f06a70dddfaull,
        0x2d6dfc2a25e7b22full,0xa5cc272ca0ff1e6cull,0x743f87467f06fc56ull,0x08121ef347e4a2cbull,
        0xca2baa98f833e088ull,0x1990cbdf92c2885cull,0xf0f71375d6e5ead0ull,0xdff97389ae276324ull,
    };
    for(unsigned i=0;i<64u;++i){const auto actual=vector_workload(i/4u,3u+5u*(i%4u));
        if(actual.hash!=expected[i])throw std::runtime_error("Vector reference mismatch: "+std::to_string(i));
    }
}
}
