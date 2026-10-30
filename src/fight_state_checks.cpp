// SPDX-License-Identifier: GPL-3.0-only
#include "fight_state_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace openkai2 {
namespace {
void verify_melee_callbacks(PS2Runtime& runtime,uint8_t* ram){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(auto at:translation::melee_task_entries)require(runtime.hasFunction(at),"Melee task callback is not registered");
    constexpr uint32_t actor=0x780000u,task=0x7a0000u,nodes=0x7c0000u,gp=0x3ac4f0u;
    constexpr uint32_t sp=0x1fffc00u,returned=0x1483f8u,marker=0x29a00u;
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> saved;
        ~Restore(){for(auto i=saved.rbegin();i!=saved.rend();++i)runtime.replaceFunction(i->first,i->second);}
        void replace(uint32_t pc,PS2Runtime::RecompiledFunction f){saved.emplace_back(pc,runtime.lookupFunction(pc));runtime.replaceFunction(pc,f);}
    } restore{runtime,{}};
    for(auto pc:{0x1f4af0u,0x1f1f90u,0x1f6010u,0x148ef0u,0x1a8d10u,0x1485f0u})
        restore.replace(pc,[](uint8_t*,R5900Context* c,PS2Runtime* r){
            auto& m=r->memory();const auto pc=c->pc;
            if(pc==0x1a8d10u)c->r[2]=_mm_cvtsi32_si128(nodes+getRegU32(c,5)*128u);
            else if(pc==0x1485f0u){
                if((getRegU32(c,4)&0x1fffffffu)!=task)throw std::runtime_error("Battle task retired a different object");
                m.write32(marker+4u,m.read32(marker+4u)+1u);
            }else if(pc!=0x148ef0u){
                if((getRegU32(c,4)&0x1fffffffu)!=actor)throw std::runtime_error("Melee callback changed its actor");
                if(pc==0x1f6010u && (getRegU32(c,5)!=0u || getRegU32(c,6)!=0u))throw std::runtime_error("Melee update changed its reset arguments");
                m.write32(marker,m.read32(marker)+1u);
            }
            c->pc=getRegU32(c,31);
        });
    for(auto alias:{0u,0x80000000u,0xa0000000u}){
        const auto call=[&](uint32_t pc,uint32_t a0){
            R5900Context c{};c.pc=pc;c.r[4]=_mm_cvtsi32_si128(a0|alias);
            c.r[28]=_mm_cvtsi32_si128(gp);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(returned);
            for(unsigned i=16u;i<=23u;++i)c.r[i]=_mm_set_epi32(i+3,i+2,i+1,i);
            c.f[20]=13.0f;c.f[21]=-27.0f;c.f[22]=49.0f;const auto before=c;
            for(unsigned step=0;step<64u && c.pc!=returned;++step){require(runtime.hasFunction(c.pc),"Melee callback lost its continuation");runtime.lookupFunction(c.pc)(ram,&c,&runtime);}
            require(c.pc==returned && getRegU32(&c,29)==sp && getRegU32(&c,28)==gp,"Melee callback did not return with its caller frame");
            require(std::memcmp(c.r+16,before.r+16,8u*sizeof(c.r[0]))==0 && c.f[20]==13.0f && c.f[21]==-27.0f && c.f[22]==49.0f,"Melee callback changed saved registers");
        };
        runtime.memory().write32(marker,0u);runtime.memory().write32(actor+0x9bd0u,0xdeadbeefu);runtime.memory().write32(actor+0x9bd8u,0xface1234u);
        call(0x1d8220u,actor);
        require(runtime.memory().read32(actor+0x9bd4u)==0x3ecccccdu && runtime.memory().read32(marker)==1u,"Melee entry lost its tail call or delay-slot actor update");
        require(runtime.memory().read32(actor+0x9bd0u)==0xdeadbeefu && runtime.memory().read32(actor+0x9bd8u)==0xface1234u,"Melee entry overwrote adjacent actor data");
        call(0x1d8240u,actor);require(runtime.memory().read32(marker)==3u,"Melee update skipped one of its original calls");
        for(unsigned state=0;state<3u;++state)for(unsigned elapsed:{0u,10u}){
            std::memset(ram+task,0,128u);std::memset(ram+nodes,0,8192u);
            runtime.memory().write32(0x3df4e8u,actor);runtime.memory().write32(actor+0xb8u,task);runtime.memory().write32(actor+0xc4u,nodes);
            runtime.memory().write32(0x3df4a8u,0u);runtime.memory().write32(0x3df49cu,0u);
            runtime.memory().write32(task+0x34u,state);runtime.memory().write32(task+0x48u,elapsed==10u ? 0x41200000u : 0u);
            runtime.memory().write32(marker+4u,0u);runtime.memory().write32(gp-0x783cu,actor);runtime.memory().write32(actor+0x142e4u,1u);
            runtime.memory().write32(0x374748u,1u);runtime.memory().write32(0x37474cu,2u);runtime.memory().write32(0x374750u,3u);
            call(0x1bc0b0u,task);
            require(runtime.memory().read32(task+0x34u)==(state==0u && elapsed==10u ? 1u : state),"Battle task lost its original phase transition");
            require(runtime.memory().read32(marker+4u)==(state==2u && elapsed==10u ? 1u : 0u),"Battle task retired at the wrong animation phase");
        }
    }
}
}

