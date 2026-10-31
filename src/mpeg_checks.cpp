// SPDX-License-Identifier: GPL-3.0-only
#include "mpeg_checks.h"
#include "ps2_runtime.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sstream>

namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime MPEG setup regression: ")+message);
}
}
void verify_mpeg_setup(PS2Runtime& runtime,uint8_t* memory) {
    constexpr uint32_t object=0x00290000u,work=0x002a0000u,capacity=0x4000u;
    constexpr uint32_t backend=0x002b0000u,stream=0x002c0000u,event=0x002d0000u;
    constexpr uint32_t gp=0x003ac4f0u,stack=0x01ffff60u,returned=0x0019de98u;
    auto put=[&](uint32_t address,uint32_t value) { runtime.memory().write32(address,value); };
    auto word=[&](uint32_t address) { return runtime.memory().read32(address); };
    auto invoke=[&](uint32_t entry,std::array<uint32_t,4> args,uint32_t call_gp=0x003ac4f0u) {
        R5900Context context{};
        context.pc=entry;
        for (unsigned i=0;i<4;++i) context.r[4+i]=_mm_cvtsi32_si128(args[i]);
        context.r[28]=_mm_cvtsi32_si128(call_gp);
        context.r[29]=_mm_cvtsi32_si128(stack); context.r[31]=_mm_cvtsi32_si128(returned);
        for (unsigned reg=16;reg<=23;++reg)
            context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
        constexpr uint32_t finish=returned;
        const auto saved=context;
        for (unsigned count=0;count<512u && context.pc!=finish;++count) {
            if (!runtime.hasFunction(context.pc)) {
                std::ostringstream message;
                message << "Runtime MPEG setup regression: entry=0x" << std::hex << entry
                        << " reached unregistered pc=0x" << context.pc;
                throw std::runtime_error(message.str());
            }
            runtime.lookupFunction(context.pc)(memory,&context,&runtime);
        }
        require(context.pc==finish && getRegU32(&context,29)==stack
                && getRegU32(&context,28)==call_gp && getRegU32(&context,31)==finish,
                "SDK setup did not restore the caller frame");
        for (unsigned reg=16;reg<=23;++reg)
            require(std::memcmp(&context.r[reg],&saved.r[reg],16)==0,"SDK setup changed a saved register");
        return getRegU32(&context,2);
    };
    const std::array<uint32_t,3> unrelated{0x001717bcu,0x00171800u,0x00171904u};
    for (auto address:unrelated) std::memset(memory+address,0xa5,64);
    auto guards=[&] {
        for (unsigned i=0;i<16;++i)
            require(memory[object-16+i]==0xa5 && memory[object+0x48u+i]==0xa5
                    && memory[work-16+i]==0xa5 && memory[work+capacity+i]==0xa5,
                    "SDK setup changed neighboring memory");
        for (auto address:unrelated) for (unsigned i=0;i<64;++i)
            require(memory[address+i]==0xa5,"SDK setup wrote a foreign game's global addresses");
    };
    for (uint32_t alignment:{0u,1u,3u}) {
        std::memset(memory+object-16,0xa5,0x48+32);
        std::memset(memory+object,0x66,0x48);
        std::memset(memory+work-16,0xa5,capacity+32);
        require(invoke(0x00107978u,{object,work+alignment,capacity-alignment,0u})==1u,
                "original constructor did not return SDK success");
        const uint32_t inner=(work+alignment+3u)&~3u;
        require(word(object+0x40u)==inner && word(inner+0x870u)==object,
                "MPEG work area is not linked to its public handle");
        const uint32_t allocatedEnd=((inner+0x10c0u+7u)&~7u)+0x800u;
        require(word(inner+0x120u)==inner+0x10c0u
                && word(inner+0x124u)==work+capacity-inner-0x10c0u
                && word(inner+0x128u)==allocatedEnd
                && word(inner+0x12cu)==allocatedEnd
                && word(inner+0x24u)==0x00109018u && word(inner+0x30u)==0x00109040u,
                "MPEG allocator or default callbacks use the wrong SDK version");
        require(word(inner+0x1d0u)==inner+0x200u && word(inner+0x1e0u)==inner+0x338u
                && word(inner+0x1f4u)==inner+0x4d8u && word(inner+0x834u)==0x70003600u,
                "MPEG reference buffers or scratchpad pointer are wrong");
        guards();
        constexpr std::array<uint32_t,4> callbacks{0x00198200u,0x00198308u,0x00198388u,0x001983b0u};
        for (unsigned slot=0;slot<callbacks.size();++slot) {
            const uint32_t previous=word(inner+0x0cu+12u*slot);
            require(invoke(0x00107dd0u,{object,slot,callbacks[slot],backend},gp+0x100u)==previous,
                    "callback registration did not return the previous function");
            require(word(inner+0x0cu+12u*slot)==callbacks[slot]
                    && word(inner+0x10u+12u*slot)==backend
                    && word(inner+0x14u+12u*slot)==gp+0x100u,
                    "original callback/data/GP record was not installed");
        }
        std::memset(memory+backend,0,0x1b40u); std::memset(memory+stream,0,0x1000u);
        put(backend+0x1b30u,stream); put(event,1u);
        require(invoke(0x00107e08u,{object,event,0u,0u})==1u,
                "original dispatcher did not reach the compiled Sofdec no-data callback");
        for (uint32_t offset:{0x200u,0x268u,0x338u,0x3a0u,0x470u,0x4d8u}) put(inner+offset+0x28u,0xdeadbeefu);
        require(invoke(0x00107d78u,{object,0u,0u,0u})==1u,"reference clear failed");
        for (uint32_t offset:{0x200u,0x268u,0x338u,0x3a0u,0x470u,0x4d8u})
            require(word(inner+offset+0x28u)==0u,"reference clear used foreign global pointers");
        put(object+8u,123u); put(inner+0x10a0u,123u); put(inner+0xc4u,123u);
        require(invoke(0x00107d18u,{object,0u,0u,0u})==1u
                && word(object+8u)==0u && word(inner+0x10a0u)==0u && word(inner+0xc4u)==0u
                && word(inner+0x830u)==1u && word(inner+0x98u)==0xffffffffu,
                "MPEG reset used the wrong work-area offsets");
        require(word(inner+0x18u)==callbacks[1] && word(inner+0x20u)==gp+0x100u,
                "reset discarded the registered no-data callback or GP");
        guards();
    }
}
}
