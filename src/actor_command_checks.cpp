// SPDX-License-Identifier: GPL-3.0-only
#include "actor_command_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace openkai2 {
void verify_actor_commands(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    for(auto at:translation::actor_command_entries)require(runtime.hasFunction(at),"Actor command entry is missing");
    constexpr uint32_t context=0x650000u,state=0x660000u,descriptor=0x670000u,actor=0x680000u,control=0x690000u;
    constexpr uint32_t marker=0x29a00u,returned=0x29f00u,sp=0x1fffde0u;
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto i=entries.rbegin();i!=entries.rend();++i)runtime.replaceFunction(i->first,i->second);}
        void replace(uint32_t at,PS2Runtime::RecompiledFunction fn){entries.emplace_back(at,runtime.lookupFunction(at));runtime.replaceFunction(at,fn);}
    } restore{runtime,{}};
    auto& m=runtime.memory();
    const auto run=[&](R5900Context& c){for(unsigned step=0;step<128u && c.pc!=returned;++step){
        const auto fn=runtime.lookupFunction(c.pc);require(fn!=nullptr,"Actor command reached an untranslated continuation");
        try{fn(ram,&c,&runtime);}catch(const EeDispatcherTransfer&){}
    }require(c.pc==returned,"Actor command did not return");};
    restore.replace(0x1b6520u,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        auto& memory=r->memory();uint32_t a,b;std::memcpy(&a,&c->f[12],4u);std::memcpy(&b,&c->f[13],4u);
        if(a!=memory.read32(marker+4u) || b!=memory.read32(marker+8u) || getRegU32(c,4)!=0u)
            throw std::runtime_error("Actor orientation helper arguments changed");
        const auto result=memory.read32(marker+12u);std::memcpy(&c->f[0],&result,4u);
        memory.write32(marker,memory.read32(marker)+1u);c->pc=getRegU32(c,31);
    });
    unsigned cases=0u;
    for(unsigned index=0;index<4u;++index)for(unsigned angle=0;angle<4u;++angle)for(auto alias:{0u,0x80000000u,0xa0000000u}){
        std::memset(ram+context,0,0x70000u);std::memset(ram+sp-0x100u,0xa5u,0x110u);std::memset(ram+marker,0,64u);
        m.write32(state+0x2cu,descriptor|alias);m.write32(descriptor,index);m.write32(0x3a4cb4u,control|alias);
        m.write32(control+0x19c54u+index*4u,actor|alias);
        const float input0=float(index)-1.5f,input1=float(angle)*0.5f-0.75f,base=10.0f+float(index),offset=float(angle)*0.25f-0.5f;
        std::memcpy(ram+actor+0x4b0u,&input0,4u);std::memcpy(ram+actor+0x4b8u,&input1,4u);std::memcpy(ram+actor+0x44u,&base,4u);
        std::memcpy(ram+marker+4u,&input0,4u);std::memcpy(ram+marker+8u,&input1,4u);std::memcpy(ram+marker+12u,&offset,4u);
        const auto before=std::array<uint8_t,4>{ram[state+0x3fu],ram[state+0x44u],ram[descriptor],ram[actor]};
        R5900Context c{};c.pc=0x1fb010u;c.r[4]=_mm_cvtsi32_si128(context|alias);c.r[5]=_mm_cvtsi32_si128(state|alias);
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(returned);
        c.r[16]=_mm_set_epi32(0x1111,0x2222,0x3333,0x4444);c.r[17]=_mm_set_epi32(0x5555,0x6666,0x7777,0x8888);const auto saved=c;
        run(c);float actual;std::memcpy(&actual,ram+state+0x40u,4u);
        require(actual==base-offset && m.read32(marker)==1u && getRegU32(&c,2)==1u,"Actor orientation callback changed its result");
        require(getRegU32(&c,29)==sp && getRegU32(&c,31)==returned && getRegU32(&c,28)==0x3ac4f0u,"Actor callback lost its caller frame");
        require(std::memcmp(&c.r[16],&saved.r[16],32u)==0,"Actor callback changed saved registers");
        require(before==std::array<uint8_t,4>{ram[state+0x3fu],ram[state+0x44u],ram[descriptor],ram[actor]},"Actor callback wrote outside its result");
        for(unsigned i=0;i<16u;++i)require(ram[sp-0x100u+i]==0xa5u && ram[sp+i]==0xa5u,"Actor callback changed stack guards");
        ++cases;
    }
    // Run the original caller's command-selection continuation with each bank
    // method recorded independently. Return zero to finish its current command.
    for(auto at:translation::actor_command_entries)restore.replace(at,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        auto& memory=r->memory();
        if(c->pc!=memory.read32(marker+4u) || getRegU32(c,4)!=memory.read32(marker+8u) || getRegU32(c,5)!=memory.read32(marker+12u))
            throw std::runtime_error("Actor command dispatcher selected a wrong callback or arguments");
        memory.write32(marker,memory.read32(marker)+1u);c->r[2]=_mm_setzero_si128();c->pc=getRegU32(c,31);
    });
    for(unsigned slot=0;slot<40u;++slot)for(auto alias:{0u,0x80000000u,0xa0000000u}){
        std::memset(ram+context,0,0x70000u);std::memset(ram+sp-0x100u,0xa5u,0x110u);std::memset(ram+marker,0,64u);
        m.write32(context,descriptor|alias);m.write32(descriptor+0x44u,control|alias);
        for(unsigned i=0;i<3u;++i)m.write32(control+i*4u,0x375550u+i*64u);
        for(unsigned i=0;i<40u;++i)m.write32(0x375550u+i*4u,translation::actor_command_slots[i]);
        // Case zero of the original caller's return switch exits through its epilogue.
        m.write32(0x37fdd0u,0x14c9e0u);
        m.write32(marker+4u,translation::actor_command_slots[slot]);m.write32(marker+8u,context|alias);m.write32(marker+12u,state|alias);
        R5900Context c{};c.pc=0x14c978u;c.r[5]=_mm_cvtsi32_si128(((slot/16u)<<12u)|((slot%16u)<<8u)|7u);
        c.r[16]=_mm_cvtsi32_si128(state|alias);c.r[17]=_mm_cvtsi32_si128(context|alias);c.r[18]=_mm_cvtsi32_si128(context-0x30u);
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp-0x50u);c.r[31]=_mm_cvtsi32_si128(returned);
        const auto saved0=_mm_set_epi32(1,2,3,4),saved1=_mm_set_epi32(5,6,7,8),saved2=_mm_set_epi32(9,10,11,12);
        m.write128(sp-0x50u,saved0);m.write128(sp-0x40u,saved1);m.write128(sp-0x30u,saved2);m.write64(sp-0x20u,returned);m.write32(sp-4u,0x6b0000u|alias);
        run(c);require(m.read32(marker)==1u && m.read32(state+0x24u)==(0x6b0000u|alias),"Actor command dispatcher did not preserve its stream position");
        require(getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u && getRegU32(&c,31)==returned,"Actor dispatcher lost its caller frame");
        require(std::memcmp(&c.r[16],&saved0,16u)==0 && std::memcmp(&c.r[17],&saved1,16u)==0 && std::memcmp(&c.r[18],&saved2,16u)==0,"Actor dispatcher changed saved registers");
        for(unsigned i=0;i<16u;++i)require(ram[sp-0x100u+i]==0xa5u && ram[sp+i]==0xa5u,"Actor dispatcher changed stack guards");
        ++cases;
    }
    require(cases==168u,"Actor command coverage incomplete");
}
}