void verify_fight_states(PS2Runtime& runtime,uint8_t* ram){
    verify_melee_callbacks(runtime,ram);
    unsigned currentMode=0,currentVariant=0;
    auto require=[&](bool ok,const char* message){if(!ok)throw std::runtime_error(std::string(message)+" (mode="+std::to_string(currentMode)+", variant="+std::to_string(currentVariant)+")");};
    for(auto entry:translation::fight_state_entries)require(runtime.hasFunction(entry),"Fight state method is not registered");
    constexpr uint32_t root=0x600000u,actor=root+0x210u,stride=0x9f50u,sp=0x1ffff40u,returned=0x1d4798u,bridge=0x1908f0u;
    constexpr uint32_t dependencies[]={0x1ef1b0u,0x1fed20u,0x1f3b50u,0x1e6430u,0x1cadb0u,0x1f1930u};
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto i=entries.rbegin();i!=entries.rend();++i)runtime.replaceFunction(i->first,i->second);}
        void replace(uint32_t entry,PS2Runtime::RecompiledFunction f){entries.emplace_back(entry,runtime.lookupFunction(entry));runtime.replaceFunction(entry,f);}
    } restore{runtime,{}};
    for(auto target:dependencies)restore.replace(target,[](uint8_t*,R5900Context* c,PS2Runtime* r){
        auto& m=r->memory();const auto variant=m.read32(0x29800u),target=c->pc,n=m.read32(0x29808u);
        if(n>=8u)throw std::runtime_error("Fight rule exceeded its dependency bound");
        m.write32(0x29900u+n*16u,target);m.write32(0x29904u+n*16u,getRegU32(c,4));m.write32(0x29908u+n*16u,getRegU32(c,5));m.write32(0x29808u,n+1u);
        if(target==0x1ef1b0u){const auto object=getRegU32(c,4);
            if(object!=actor && object!=actor+stride)throw std::runtime_error("Fight rule queried an incorrect character");
            c->r[2]=_mm_cvtsi32_si128((variant&(object==actor ? 1u : 2u)) ? 1u : 0u);
        }else if(target==0x1fed20u)c->r[2]=_mm_cvtsi32_si128((variant&32u) ? 5u : 4u);
        else if(target==0x1f3b50u){
            if(c->f[12]!=180.0f)throw std::runtime_error("Fight rule changed its animation duration");c->r[2]=_mm_cvtsi32_si128(12u);
        }else if(target==0x1cadb0u){
            const auto object=m.read32(getRegU32(c,5));
            if(getRegU32(c,4)!=16u || (object!=actor+0x4b0u && object!=actor+stride+0x4b0u))
                throw std::runtime_error("Fight rule changed its animation command");
        }else if(target==0x1e6430u && getRegU32(c,5)!=14u)throw std::runtime_error("Fight rule changed its character state command");
        else if(target==0x1f1930u && (getRegU32(c,4)!=0u || getRegU32(c,5)!=43u))throw std::runtime_error("Fight rule changed its result sound command");
        c->pc=getRegU32(c,31);
    });
    const auto previousBridge=runtime.lookupFunction(bridge);restore.entries.emplace_back(bridge,previousBridge);
    auto& m=runtime.memory();std::vector<uint8_t> expected(0x20000u);
    auto putFloat=[&](uint32_t at,float f){uint32_t bits;std::memcpy(&bits,&f,4u);m.write32(at,bits);};
    auto writeExpected=[&](uint32_t at,const auto& value){std::memcpy(expected.data()+at-root,&value,sizeof(value));};
    for(unsigned mode=0;mode<14u;++mode)for(unsigned variant=0;variant<64u;++variant){
        currentMode=mode;currentVariant=variant;
        std::memset(ram+root,0,expected.size());std::memset(ram+root-16u,0xa5,16u);std::memset(ram+root+expected.size(),0xa5,16u);
        std::memset(ram+sp-0x100u,0xa5,0x110u);std::memset(ram+0x29800u,0,0x200u);
        m.write32(0x29800u,variant);m.write32(0x3a4cb4u,root);m.write8(root+0x143a0u,uint8_t(mode));
        for(unsigned i=0;i<16u;++i)m.write32(0x3939a0u+i*4u,translation::fight_rule_slots[i]);
        const auto request=(variant&8u) ? uint8_t(1u+((variant>>4u)&1u)) : uint8_t(0);
        const int16_t timerSide=int16_t(variant%3u)-1;
        m.write8(root+0x19c7eu,request);m.write16(root+0x19c68u,uint16_t(timerSide));
        putFloat(root+0x19c6cu,(variant&4u) ? 10.0f : 9.0f);putFloat(root+0x19c70u,10.0f);
        putFloat(root+0x142f8u,(variant&4u) ? 10.0f : 9.0f);m.write32(root+0x143a4u,10u);
        m.write8(root+0x14305u,0x55u);m.write8(root+0x14306u,0x66u);m.write8(root+0x14307u,0x77u);
        float health[2]={variant%3u==2u ? 125.0f : 100.0f,variant%3u==0u ? 125.0f : 100.0f};
        for(unsigned i=0;i<2u;++i){const auto object=actor+i*stride;
            putFloat(object+0x9c94u,health[i]);putFloat(object+0x9c98u,health[i]+((variant&32u) ? 0.0f : 5.0f));
            m.write32(object+0xf9cu,i);m.write16(object+0x95b0u,uint16_t(17u+i*12u));
            m.write8(object+0x9f10u,0x31u);m.write8(object+0x9ef0u,0x32u);m.write8(object+0xa05u,0x33u);
            m.write32(object+0xfb4u,0x710000u+i*64u);
        }
        std::memcpy(expected.data(),ram+root,expected.size());
        const auto target=translation::fight_rule_slots[mode];const auto original=runtime.lookupFunction(target);
        runtime.replaceFunction(bridge,original);restore.replace(target,[](uint8_t* data,R5900Context* c,PS2Runtime* r){
            r->memory().write32(0x29804u,c->pc);r->lookupFunction(bridge)(data,c,r);throw EeDispatcherTransfer{};
        });
        R5900Context c{};c.pc=0x1d4770u;c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(0x1d5648u);
        for(unsigned i=16;i<=23u;++i)c.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);
        c.r[16]=_mm_cvtsi32_si128(root);const auto saved=c;
        for(unsigned step=0;step<64u && c.pc!=returned;++step){
            const auto function=step==0u ? runtime.lookupFunction(0x1d4630u) : runtime.lookupFunction(c.pc);
            require(function!=nullptr,"Fight rule reached a missing continuation");try {function(ram,&c,&runtime);}catch(const EeDispatcherTransfer&){}
        }
        runtime.replaceFunction(target,original);restore.entries.pop_back();
        require(c.pc==returned && m.read32(0x29804u)==target,"Fight rule did not return through the original indirect call");
        bool complete=false;unsigned result=0,winner=0;bool outcome=false;unsigned aliveCalls=2u;
        const bool expired=(variant&4u)!=0u;
        auto outcomeBytes=[&](unsigned kind,int side){result=kind;winner=unsigned(side);outcome=true;complete=true;};
        if(target==0x1d3030u){
            if(timerSide>=0 && expired){outcomeBytes(3u,1-timerSide);const auto object=actor+uint32_t(timerSide)*stride;
                writeExpected(object+0x9ef0u,uint8_t(0xffu));writeExpected(object+0x9c06u,uint16_t(12u));
                writeExpected(object+0x9bf8u,uint32_t(0x3fe66666u));writeExpected(object+0x9bfcu,uint32_t(0));writeExpected(object+0xa05u,uint8_t(0));}
        }else if(target==0x1d31c0u || target==0x1d3280u){if(expired)outcomeBytes(3u,0);}
        else if(target==0x1d3340u){if(variant&32u)outcomeBytes(5u,0);}
        else if(target==0x1d33f0u || (target==0x1d34f0u && request)){
            if(target==0x1d34f0u)aliveCalls=0u;
            if(request){outcomeBytes(4u,int(request)-1);writeExpected(root+0x19c7eu,uint8_t(-int(request)));
                writeExpected(actor+(1u-(request-1u))*stride+0x9f10u,uint8_t(0));}
        }else {
            const bool first=variant&1u,second=variant&2u;
            if(!first && !second)outcomeBytes(1u,-1);
            else if(!first || !second)outcomeBytes((variant&32u) ? 2u : 0u,first ? 0 : 1);
            else if(expired)outcomeBytes(3u,health[0]<health[1] ? 1 : health[0]>health[1] ? 0 : -1);
        }
        if(outcome){writeExpected(root+0x14305u,uint8_t(result));writeExpected(root+0x14306u,uint8_t(winner));
            writeExpected(root+0x14307u,winner==uint32_t(-1) ? uint8_t(0xffu) : uint8_t(17u+winner*12u));}
        require(getRegU32(&c,2)==unsigned(complete),"Fight rule changed its completion result");
        if(std::memcmp(expected.data(),ram+root,expected.size())){
            size_t offset=0;while(expected[offset]==ram[root+offset])++offset;
            throw std::runtime_error("Fight rule changed context: mode="+std::to_string(mode)+", variant="+std::to_string(variant)+", offset="+std::to_string(offset)+", expected="+std::to_string(expected[offset])+", actual="+std::to_string(ram[root+offset]));
        }
        unsigned checks=0;for(unsigned i=0;i<m.read32(0x29808u);++i)checks+=m.read32(0x29900u+i*16u)==0x1ef1b0u;
        require(checks==aliveCalls,"Fight rule changed its character-check conditions");
        require(getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u && getRegU32(&c,31)==returned,"Fight rule lost its original caller frame");
        for(unsigned i=16;i<=23u;++i)require(std::memcmp(&c.r[i],&saved.r[i],16u)==0,"Fight rule changed a saved register");
        require(std::all_of(ram+sp-0x100u,ram+sp-0x80u,[](uint8_t v){return v==0xa5u;})
            && std::all_of(ram+sp,ram+sp+16u,[](uint8_t v){return v==0xa5u;}),"Fight rule changed stack guards");
        for(unsigned i=0;i<16u;++i)require(ram[root-16u+i]==0xa5u && ram[root+expected.size()+i]==0xa5u,"Fight rule changed context guards");
    }
}
}
