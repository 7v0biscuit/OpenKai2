// SPDX-License-Identifier: GPL-3.0-only
#include "ps2_runtime.h"
#include "runtime_layout.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <vector>
namespace openkai2 {
namespace {
constexpr uint32_t nop=0x2ffu;
constexpr uint32_t kick=0x80000efcu;
uint32_t integer(unsigned reg,unsigned value) {return 0x10000000u|(reg<<16u)|(value&2047u);}
void upload_program(PS2Memory& memory,unsigned first,unsigned second) {
    const std::array<uint32_t,13> packet={
        0x4a060000u,kick,nop,integer(2u,first),nop|0x40000000u,0u,nop,
        kick,nop,integer(2u,second),nop|0x40000000u,0u,nop};
    memory.processVIF1Data(reinterpret_cast<const uint8_t*>(packet.data()),unsigned(sizeof(packet)));
}
void triangle_packet(uint8_t* data,uint32_t color) {
    const std::array<std::array<uint64_t,2>,12> registers={{{0x10000u,0x4cu},
        {128u|(1ull<<32u),0x4eu},{0u,0x18u},{(63ull<<16u)|(63ull<<48u),0x40u},
        {0x30000u,0x47u},{1u,0x46u},{1u,0x1au},{3u,0u},
        {uint64_t(color)|(0x3f800000ull<<32u),1u},
        {128u|(128ull<<16u)|(5ull<<32u),5u},
        {640u|(160ull<<16u)|(5ull<<32u),5u},
        {160u|(640ull<<16u)|(5ull<<32u),5u}}};
    const uint64_t tag=registers.size()|0x8000u|(1ull<<60u),format=0xeu;
    std::memcpy(data,&tag,8u);std::memcpy(data+8u,&format,8u);
    std::memcpy(data+16u,registers.data(),sizeof(registers));
}
void upload_triangle(PS2Memory& memory,uint32_t color) {
    std::array<uint8_t,212> packet{};const uint32_t command=0x6c0d0000u;
    std::memcpy(packet.data(),&command,4u);triangle_packet(packet.data()+4u,color);
    memory.processVIF1Data(packet.data(),unsigned(packet.size()));
}
uint32_t pixel(PS2Runtime& runtime,unsigned x,unsigned y) {
    return runtime.gs().ReadVram(GS_PSM_CT32,0u,1u,x,y);
}
}
void verify_vector_runtime_impl(bool synchronize) {
    verify_runtime_layout(sizeof(VU1Interpreter),sizeof(PS2Runtime));
    auto runtime=std::make_unique<PS2Runtime>();auto& memory=runtime->memory();
    if(!memory.initialize() || !runtime->syncCoreSubsystems())throw std::runtime_error("Runtime VIF1 fixture initialization failed");
    for(unsigned variant=0;variant<4u;++variant){
        runtime->vu0().reset();runtime->vu1().reset();
        std::memset(memory.getGSVRAM(),0,PS2_GS_VRAM_SIZE);
        const uint32_t color=0x80000000u|((variant*53u+37u)<<16u)|((variant*31u+67u)<<8u)|(variant*17u+89u);
        upload_triangle(memory,color);upload_program(memory,17u+variant,29u+variant);
        runtime->vu1().state().vi[1]=0;
        const uint32_t mscal=0x14000000u,mscnt=0x17000000u;
        memory.processVIF1Data(reinterpret_cast<const uint8_t*>(&mscal),4u);
        if(synchronize)runtime->gs().latchHostPresentationFrame();
        if(runtime->vu1().state().vi[2]!=int(17u+variant) || runtime->vu1().state().pc!=24u
            || pixel(*runtime,12u,12u)!=color || pixel(*runtime,63u,63u)!=0u)
        {
            std::ostringstream error;error<<"Runtime VIF1 MSCAL did not render its PATH1 triangle: vi2="<<runtime->vu1().state().vi[2]
                <<" pc="<<std::hex<<runtime->vu1().state().pc<<" pixel="<<pixel(*runtime,12u,12u)<<" expected="<<color
                <<" guard="<<pixel(*runtime,63u,63u)<<" cycles="<<runtime->vu1().state().cycles;
            throw std::runtime_error(error.str());
        }
        std::array<uint8_t,sizeof(VU1State)> saved{};
        std::memcpy(saved.data(),&runtime->vu1().state(),saved.size());
        memory.write64(PS2_VU0_CODE_BASE,integer(5u,99u)|(uint64_t(nop|0x40000000u)<<32u));
        memory.write64(PS2_VU0_CODE_BASE+8u,uint64_t(nop)<<32u);
        runtime->vu0StartMicroProgram(memory.getRDRAM(),&runtime->cpu(),0u);
        if(runtime->cpu().vi[5]!=99u || std::memcmp(saved.data(),&runtime->vu1().state(),saved.size())!=0)
            throw std::runtime_error("Runtime VU0 execution changed the embedded VU1 state");
        const uint32_t second=color^0x00ffffffu;upload_triangle(memory,second);
        memory.processVIF1Data(reinterpret_cast<const uint8_t*>(&mscnt),4u);
        if(synchronize)runtime->gs().latchHostPresentationFrame();
        if(runtime->vu1().state().vi[2]!=int(29u+variant) || runtime->vu1().state().pc!=48u
            || pixel(*runtime,12u,12u)!=second || pixel(*runtime,63u,63u)!=0u)
            throw std::runtime_error("Runtime VIF1 MSCNT did not resume PATH1 triangle rendering after VU0 execution");
    }
}
void verify_vector_runtime(){verify_vector_runtime_impl(false);}
void verify_vector_runtime_gpu(){verify_vector_runtime_impl(true);}

}
