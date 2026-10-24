// SPDX-License-Identifier: GPL-3.0-only
#include "impact_effect_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace openkai2 {
void verify_impact_effects(PS2Runtime& runtime,uint8_t* ram) {
    unsigned currentSlot=0,currentVariant=0,currentAllocation=0;
    auto require=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string(why)+" (slot="+std::to_string(currentSlot)+", variant="+std::to_string(currentVariant)+", allocation="+std::to_string(currentAllocation)+")");};
    for(auto at:translation::impact_effect_entries)require(runtime.hasFunction(at),"Impact effect method is missing");
    constexpr uint32_t node=0x650000u,payload=node+0x20u,descriptor=0x660000u,resource=0x670000u,actor=0x680000u,control=0x690000u;
    constexpr uint32_t sp=0x1fffde0u,returned=0x29f00u;
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto i=entries.rbegin();i!=entries.rend();++i)runtime.replaceFunction(i->first,i->second);}
        void replace(uint32_t at,PS2Runtime::RecompiledFunction fn){entries.emplace_back(at,runtime.lookupFunction(at));runtime.replaceFunction(at,fn);}
    } restore{runtime,{}};
    constexpr uint32_t dependencies[]={0x2062d0u,0x20b960u,0x20b8e0u,0x149e40u,0x149e00u,0x20b340u,0x21bfc0u,0x149e90u,0x1453b0u,0x21c2d0u,0x149e60u,0x20b3b0u,0x21c600u};
    for(auto at:dependencies)restore.replace(at,[](uint8_t* data,R5900Context* c,PS2Runtime* r){
        auto& m=r->memory();uint32_t result=0u;
        if(c->pc==0x2062d0u){
            if(getRegU32(c,5)!=1u || getRegU32(c,4)!=(m.read32(0x29804u)&1u))throw std::runtime_error("Impact factory changed allocator arguments");
            m.write32(0x2980cu,m.read32(0x2980cu)+1u);
            if(m.read32(0x29800u)){result=node;std::memset(data+node,0,0x200u);m.write16(node+0x24u,uint16_t(0x42u));}
        }else if(c->pc==0x20b340u)result=0x6a0000u;
        else if(c->pc==0x21bfc0u){m.write32(0x29808u,m.read32(0x29808u)+1u);result=resource;}
        else if(c->pc==0x20b3b0u)result=resource;
        else if(c->pc!=0x21c2d0u && c->pc!=0x21c600u){
            const auto out=getRegU32(c,4);
            if(out<node || (out>=control+0x10000u && !(out>=sp-0x200u && out<sp)))throw std::runtime_error("Impact dependency received an invalid output");
            const float value[4]={1.0f,2.0f,3.0f,1.0f};std::memcpy(data+out,value,16u);result=out;
        }
        c->r[2]=_mm_cvtsi32_si128(result);c->pc=getRegU32(c,31);
    });
    auto& m=runtime.memory();unsigned cases=0u;
    for(unsigned slot=0;slot<7u;++slot)for(unsigned allocate=0;allocate<2u;++allocate)for(unsigned variant=0;variant<8u;++variant){
        currentSlot=slot;currentVariant=variant;currentAllocation=allocate;
        std::memset(ram+node,0,0x60000u);std::memset(ram+node-16u,0xa5u,16u);std::memset(ram+node+0x200u,0xa5u,16u);
        std::memset(ram+sp-0x400u,0xa5,0x410u);std::memset(ram+0x29800u,0,64u);
        m.write32(0x29800u,allocate);m.write32(0x29804u,variant);
        m.write32(actor+0xf9cu,variant&1u);m.write32(0x3a4654u,control);m.write32(control,0x3f800000u);
        m.write16(control+(variant&1u)*1248u+0x12au,(variant&2u) ? 1u : 0u);
        m.write32(descriptor+8u,slot);m.write32(descriptor+0xcu,0x42340000u);m.write32(descriptor+0x10u,0x3f000000u);
        m.write32(descriptor+0x18u,120u+variant*10u);m.write32(descriptor+0x24u,variant&1u);m.write32(descriptor+0x28u,(variant>>2u)&1u);
        m.write32(descriptor+0x54u,0x3e800000u);
        m.write32(0x6a0008u,0u);m.write32(0x6a0014u,resource);m.write32(0x6a0018u,resource);
        m.write32(0x3a4cb4u,actor);
        std::array<uint8_t,128> savedDescriptor{};std::memcpy(savedDescriptor.data(),ram+descriptor,savedDescriptor.size());
        for(unsigned i=0;i<8u;++i)m.write32(0x3772b0u+i*4u,translation::impact_create_slots[i]);
        R5900Context c{};c.pc=0x2113d0u;c.r[4]=_mm_cvtsi32_si128(actor);c.r[5]=_mm_cvtsi32_si128(descriptor);
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(returned);
        c.vu0_vf[0]=_mm_set_ps(1.0f,0.0f,0.0f,0.0f);
        for(unsigned i=16;i<=23u;++i)c.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);
        const auto saved=c;
        for(unsigned step=0;step<256u && c.pc!=returned;++step){
            require(c.pc!=0u,"Impact factory reached a null continuation");const auto fn=runtime.lookupFunction(c.pc);
            require(fn!=nullptr,"Impact factory reached a missing continuation");try{fn(ram,&c,&runtime);}catch(const EeDispatcherTransfer&){}
        }
        require(c.pc==returned && m.read32(0x2980cu)==1u,"Impact factory did not return through its original frame");
        if(allocate){
            require(m.read32(node+8u)==0x2112f0u && m.read32(node+0xcu)==0x2112c0u,"Impact factory changed lifecycle callbacks");
            require(m.read32(node+0x18u)==0xbf800000u && m.read32(payload)==0x42340000u,"Impact factory changed lifetime fields");
            require(m.read32(payload+8u)==actor && m.read32(payload+0xcu)==descriptor,"Impact factory changed its actor or descriptor");
            if(m.read16(payload+4u)!=0x1c42u || m.read16(payload+6u)!=((120u+variant*10u)+((variant&2u) ? 5u*((120u+variant*10u)/10u) : 0u)))
                throw std::runtime_error("Impact factory flags/parameter mismatch: slot="+std::to_string(slot)+", variant="+std::to_string(variant)+", flags="+std::to_string(m.read16(payload+4u))+", index="+std::to_string(m.read16(payload+6u)));
            require(m.read32(0x29808u)>0u,"Impact constructor did not bind any resources");
        }else require(m.read32(node+8u)==0u && m.read32(0x29808u)==0u,"Failed impact allocation initialized an effect");
        require(std::memcmp(savedDescriptor.data(),ram+descriptor,savedDescriptor.size())==0,"Impact constructor changed its descriptor");
        require(getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u && getRegU32(&c,31)==returned,"Impact factory lost its caller frame");
        for(unsigned i=16;i<=23u;++i)require(std::memcmp(&c.r[i],&saved.r[i],16u)==0,"Impact constructor changed a saved register");
        for(unsigned i=0;i<16u;++i)require(ram[node-16u+i]==0xa5u && ram[node+0x200u+i]==0xa5u && ram[sp-0x400u+i]==0xa5u && ram[sp+i]==0xa5u,"Impact constructor changed memory guards");
        ++cases;
    }
    require(cases==112u,"Impact factory coverage incomplete");
}
}
