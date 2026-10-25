// SPDX-License-Identifier: GPL-3.0-only
#include "pause_menu_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace openkai2 {
void verify_pause_menu(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    for(auto at:translation::pause_menu_entries)require(runtime.hasFunction(at),"Pause menu callback is missing");
    constexpr uint32_t object=0x630000u,manager=0x640000u,list=0x650000u,control=0x660000u;
    constexpr uint32_t marker=0x29b00u,returned=0x29f00u,sp=0x1fffde0u;
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto i=entries.rbegin();i!=entries.rend();++i)runtime.replaceFunction(i->first,i->second);}
        void replace(uint32_t at,PS2Runtime::RecompiledFunction fn){entries.emplace_back(at,runtime.lookupFunction(at));runtime.replaceFunction(at,fn);}
    } restore{runtime,{}};
    auto& m=runtime.memory();
    const auto run=[&](R5900Context& c){for(unsigned step=0;step<128u && c.pc!=returned;++step){
        const auto fn=runtime.lookupFunction(c.pc);require(fn!=nullptr,"Pause menu reached an untranslated continuation");
        try{fn(ram,&c,&runtime);}catch(const EeDispatcherTransfer&){}
    }require(c.pc==returned,"Pause menu did not return");};
    const auto context=[&](uint32_t pc,uint32_t alias){
        R5900Context c{};c.pc=pc;c.r[4]=_mm_cvtsi64_si128(int64_t(int32_t(object|alias)));
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(returned);
        c.r[16]=_mm_set_epi32(1,2,3,4);c.r[17]=_mm_set_epi32(5,6,7,8);return c;
    };
    const auto frame=[&](const R5900Context& c){
        require(getRegU32(&c,29)==sp && getRegU32(&c,31)==returned && getRegU32(&c,28)==0x3ac4f0u,"Pause menu lost its caller frame");
        const auto s0=_mm_set_epi32(1,2,3,4),s1=_mm_set_epi32(5,6,7,8);
        require(!std::memcmp(&c.r[16],&s0,16u) && !std::memcmp(&c.r[17],&s1,16u),"Pause menu changed saved registers");
        for(unsigned i=0;i<16u;++i)require(ram[sp-0x100u+i]==0xa5u && ram[sp+i]==0xa5u,"Pause menu changed stack guards");
    };
    const auto reset=[&]{std::memset(ram+object,0,0x50000u);std::memset(ram+marker,0,64u);std::memset(ram+sp-0x100u,0xa5u,0x110u);};
    restore.replace(0x147420u,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        uint32_t value;std::memcpy(&value,&c->f[12],4u);auto& m=r->memory();
        if(getRegU32(c,4)!=0u || getRegU32(c,5)!=0u || value!=m.read32(0x694860u))throw std::runtime_error("Pause menu fade arguments changed");
        m.write32(marker,m.read32(marker)+1u);c->pc=getRegU32(c,31);
    });
    restore.replace(0x147260u,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        if(getRegU32(c,4)!=0u || getRegU32(c,5)!=0u || getRegU32(c,6)!=22u || getRegU32(c,7)!=0u)throw std::runtime_error("Pause menu sound arguments changed");
        auto& m=r->memory();m.write32(marker+4u,m.read32(marker+4u)+1u);c->pc=getRegU32(c,31);
    });
    unsigned cases=0u;
    for(unsigned mode=0;mode<3u;++mode)for(unsigned tick=0;tick<4u;++tick)for(auto alias:{0u,0x80000000u,0xa0000000u}){
        reset();m.write32(object+0x30u,manager|alias);m.write32(object+0x34u,mode);m.write32(manager+12u,0u);
        m.write32(object+0x4cu,0xffffffffu);m.write32(object+0x38u,0xffffffffu);m.write32(0x3a4cb4u,control|alias);
        const float elapsed=float(tick),delta=0.5f,volume=0.25f;
        std::memcpy(ram+object+0x3cu,&elapsed,4u);std::memcpy(ram+control+0x142e8u,&delta,4u);std::memcpy(ram+0x694860u,&volume,4u);
        auto c=context(0x1c0bd0u,alias);run(c);frame(c);
        require(m.read32(object+0x1cu)==0x1c22f0u && m.read32(object+0x38u)==1u && m.read32(object+0x4cu)==0u,"Pause open callback changed state or continuation");
        require(m.read32(marker)==1u && m.read32(marker+4u)==1u,"Pause open callback did not perform its original dependencies");
        c=context(0x1c22f0u,alias);run(c);frame(c);float actual;std::memcpy(&actual,ram+object+0x3cu,4u);
        require(actual==elapsed+delta && m.read32(object+0x1cu)==0x1c22f0u,"Pause continuation changed its timer or callback");
        cases+=2u;
    }
    restore.replace(0x1bf2e0u,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        c->r[2]=_mm_cvtsi32_si128(r->memory().read32(marker+16u));c->pc=getRegU32(c,31);
    });
    for(auto at:translation::pause_menu_entries)restore.replace(at,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        auto& m=r->memory();if(c->pc!=m.read32(marker+20u) || getRegU32(c,4)!=m.read32(marker+24u) || getRegU32(c,5)!=m.read32(marker+28u))throw std::runtime_error("Pause input dispatcher selected a wrong callback or arguments");
        m.write32(marker+32u,m.read32(marker+32u)+1u);c->pc=getRegU32(c,31);
    });
    for(unsigned row=0;row<21u;++row)for(unsigned slot=0;slot<7u;++slot)for(auto alias:{0u,0x80000000u,0xa0000000u}){
        reset();m.write32(object+0x30u,manager|alias);m.write32(manager+4u,object|alias);m.write32(manager+8u,0u);
        m.write32(object+0x40u,list|alias);m.write32(object+0x44u,row);m.write32(list+row*4u,row);m.write32(object+0x50u,0u);
        for(unsigned r=0;r<21u;++r)for(unsigned i=0;i<6u;++i)m.write32(0x3748a0u+r*40u+16u+i*4u,translation::pause_menu_slots[r*6u+i]);
        unsigned selected=0u;const unsigned input=slot==6u ? 63u : 1u<<slot;
        for(unsigned i=0;i<6u;++i)if((input&(1u<<i)) && translation::pause_menu_slots[row*6u+i]){selected=translation::pause_menu_slots[row*6u+i];break;}
        m.write32(marker+16u,input);m.write32(marker+20u,selected);m.write32(marker+24u,object|alias);m.write32(marker+28u,0x3748a0u+row*40u);
        auto c=context(0x1c2170u,alias);run(c);frame(c);
        if(m.read32(marker+32u)!=(selected ? 1u : 0u))throw std::runtime_error("Pause input dispatcher changed callback priority or null-slot filtering at row "+std::to_string(row)+", slot "+std::to_string(slot)+", input "+std::to_string(input));
        ++cases;
    }
    require(cases==513u,"Pause menu coverage incomplete");
}
}
