// SPDX-License-Identifier: GPL-3.0-only
#include "battle_command_checks.h"
#include "cpu_difficulty.h"
#include "runtime/ee_scheduler.h"
#include "ps2_runtime.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace openkai2 {
void verify_tournament_cpu(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    constexpr uint32_t battle=0x500000u,file=0x600000u,stack=0x1fffde0u,returned=0x29f00u;
    auto& m=runtime.memory();
    auto call=[&](uint32_t pc,unsigned side=0) {
        R5900Context c{};c.pc=pc;c.r[16]=_mm_cvtsi32_si128(side);c.r[17]=_mm_cvtsi32_si128(battle+0x210u);
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(stack);c.r[31]=_mm_cvtsi32_si128(returned);
        for(unsigned step=0;step<64u && c.pc!=returned;++step) {
            require(runtime.hasFunction(c.pc),"Tournament CPU check reached an unregistered continuation");
            try {runtime.lookupFunction(c.pc)(ram,&c,&runtime);} catch(const EeDispatcherTransfer&) {}
        }
        require(c.pc==returned,"Tournament CPU check did not return");
    };
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(const auto& entry:entries)runtime.replaceFunction(entry.first,entry.second);}
    } restore{runtime,{}};
    for(const auto target:{0x1d0410u,0x1d0870u}) {
        restore.entries.emplace_back(target,runtime.lookupFunction(target));
        runtime.replaceFunction(target,[](uint8_t*,R5900Context* c,PS2Runtime* r){
            r->memory().write32(c->pc==0x1d0410u ? 0x29800u : 0x29804u,1u);
            c->pc=returned;
        });
    }
    for(const uint32_t alias:{0u,0x80000000u,0xa0000000u}) {
        std::memset(ram+battle,0,0x20000u);std::memset(ram+file,0,0x7000u);
        std::memset(ram+cpu::ai_context,0,cpu::ai_stride*2u);
        m.write32(cpu::battle_pointer,battle|alias);m.write8(battle+cpu::battle_mode,2);
        m.write8(battle+0x143a0u,2);m.write8(battle+cpu::battle_level,1);m.write8(battle+cpu::battle_level+1,2);
        m.write32(file+8u,(file+0x5000u)|alias);m.write32(file+0x10u,(file+0x2000u)|alias);
        m.write32(file+0x38u,(file+0x1000u)|alias);m.write8(file+0x1000u,7);
        m.write32(file+0x2000u+2u*4u,(file+0x3000u)|alias);
        m.write32(file+0x2000u+7u*4u,(file+0x4000u)|alias);
        m.write32(file+0x5000u,(file+0x6000u)|alias);
        for(unsigned side=0;side<2;++side) {
            const auto at=cpu::ai_context+side*cpu::ai_stride;
            m.write32(at+8u,file|alias);m.write8(at+0x15u,2);
            m.write16(battle+cpu::fighter_port+side*cpu::fighter_stride,side ? 0xffffu : 0u);
        }
        m.write8(file+0x4116u,31);m.write8(file+0x411du,47);m.write8(file+0x411eu,63);
        require(cpu::prepare_ai(ram,PS2_RAM_SIZE,3)==1,"Tournament strongest AI preset was not prepared");
        call(0x223bd0u);
        require(m.read8(cpu::ai_context+0x16u)==2 && m.read8(cpu::ai_context+cpu::ai_stride+0x16u)==7,
                "Original AI initializer did not apply the CPU preset exclusively");
        require(m.read32(cpu::ai_context+cpu::ai_stride+0xcu)==((file+0x4000u)|alias) &&
                m.read16(cpu::ai_context+cpu::ai_stride+0x22u)==31 &&
                m.read16(cpu::ai_context+cpu::ai_stride+0x24u)==47 &&
                m.read16(cpu::ai_context+cpu::ai_stride+0x26u)==63,
                "Original AI initializer did not rebuild active behavior parameters");
        require(m.read8(battle+cpu::battle_level)==1 && m.read8(battle+cpu::battle_level+1)==2,
                "Tournament bracket or round changed");
        for(unsigned side=0;side<2;++side) {
            m.write32(0x29800u,0);m.write32(0x29804u,0);call(0x1e5ee0u,side);
            require(m.read32(0x29800u)==side && m.read32(0x29804u)==1u-side,
                    "Tournament CPU side disagrees with the original input dispatcher");
        }
    }
}

