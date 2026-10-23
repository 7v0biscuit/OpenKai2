// SPDX-License-Identifier: GPL-3.0-only
#include "battle_update_checks.h"
#include "ps2_runtime.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace openkai2 {
void verify_battle_updates(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(auto entry:translation::battle_update_entries)
        require(runtime.hasFunction(entry),"Battle update method is not registered");
    constexpr uint32_t root=0x600000u,globals=0x640000u,model=0x680000u;
    constexpr uint32_t sp=0x1ffffc0u,returned=0x1be648u,primaryBridge=0x1908e8u,secondaryBridge=0x1908f0u;
    constexpr uint32_t dependencies[]={0x1a7ce0u,0x1bdfc0u,0x1c5b20u,0x1c7490u,0x1be300u,0x1c89a0u,0x123a60u};
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto i=entries.rbegin();i!=entries.rend();++i)runtime.replaceFunction(i->first,i->second);}
    } restore{runtime,{}};
    for(auto entry:dependencies){
        restore.entries.emplace_back(entry,runtime.lookupFunction(entry));
        runtime.replaceFunction(entry,[](uint8_t* ram,R5900Context* c,PS2Runtime* r){
            auto& m=r->memory();const auto variant=m.read32(0x29800u),target=c->pc;
            if(target==0x123a60u){
                if(getRegU32(c,4)!=0x3df4ecu || getRegU32(c,5)!=0x600000u || getRegU32(c,6)!=0xf4u)
                    throw std::runtime_error("Battle update changed its previous-state copy arguments");
                std::memmove(ram+0x3df4ecu,ram+0x600000u,0xf4u);m.write32(0x29820u,m.read32(0x29820u)+1u);
            } else {
                const auto n=m.read32(0x29000u),out=0x29010u+n*24u;
                if(n>=48u)throw std::runtime_error("Battle update exceeded its dependency-call bound");
                m.write32(out,target);for(unsigned i=0;i<4u;++i)m.write32(out+4u+i*4u,getRegU32(c,4u+i));
                m.write32(0x29000u,n+1u);
                c->r[2]=_mm_cvtsi32_si128(variant&1u);
                if(target==0x1bdfc0u){
                    const auto count=m.read32(0x29824u);m.write32(0x29824u,count+1u);
                    c->r[2]=_mm_cvtsi32_si128((variant&1u) ? 0x6f0000u+count*64u : 0u);
                }
                if(target==0x1c5b20u && (variant&4u)){
                    const auto object=0x680000u+getRegU32(c,4)*0x400u;
                    m.write32(object+0xb0u,m.read32(object+0xb0u)|0x80u);
                }
                if(target==0x1be300u){
                    const auto out=getRegU32(c,4);std::memset(ram+out,0,8u);
                    m.write8(out+3u,(variant&4u) ? 1u : 0u);m.write8(out+4u,(variant&2u) ? 1u : 0u);
                }
                if(target==0x1c89a0u && (getRegU32(c,4)!=0u || m.read32(getRegU32(c,5))!=0u))
                    throw std::runtime_error("Battle update changed its reset-command parameters");
            }
            c->pc=getRegU32(c,31);
        });
    }
    for(auto entry:{primaryBridge,secondaryBridge})restore.entries.emplace_back(entry,runtime.lookupFunction(entry));
    auto& m=runtime.memory();
    auto putFloat=[&](uint32_t a,float f){uint32_t bits;std::memcpy(&bits,&f,4u);m.write32(a,bits);};
    auto readFloat=[&](uint32_t a){const auto bits=m.read32(a);float f;std::memcpy(&f,&bits,4u);return f;};
    constexpr uint32_t flags[]={0u,0x01000010u,0x160u,0x01000230u,0x00200080u,0x00015c00u,0x180u,0x00a00040u};
    constexpr uint32_t controlFlags[]={0u,0x100u,0x80000000u,0x20000000u,0x40000000u,0x20000100u,0x80000100u,0u};
    constexpr int health[]={50,100,150,0,11,0,51,150},previousHealth[]={100,100,100,100,10,0,150,150};
    auto seed=[&](unsigned mode,unsigned selection,unsigned variant,bool paused,bool absent){
        std::memset(ram+root,0,0x1000u);std::memset(ram+globals,0,0x20000u);
        std::memset(ram+model,0xa5,0x1000u);std::memset(ram+0x690000u,0,0x70000u);
        std::memset(ram+0x3df4c0u,0,0x200u);std::memset(ram+0x29000u,0,0x1000u);
        std::memset(ram+sp-0x160u,0xa5,0x170u);
        m.write32(0x29800u,variant);m.write32(0x3df4e8u,absent ? 0u : root);m.write32(0x3a4cb4u,globals);
        m.write32(0x3a4c5cu,0u);putFloat(0x3a4580u,0.5f);
        m.write32(globals+0x142e4u,paused);putFloat(globals+0x142e8u,0.5f);
        m.write16(globals+0x19c5cu,variant>=8u ? 1u : 0u);m.write8(globals+0x19c78u,variant&1u);
        m.write32(root+0xd0u,selection ? mode : 0u);m.write32(root+0xd4u,selection && mode ? 15u : mode);
        m.write32(root+0xc8u,0x6c0000u);m.write32(root+0xc4u,0x6c0100u);m.write32(0x6c000cu,0x6d0000u);m.write32(0x6c010cu,0x6d1000u);
        m.write32(root+0xd8u,(variant&1u) ? 50u : 0u);m.write32(root+0xdcu,30u);if(mode==5u)putFloat(root+0xdcu,1.0f);m.write32(root+0xe0u,(variant&4u) ? 0xffffffffu : 0u);
        // This secondary method tail-calls the pure original clip helper directly.
        // Supply a short synthetic clip and verify its actual frame advance.
        m.write32(0x6d00a0u,10u);m.write32(0x6d00a4u,10u);
        putFloat(root+0xe4u,10.0f);m.write32(root+0xe8u,controlFlags[variant&7u]);
        m.write32(0x3df5c8u,(variant&2u) ? 31u : 29u);
        for(unsigned i=0;i<2u;++i){const auto kind=(variant+i*3u)&7u,object=model+i*0x400u,record=root+i*92u;
            m.write32(record+4u,uint32_t(health[kind]));m.write32(record+0xcu,250u+i*100u);
            m.write32(record+0x38u,20u+i);m.write32(record+0x3cu,40u+i);
            m.write32(record+0x54u,0x6e0000u+i*0x100u);m.write32(0x6e00c0u+i*0x100u,i+2u);
            m.write32(record+0x58u,object);m.write32(object+8u,0x690000u+i*0x100u);m.write32(0x69000cu+i*0x100u,0x6a0000u+i*0x1000u);
            m.write32(object+0xb0u,flags[kind]);m.write32(object+0x10u,uint32_t(previousHealth[kind]-5));
            m.write32(object+0x14u,200u);m.write32(object+0x28u,3u);m.write32(object+0x2cu,variant&3u);
            m.write32(object+0x38u,10u);m.write32(object+0x3cu,50u);putFloat(object+0x44u,59.75f);putFloat(object+0x48u,59.0f);
            putFloat(object+0x78u,32.0f);putFloat(object+0x90u,(variant&2u) ? 0.99f : 0.01f);
            m.write32(0x3df4f0u+i*92u,uint32_t(previousHealth[kind]));m.write32(0x3df4f8u+i*92u,150u+i*100u);
            m.write32(0x3df500u+i*92u,123u+i);m.write32(0x3a45f0u+i*4u,71u+i*20u);m.write32(0x3a45f8u+i*4u,i+1u);
        }
        const auto selected=variant&1u;m.write32(root+0xecu,root+selected*92u);m.write32(root+0xf0u,root+(1u-selected)*92u);
        m.write32(0x3df5d8u,0x3df4ecu+selected*92u);m.write32(0x3df5dcu,0x3df4ecu+(1u-selected)*92u);
        for(unsigned i=0;i<16u;++i){m.write32(0x3747d0u+i*4u,translation::battle_primary_slots[i]);m.write32(0x374810u+i*4u,translation::battle_secondary_slots[i]);}
    };
    auto context=[&](uint32_t entry){R5900Context c{};c.pc=entry;c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(returned);c.f[20]=5.5f;
        for(unsigned i=16;i<=23u;++i)c.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);return c;};
    auto run=[&](R5900Context& c){for(unsigned step=0;step<128u && c.pc!=returned;++step){
        require(runtime.hasFunction(c.pc),"Battle update reached a missing continuation");runtime.lookupFunction(c.pc)(ram,&c,&runtime);}
        require(c.pc==returned,"Battle update did not return within its execution bound");};
    for(unsigned mode=0;mode<16u;++mode)for(unsigned selection=0;selection<2u;++selection)for(unsigned variant=0;variant<16u;++variant){
        seed(mode,selection,variant,false,false);auto reference=context(0x1c6e10u);run(reference);
        std::array<std::array<uint8_t,256>,2> expected;
        for(unsigned i=0;i<2u;++i)std::memcpy(expected[i].data(),ram+model+i*0x400u,256u);
        seed(mode,selection,variant,false,false);for(unsigned i=0;i<2u;++i)std::memcpy(ram+0x700000u+i*0x400u,expected[i].data(),256u);
        const auto primary=translation::battle_primary_slots[mode],secondary=translation::battle_secondary_slots[mode];
        const auto originalPrimary=primary ? runtime.lookupFunction(primary) : nullptr,originalSecondary=secondary ? runtime.lookupFunction(secondary) : nullptr;
        const bool alias=primary==0x1c6e10u || primary==0x1c6e00u || primary==0x1c6b40u || primary==0x1c6b30u || primary==0x1c6650u || primary==0x1c6640u;
        m.write32(0x2980cu,alias);
        if(primary){restore.entries.emplace_back(primary,originalPrimary);runtime.replaceFunction(primaryBridge,originalPrimary);
            runtime.replaceFunction(primary,[](uint8_t* ram,R5900Context* c,PS2Runtime* r){auto& m=r->memory();m.write32(0x29804u,m.read32(0x29804u)+1u);
                r->lookupFunction(primaryBridge)(ram,c,r);m.write32(0x29810u,m.read32(0x29000u));
                if(m.read32(0x2980cu))for(unsigned i=0;i<2u;++i)if(std::memcmp(ram+0x680000u+i*0x400u,ram+0x700000u+i*0x400u,256u))
                    throw std::runtime_error("Battle update tail alias differs from its shared original body");});}
        if(secondary){restore.entries.emplace_back(secondary,originalSecondary);runtime.replaceFunction(secondaryBridge,originalSecondary);
            runtime.replaceFunction(secondary,[](uint8_t* ram,R5900Context* c,PS2Runtime* r){auto& m=r->memory();m.write32(0x29808u,m.read32(0x29808u)+1u);m.write32(0x29818u,m.read32(0x29000u));
                r->lookupFunction(secondaryBridge)(ram,c,r);m.write32(0x2981cu,m.read32(0x29000u));});}
        auto c=context(0x1be500u);const auto saved=c;run(c);
        if(secondary){runtime.replaceFunction(secondary,originalSecondary);restore.entries.pop_back();}
        if(primary){runtime.replaceFunction(primary,originalPrimary);restore.entries.pop_back();}
        require(m.read32(0x29804u)==unsigned(primary!=0u) && m.read32(0x29808u)==unsigned(secondary!=0u),"Battle update selected an incorrect table method or reserved slot");
        require(getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u && c.f[20]==saved.f[20],"Battle update changed its caller frame or saved floating register");
        for(unsigned i=16;i<=23u;++i)require(std::memcmp(&c.r[i],&saved.r[i],16u)==0,"Battle update changed a saved register");
        require(std::all_of(ram+sp-0x160u,ram+sp-0x140u,[](uint8_t v){return v==0xa5u;}) && std::all_of(ram+sp,ram+sp+0x10u,[](uint8_t v){return v==0xa5u;}),"Battle update changed its stack guards");
        require(m.read32(0x29820u)==1u && std::memcmp(ram+0x3df4ecu,ram+root,0xecu)==0,"Battle update lost its previous-state snapshot");
        require(m.read32(0x3df5d8u)==0x3df4ecu+(variant&1u)*92u && m.read32(0x3df5dcu)==0x3df4ecu+(1u-(variant&1u))*92u,"Battle update did not rebind both previous character records");
        for(unsigned i=0;i<2u;++i)require(std::all_of(ram+model+i*0x400u+0x100u,ram+model+(i+1u)*0x400u,[](uint8_t v){return v==0xa5u;}),"Battle update wrote beyond a character model record");
        if(primary==0x1c65f0u)for(unsigned i=0;i<2u;++i)require((m.read32(model+i*0x400u+0xb0u)&0x20u)==0u,"Battle update failed to clear both character animation flags");
        if(primary==0x1c6450u)for(unsigned i=0;i<2u;++i)require(m.read32(model+i*0x400u+0x30u)==71u+i*20u && m.read32(model+i*0x400u+0x2cu)==std::min((variant&3u)+1u,3u),"Battle update lost its character parameters or clamped counter");
        uint32_t control=controlFlags[variant&7u];float progress=10.0f;
        if(secondary==0x1ba310u)progress+=0.5f;
        if(secondary==0x1ba390u && !(variant&4u) && !(control&0x80000000u)){
            if(!(variant&1u) && !(control&0x40000000u)){control|=0x40000000u;progress=0.0f;}
            if(control&0x20000000u){control&=~0x60000000u;progress=0.0f;}}
        if(secondary==0x1ba460u || secondary==0x1ba500u){
            if((control&0x100u) && !(variant&1u)){control&=~0x100u;progress=0.0f;}
            if(!(variant&2u)){control|=0x100u;progress=0.0f;}}
        require(m.read32(root+0xe8u)==control && readFloat(root+0xe4u)==progress,"Secondary battle update changed incorrect state flags or progress");
        if(secondary==0x1ba5a0u)require(readFloat(root+0xdcu)==1.5f,"Secondary battle tail call lost its original clip advance");
        if(secondary==0x1ba5c0u){const auto first=m.read32(0x29818u),last=m.read32(0x2981cu);unsigned commands=0,models=0,stats=0;
            for(unsigned i=first;i<last;++i){const auto call=m.read32(0x29010u+i*24u);commands+=call==0x1c89a0u;models+=call==0x1a7ce0u;stats+=call==0x1be300u;}
            require(stats==1u && models==unsigned(!(variant&4u) && (variant&2u)) && commands==unsigned(variant>=8u && !(variant&1u)),"Secondary battle update changed input/reset callback conditions");}
    }
    // A paused frame still snapshots and rebinds; an absent context returns
    // before table selection or copying. Retail asset dependencies stay bounded.
    for(unsigned mode=0;mode<16u;++mode)for(unsigned selection=0;selection<2u;++selection){
        seed(mode,selection,0u,true,false);auto c=context(0x1be500u);run(c);
        require(m.read32(0x29000u)==0u && m.read32(0x29820u)==1u,"Paused battle update executed an active method or skipped its snapshot");}
    seed(1u,1u,0u,false,true);auto c=context(0x1be500u);run(c);
    require(m.read32(0x29000u)==0u && m.read32(0x29820u)==0u,"Absent battle context executed an update or state copy");
}
}
