// SPDX-License-Identifier: GPL-3.0-only
#include "aura_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace openkai2 {
void verify_aura_updates(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(auto entry:translation::aura_update_entries)
        require(runtime.hasFunction(entry),"Aura update callback is not registered");
    constexpr uint32_t dependency=0x21bb00u,bridge=0x1908e8u,sp=0x1fffef0u;
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto& entry:entries)runtime.replaceFunction(entry.first,entry.second);}
    } restore{runtime,{{dependency,runtime.lookupFunction(dependency)},{bridge,runtime.lookupFunction(bridge)}}};
    // Bound only the model submission dependency. Run each original method
    // through the actual table selector and JALR, stopping at its return site
    // before the dispatcher advances into unrelated character assets.
    runtime.replaceFunction(dependency,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        auto& m=r->memory();const auto n=m.read32(0x29400u);
        m.write32(0x29404u+n*4u,getRegU32(c,4));m.write32(0x29400u,n+1u);c->pc=getRegU32(c,31);
    });
    auto& m=runtime.memory();
    auto putFloat=[&](uint32_t address,float f){uint32_t bits;std::memcpy(&bits,&f,4);m.write32(address,bits);};
    auto readFloat=[&](uint32_t address){auto bits=m.read32(address);float f;std::memcpy(&f,&bits,4);return f;};
    for(unsigned bank=0;bank<2u;++bank)for(unsigned slot=0;slot<3u;++slot)
    for(unsigned character=0;character<2u;++character)for(unsigned active=0;active<4u;++active)
    for(bool special:{false,true})for(float progress:{-0.5f,0.0f,0.5f})for(bool expired:{false,true}) {
        std::memset(ram+0x29400u,0,0x40u);std::memset(ram+0x622070u,0,0x100u);
        std::memset(ram+0x600000u,0,0x1000u);std::memset(ram+0x610000u,0xa5,0x2000u);
        std::memset(ram+sp-0x50u,0xa5,0x60u);
        for(unsigned i=0;i<4u;++i){m.write32(0x377330u+i*4u,translation::aura_primary_slots[i]);
            m.write32(0x377340u+i*4u,translation::aura_secondary_slots[i]);}
        const auto state=0x6220b0u+character*96u;
        const auto descriptor=(bank ? 0x377610u : 0x377350u)+character*176u;
        const auto auxiliary=0x3774b0u+character*176u;
        const auto destination=0x610000u+character*0x1000u+(bank ? 0x100u : 0u);
        std::memset(ram+descriptor,0,176u);std::memset(ram+auxiliary,0,176u);
        m.write32(descriptor+0x24u,destination);putFloat(descriptor+0x54u,17.0f);putFloat(descriptor+0x5cu,19.0f);
        putFloat(auxiliary+0x5cu,23.0f);putFloat(state+4u,progress);putFloat(state+8u,0.5f);
        m.write32(state+0xcu,special ? 2u : 0u);
        m.write8(state+0x14u,slot);m.write8(state+0x18u,slot);m.write8(state+0x15u,character);
        m.write8(state+0x16u,(active&1u) ? 1u : 0xffu);
        m.write8(state+0x17u,(active&2u) ? 1u : 0xffu);
        m.write8(state+0x19u,(active&1u) ? 1u : 0xffu);
        for(unsigned i=0;i<3u;++i){const auto object=0x600000u+character*0x400u+i*0x100u;
            m.write32(state+0x1cu+i*4u,object);m.write32(object+0x38u,expired ? 0x80000000u : 1u);}
        putFloat(0x3777a0u+character*8u,2.0f+character);putFloat(0x3777a4u+character*8u,4.0f);
        std::array<uint32_t,16> expectedMatrix;
        for(unsigned i=0;i<16u;++i){float f=float(i+1u);putFloat(0x377800u+i*4u,f);
            std::memcpy(&expectedMatrix[i],&f,4);}
        const auto target=bank ? translation::aura_secondary_slots[slot] : translation::aura_primary_slots[slot];
        const auto original=runtime.lookupFunction(target);restore.entries.emplace_back(target,original);
        runtime.replaceFunction(bridge,original);
        runtime.replaceFunction(target,[](uint8_t* data,R5900Context* c,PS2Runtime* r){
            r->lookupFunction(bridge)(data,c,r);throw EeDispatcherTransfer{};
        });
        R5900Context c{};c.pc=bank ? 0x2124a4u : 0x212478u;
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);
        for(unsigned i=16;i<=23u;++i)c.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);
        c.r[19]=_mm_cvtsi32_si128(state);c.r[20]=_mm_cvtsi32_si128(character);
        const auto saved=c;
        try{runtime.lookupFunction(c.pc)(ram,&c,&runtime);}catch(const EeDispatcherTransfer&){}
        runtime.replaceFunction(target,original);restore.entries.pop_back();
        require(c.pc==(bank ? 0x2124c4u : 0x212498u) && getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u,
            "Aura callback did not return to its original indirect caller");
        for(unsigned i=16;i<=23u;++i)require(std::memcmp(&saved.r[i],&c.r[i],16u)==0,
            "Aura callback changed a saved register");
        require(std::all_of(ram+sp-0x50u,ram+sp-0x30u,[](uint8_t v){return v==0xa5u;})
            && std::all_of(ram+sp,ram+sp+0x10u,[](uint8_t v){return v==0xa5u;}),"Aura callback changed stack guards");
        const bool primary=slot!=0u && (active&1u),secondary=bank==0u && slot==2u && (active&2u);
        require(m.read32(0x29400u)==unsigned(primary)+unsigned(secondary),"Aura callback submitted an inactive object");
        unsigned call=0;
        if(primary){require(m.read32(0x29404u+call++*4u)==0x600000u+character*0x400u+(bank ? 0x200u : 0u),
                "Aura callback submitted the wrong character object");
            const unsigned count=slot==2u ? 16u : 8u;
            for(unsigned i=0;i<16u;++i){uint32_t expected=0xa5a5a5a5u;
                if(i<count){expected=expectedMatrix[i];if(i%4u==3u){float f=float(i+1u)*0.5f;std::memcpy(&expected,&f,4);}}
                require(m.read32(destination+0x80u+i*4u)==expected,"Aura matrix copy or scale differs");}
        } else require(std::all_of(ram+destination+0x80u,ram+destination+0xc0u,[](uint8_t v){return v==0xa5u;}),
            "Inactive aura changed its model matrix");
        if(secondary)require(m.read32(0x29404u+call*4u)==0x600100u+character*0x400u,"Secondary aura object order differs");
        if(!bank && primary){const auto expected= special ? 5.0f : progress>0 ? 2.0f+character+4.0f*progress : 0.0f;
            require(readFloat(descriptor+0x5cu)==expected && readFloat(descriptor+0x54u)==(special ? 4.0f : slot==2u ? -1.0f : -2.0f),
                "Aura progress or special-state fields differ");}
        if(secondary)require(readFloat(auxiliary+0x5cu)==(progress<0 ? 0.0f : 0.1f*progress),"Secondary aura progress differs");
        if(bank)require(m.read32(state+0x24u)==(primary && expired ? 0u : 0x600200u+character*0x400u),
            "Aura completion cleared an incorrect object");
    }
}
}
