// SPDX-License-Identifier: GPL-3.0-only
#include "attack_effect_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace openkai2 {
void verify_attack_effects(PS2Runtime& runtime,uint8_t* ram) {
    unsigned currentSlot=0,currentKind=0,currentAllocation=0;
    auto require=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string(why)+" (slot="+std::to_string(currentSlot)+", kind="+std::to_string(currentKind)+", allocation="+std::to_string(currentAllocation)+")");};
    for(auto entry:translation::attack_effect_entries)require(runtime.hasFunction(entry),"Attack effect method is missing");
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto i=entries.rbegin();i!=entries.rend();++i)runtime.replaceFunction(i->first,i->second);}
        void replace(uint32_t at,PS2Runtime::RecompiledFunction fn){entries.emplace_back(at,runtime.lookupFunction(at));runtime.replaceFunction(at,fn);}
    } restore{runtime,{}};
    constexpr uint32_t node=0x660000u,packet=0x650000u,item=0x640000u,object=0x670000u,bridge=0x1908f0u;
    constexpr uint32_t returnPc=0x20ab6cu,sp=0x1fffcd0u;
    constexpr uint32_t dependencies[]={0x21c8f0u,0x123c18u,0x20b340u,0x21bfc0u,0x21c600u,0x13c300u,0x149e60u,0x1453b0u,0x21c3d0u,0x149e00u};
    for(auto at:dependencies)restore.replace(at,[](uint8_t* data,R5900Context* c,PS2Runtime* r){
        auto& m=r->memory();uint32_t result=0u;
        if(c->pc==0x21c8f0u){m.write32(0x29808u,m.read32(0x29808u)+1u);result=m.read32(0x29800u) ? node : 0u;}
        else if(c->pc==0x123c18u){
            if(getRegU32(c,4)!=node || getRegU32(c,5)!=0u || getRegU32(c,6)!=0x80u)throw std::runtime_error("Attack effect changed its allocation clear");
            std::memset(data+node,0,0x80u);result=node;
        }else if(c->pc==0x20b340u)result=0x683000u;
        else if(c->pc==0x21bfc0u)result=object;
        else result=getRegU32(c,4);
        c->r[2]=_mm_cvtsi32_si128(result);c->pc=getRegU32(c,31);
    });
    restore.replace(bridge,nullptr);auto& m=runtime.memory();unsigned cases=0u;
    for(unsigned slot=0;slot<19u;++slot)for(unsigned allocate=0;allocate<2u;++allocate)for(unsigned kind=0;kind<3u;++kind){
        currentSlot=slot;currentKind=kind;currentAllocation=allocate;
        std::memset(ram+0x640000u,0,0x50000u);std::memset(ram+0x29800u,0,64u);
        std::memset(ram+node-16u,0xa5,0xa0u);std::memset(ram+sp-0x200u,0xa5,0x210u);
        m.write32(0x29800u,allocate);m.write32(item+0x1cu,slot);
        m.write32(0x621500u,0x621500u);m.write32(0x621504u,0x621500u);
        m.write32(0x3a4d10u,0x680000u);m.write32(0x680000u,0x681000u);m.write32(0x681010u,0x682000u);
        m.write32(0x682068u,kind);m.write32(0x68206cu,0u);
        m.write32(0x3df7d0u,object);m.write32(0x68300cu,0u);m.write32(0x683014u,object);m.write32(0x6215a0u,object);
        m.write32(0x3a4cb4u,0x684000u);
        for(unsigned i=0;i<16u;++i)m.write32(packet+i*4u,0x3f800000u);
        m.write16(packet+0x24u,7u);m.write16(packet+0x26u,0u);m.write16(packet+0x28u,11u);
        m.write16(packet+0x2au,13u);m.write16(packet+0x2cu,17u);m.write16(packet+0x2eu,1u);m.write16(packet+0x30u,2u);m.write16(packet+0x32u,0u);
        std::array<uint8_t,64> input{};std::memcpy(input.data(),ram+packet,input.size());
        for(unsigned i=0;i<20u;++i)m.write32(0x3a21a0u+i*4u,translation::attack_effect_slots[i]);
        const auto target=translation::attack_effect_slots[slot];const auto original=runtime.lookupFunction(target);
        runtime.replaceFunction(bridge,original);restore.replace(target,[](uint8_t* data,R5900Context* c,PS2Runtime* r){
            r->memory().write32(0x29804u,c->pc);r->lookupFunction(bridge)(data,c,r);throw EeDispatcherTransfer{};
        });
        R5900Context c{};c.pc=0x20ab54u;c.r[2]=_mm_cvtsi32_si128(0x3a21a0u);c.r[4]=_mm_cvtsi32_si128(packet);c.r[20]=_mm_cvtsi32_si128(item);
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(0x20aab0u);
        for(unsigned i=16;i<=23u;++i)if(i!=20u)c.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);
        const auto saved=c;
        for(unsigned step=0;step<256u && c.pc!=returnPc;++step){
            require(c.pc!=0u,"Attack effect reached a null continuation");
            const auto fn=step==0u ? runtime.lookupFunction(0x20a870u) : runtime.lookupFunction(c.pc);
            require(fn!=nullptr,"Attack effect reached a missing continuation");try{fn(ram,&c,&runtime);}catch(const EeDispatcherTransfer&){}
        }
        runtime.replaceFunction(target,original);restore.entries.pop_back();
        require(c.pc==returnPc && m.read32(0x29804u)==target,"Attack effect did not return through its original call site");
        require(m.read32(0x29808u)==1u,"Attack effect changed its allocation count");
        if(allocate){
            const uint32_t callback=target==0x208410u ? 0x2082f0u : target==0x2086f0u ? 0x208630u : target==0x208ab0u ? 0x208920u : target==0x208f10u ? 0x208e70u : target==0x2091a0u ? 0x209140u : target==0x209560u ? 0x2093f0u : 0x209770u;
            require(m.read32(node)==0x621500u && m.read32(node+4u)==0x621500u && m.read32(0x621500u)==node && m.read32(0x621504u)==node,"Attack effect changed its list links");
            require(m.read32(node+8u)==callback && runtime.hasFunction(callback),"Attack effect installed an unregistered callback");
            require(m.read16(node+0x1au)==16u && m.read16(node+0x1cu)==13u && m.read16(node+0x1eu)==17u,"Attack effect changed its node parameters");
        }else require(m.read32(0x621500u)==0x621500u && m.read32(0x621504u)==0x621500u,"Failed attack allocation changed its list");
        require(std::memcmp(input.data(),ram+packet,input.size())==0,"Attack effect changed its input packet");
        require(getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u && getRegU32(&c,31)==returnPc,"Attack effect lost its caller frame");
        for(unsigned i=16;i<=23u;++i)require(std::memcmp(&c.r[i],&saved.r[i],16u)==0,"Attack effect changed a saved register");
        for(unsigned i=0;i<16u;++i)require(ram[node-16u+i]==0xa5u && ram[node+0x80u+i]==0xa5u,"Attack effect changed allocation guards");
        ++cases;
    }
    require(cases==114u,"Attack effect constructor coverage incomplete");
}
}