void verify_battle_commands(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(auto entry:translation::battle_command_entries)
        require(runtime.hasFunction(entry),"Battle command handler is not registered");
    constexpr uint32_t dependencies[]={0x1c62f0u,0x1c6080u,0x1bdaa0u,0x1bda40u,0x1bda70u,
        0x1bdbc0u,0x1bdb50u,0x1bdb10u,0x1bd8e0u,0x1c6220u,0x1c5d50u,0x1c5ca0u,
        0x1c5bf0u,0x1bdf00u,0x1bdc70u,0x1c59b0u,0x1c25d0u,0x1c5a60u,0x1c6050u,0x1c6150u};
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto& entry:entries)runtime.replaceFunction(entry.first,entry.second);}
    } restore{runtime,{}};
    for(auto entry:dependencies){
        restore.entries.emplace_back(entry,runtime.lookupFunction(entry));
        runtime.replaceFunction(entry,[](uint8_t*,R5900Context* c,PS2Runtime* r){
            auto& m=r->memory();const auto count=m.read32(0x29400u),out=0x29410u+count*32u;
            m.write32(out,c->pc);for(unsigned i=0;i<5u;++i)m.write32(out+4u+i*4u,getRegU32(c,4u+i));
            m.write32(0x29400u,count+1u);c->pc=getRegU32(c,31);
        });
    }
    constexpr uint32_t context=0x1c0000u,parameters=0x29000u,sp=0x1ffff40u,returned=0x1d53a8u;
    auto& m=runtime.memory();
    for(unsigned mode:{0u,3u,5u})for(unsigned command=0;command<45u;++command)for(unsigned variant=0;variant<4u;++variant){
        std::memset(ram+context,0,0x40000u);std::memset(ram+0x29000u,0,0x1000u);
        std::memset(ram+sp-0x120u,0xa5,0x120u);
        const unsigned first=variant&1u;const bool enabled=variant!=0u;
        const uint32_t flags=0x7a001e0u|uint32_t(enabled);
        m.write32(0x3df4e8u,context);m.write32(0x3df634u,flags);m.write32(0x3a4cb4u,0x1e0000u);
        m.write32(context+0xd0u,mode);m.write32(context+0xe0u,variant==2u ? 0xffffffffu : 0u);
        m.write32(context+0xdcu,variant==2u ? 0xffffffffu : 0u);
        m.write32(context+0xe8u,variant==3u ? 0x28u : 0u);
        m.write32(context+0xb8u,0x1f0100u);m.write32(context+0xccu,0x1f0200u);
        for(unsigned index=0;index<2u;++index){
            const auto object=0x1d0000u+index*0x200u,child=0x1d1000u+index*0x200u;
            m.write32(context+0x58u+index*92u,child);m.write32(context+0xecu+index*4u,object);
            m.write32(object+0x58u,child);m.write32(child+0xb0u,0x10040u);
            m.write32(child+0x24u,0xeeeeeeeeu);m.write32(child+0x34u,0xeeeeeeeeu);
            m.write32(context+0x50u+index*92u,0xeeeeeeeeu);
        }
        m.write32(parameters,first);m.write32(parameters+4u,0x3f4ccccdu);m.write32(parameters+8u,0x3f4ccccdu);
        std::array<uint8_t,16> savedParameters;std::memcpy(savedParameters.data(),ram+parameters,16u);
        for(unsigned i=0;i<45u;++i){m.write32(0x374fa0u+i*4u,translation::battle_command_slots[i]);
            m.write32(0x3886e0u+i*4u,translation::battle_command_filter[i]);}
        for(unsigned i=0;i<14u;++i){m.write32(0x3886a0u+i*4u,translation::battle_command_switch[i]);
            m.write16(0x388680u+i*2u,2u);m.write32(0x375060u+i*4u,0x1f1000u+i*8u);
            m.write16(0x1f1000u+i*8u,0x10u+i);m.write16(0x1f1002u+i*8u,0x20u+i);}
        R5900Context c{};c.pc=0x1c89a0u;c.r[4]=_mm_cvtsi32_si128(command);c.r[5]=_mm_cvtsi32_si128(parameters);
        c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(returned);
        for(unsigned i=16;i<=23;++i)c.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);
        const auto saved=c;
        for(unsigned step=0;step<128u && c.pc!=returned;++step){
            require(runtime.hasFunction(c.pc),"Battle command reached a missing continuation");
            runtime.lookupFunction(c.pc)(ram,&c,&runtime);
        }
        require(c.pc==returned && getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u,
            "Battle command failed to restore its original caller frame");
        for(unsigned i=16;i<=23;++i)require(std::memcmp(&saved.r[i],&c.r[i],16u)==0,
            "Battle command changed a saved register");
        require(std::memcmp(savedParameters.data(),ram+parameters,16u)==0
            && std::all_of(ram+sp-0x120u,ram+sp-0x110u,[](uint8_t v){return v==0xa5u;}),
            "Battle command changed source parameters or stack guards");
        const bool filtered=mode==5u && translation::battle_command_filter[command]==0x1c8a00u;
        const bool executed=enabled && !filtered && translation::battle_command_slots[command]!=0u;
        if(command==0u){
            const auto expected=enabled ? (first ? flags|0x1eu : flags&~6u) : flags;
            require(m.read32(0x3df634u)==expected,"Battle command zero changed incorrect enable bits");
        }
        if(!executed)require(m.read32(0x29400u)==0u && m.read32(0x3df634u)==flags,
            "Disabled, filtered, or reserved battle command executed a handler");
        if(executed && command>=7u && command<=20u){
            const auto index=command-7u;const bool finish=command>=8u && command<=11u;
            require(m.read32(0x29400u)==2u+unsigned(finish),"Repeated battle command changed callback count");
            for(unsigned i=0;i<2u;++i){const auto out=0x29410u+i*32u;
                require(m.read32(out)==0x1bdf00u && m.read32(out+4u)==0x1f0100u
                    && m.read32(out+8u)==0x1f0200u && m.read32(out+12u)==0x10u+index+i*16u
                    && m.read32(out+16u)==0x05000000u && m.read32(out+20u)==0u,
                    "Battle command switch changed model callback arguments");}
            if(finish)require(m.read32(0x29450u)==0x1bdc70u && m.read32(0x29454u)==index,
                "Battle command lost its conditional completion call");
        }
        if(executed && command==39u)require(m.read32(context+0xe0u)==first
            && (m.read32(context+0xe8u)&0x20000000u)!=0u,"Battle command failed to set its state flag");
        if(executed && command==38u){const auto child=0x1d1000u+first*0x200u;
            require((m.read32(child+0xb0u)&0x100u)!=0u && m.read32(child+0x28u)==0x3f4ccccdu
                && m.read32(child+0x34u)==0u,"Battle command changed character animation state");}
    }
}
}
