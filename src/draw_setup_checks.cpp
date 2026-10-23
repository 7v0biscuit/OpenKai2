// SPDX-License-Identifier: GPL-3.0-only
#include "draw_setup_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace openkai2 {
void verify_battle_assets(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for(auto entry:translation::battle_asset_entries)
        require(runtime.hasFunction(entry),"Battle asset relocation callback is missing");
    for(unsigned i=0;i<translation::battle_asset_table.size();++i)
        runtime.memory().write32(0x3939e0u+i*4u,translation::battle_asset_table[i]);
    constexpr uint32_t base=0x0190d9d0u,returned=0x001d5780u,sp=0x01fffea0u;
    for(unsigned kind=0;kind<2u;++kind)for(unsigned variant=0;variant<3u;++variant)
        for(uint32_t alias:{base,base|0x20000000u}) {
        std::memset(ram+base-16u,0xa5,0x840u);
        std::memset(ram+base,0,0x800u);
        auto word=[&](unsigned offset,uint32_t value){runtime.memory().write32(base+offset,value);};
        word(0,kind ? 0x4b534223u : 0x4d434223u); // #BSK / #BCM
        std::array<uint8_t,0x840u> expected{};
        if(!kind){
            word(4,variant ? 3u : 0u);word(8,0x10u);
            runtime.memory().write16(base+0x10u+0xeu,2u);
            word(0x50u,0x600u);word(0x54u,0u);
            runtime.memory().write16(base+0x58u+0xeu,0u);
            runtime.memory().write16(base+0x98u+0xeu,1u);word(0xd8u,0x700u);
        }else{
            word(0x10u,variant ? 3u : 0u);word(0x14u,0x80u);
            word(0x1cu,variant==2u ? 0u : 0x600u);
            word(0x80u,0u);word(0x84u,0x100u);word(0x88u,0x240u);
            word(0x128u,2u);word(0x12cu,0x400u);
            word(0x268u,0u);word(0x26cu,0u);
            word(0x400u,0x12345678u);word(0x404u,0x700u);
            word(0x408u,0x87654321u);word(0x40cu,0u);
        }
        std::memcpy(expected.data(),ram+base-16u,expected.size());
        auto expect=[&](unsigned offset,uint32_t value){std::memcpy(expected.data()+16u+offset,&value,4u);};
        expected[16u]='!';
        if(!kind){
            expect(8,alias+0x10u);
            if(variant){expect(0x50u,alias+0x600u);expect(0x54u,alias);expect(0xd8u,alias+0x700u);}
        }else{
            expect(0x14u,alias+0x80u);if(variant!=2u)expect(0x1cu,alias+0x600u);
            if(variant){expect(0x84u,alias+0x100u);expect(0x88u,alias+0x240u);
                expect(0x12cu,alias+0x400u);expect(0x404u,alias+0x700u);}
        }
        auto call=[&](){
            R5900Context c{};c.pc=0x001d65e0u;c.r[4]=_mm_cvtsi32_si128(alias);
            c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp);
            c.r[31]=_mm_cvtsi32_si128(returned);
            for(unsigned i=16u;i<=21u;++i)c.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);
            const auto saved=c;
            for(unsigned steps=0;steps<1024u && c.pc!=returned;++steps){
                require(runtime.hasFunction(c.pc),"Battle asset dispatcher reached a missing continuation");
                runtime.lookupFunction(c.pc)(ram,&c,&runtime);
            }
            require(c.pc==returned && getRegU32(&c,29)==sp && getRegU32(&c,28)==0x3ac4f0u,
                "Battle asset dispatcher did not restore its caller frame");
            for(unsigned i=16u;i<=21u;++i)require(std::memcmp(&saved.r[i],&c.r[i],16u)==0,
                "Battle asset dispatcher changed a saved register");
            return getRegU32(&c,2);
        };
        require(call()==1u && std::memcmp(expected.data(),ram+base-16u,expected.size())==0,
            "Original battle asset dispatcher changed relative pointers, nulls, or memory guards");
        require(call()==0u && std::memcmp(expected.data(),ram+base-16u,expected.size())==0,
            "Already relocated battle asset was relocated twice");
    }
}

void verify_battle_initialization(PS2Runtime& runtime,uint8_t* ram) {
    constexpr uint32_t init=0x001b2560u,returned=0x001d5780u,sp=0x01ffff70u;
    for(auto address:translation::battle_init_entries)
        if(!runtime.hasFunction(address))throw std::runtime_error("Battle initialization stage is not registered");
    // Bound the asset operations while executing the original surrounding
    // copy/clear logic. A complete battle also requires the retail geometry.
    constexpr uint32_t dependencies[]={0x1b1570u,0x1b1470u,0x1b0fd0u,0x1b0e20u,0x1b10c0u};
    struct Restore {
        PS2Runtime& runtime;std::vector<std::pair<uint32_t,PS2Runtime::RecompiledFunction>> entries;
        ~Restore(){for(auto& entry:entries)runtime.replaceFunction(entry.first,entry.second);}
    } restore{runtime,{}};
    for(auto address:dependencies){
        restore.entries.emplace_back(address,runtime.lookupFunction(address));
        runtime.replaceFunction(address,[](uint8_t*,R5900Context* c,PS2Runtime* r){
            auto& m=r->memory();const auto n=m.read32(0x29000u);
            m.write32(0x29004u+n*8u,c->pc);m.write32(0x29008u+n*8u,getRegU32(c,4));
            m.write32(0x29000u,n+1u);c->pc=getRegU32(c,31);
        });
    }
    auto call=[&](){
        R5900Context c{};c.pc=init;c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);
        c.r[29]=_mm_cvtsi32_si128(sp);c.r[31]=_mm_cvtsi32_si128(returned);
        for(unsigned step=0;step<128u && c.pc!=returned;++step){
            if(!runtime.hasFunction(c.pc))throw std::runtime_error("Battle initializer reached a missing continuation");
            runtime.lookupFunction(c.pc)(ram,&c,&runtime);
        }
        if(c.pc!=returned || getRegU32(&c,29)!=sp)
            throw std::runtime_error("Battle initializer did not restore its caller frame");
    };
    for(unsigned copy=0;copy<2u;++copy){
        std::memset(ram+0x373c50u,0xa5,0x70u);std::memset(ram+0x373d28u,0x6b,0x70u);
        for(unsigned i=0;i<19u;++i)runtime.memory().write32(0x373d38u+i*4u,0x100u+i);
        runtime.memory().write32(0x373d38u,2u);runtime.memory().write32(0x373d54u,0u);
        runtime.memory().write32(0x373d64u,copy);runtime.memory().write32(0x373c60u,2u);
        runtime.memory().write32(0x3732d0u+2u*156u,0x12345678u);runtime.memory().write32(0x29000u,0u);
        std::array<uint8_t,0x4cu> source;std::memcpy(source.data(),ram+0x373d38u,source.size());
        call();
        if(runtime.memory().read32(0x29000u)!=4u+copy || runtime.memory().read32(0x373cacu)!=0x12345678u
            || (copy && (std::memcmp(source.data(),ram+0x373c60u,source.size())!=0
                || !std::all_of(ram+0x373d38u,ram+0x373d84u,[](uint8_t b){return b==0u;}))))
            throw std::runtime_error("Battle initializer changed stage order, asset state copy, or source clear");
        for(unsigned i=0;i<4u+copy;++i)
            if(runtime.memory().read32(0x29004u+i*8u)!=dependencies[i]
                || runtime.memory().read32(0x29008u+i*8u)!=(i==4u ? 0x373c60u : 0x373d38u))
                throw std::runtime_error("Battle initializer passed an incorrect asset argument");
    }
    // Stop at the callback return before later stages consume retail assets.
    constexpr uint32_t bridge=0x001908e8u,object=0x003ba3b0u;
    restore.entries.emplace_back(bridge,runtime.lookupFunction(bridge));
    restore.entries.emplace_back(init,runtime.lookupFunction(init));
    runtime.replaceFunction(bridge,runtime.lookupFunction(init));
    runtime.replaceFunction(init,[](uint8_t* data,R5900Context* c,PS2Runtime* r){
        r->lookupFunction(bridge)(data,c,r);throw EeDispatcherTransfer{};
    });
    runtime.memory().write32(0x375230u,init);runtime.memory().write32(object+0x30u,0u);
    runtime.memory().write32(0x373d54u,1u);
    R5900Context c{};c.pc=0x1d5740u;c.r[4]=c.r[17]=_mm_cvtsi32_si128(object);
    c.r[28]=_mm_cvtsi32_si128(0x3ac4f0u);c.r[29]=_mm_cvtsi32_si128(sp+0x50u);
    try{runtime.lookupFunction(c.pc)(ram,&c,&runtime);}catch(const EeDispatcherTransfer&){}
    if(c.pc!=returned || getRegU32(&c,29)!=sp || getRegU32(&c,18)!=object+0x30u)
        throw std::runtime_error("Original battle dispatcher did not return from its logged indirect call");
}
void verify_versus_animation(PS2Runtime& runtime,uint8_t* ram) {
    constexpr uint32_t update=0x002aeac0u,draw=0x002ae2a0u,object=0x003b8c30u;
    constexpr uint32_t gp=0x003ac4f0u,slotTable=gp-0x7028u,returned=0x001483f8u;
    if(!runtime.hasFunction(update))throw std::runtime_error("Versus animation callback is not registered");
    struct Restore {
        PS2Runtime& runtime;PS2Runtime::RecompiledFunction original;
        ~Restore(){runtime.replaceFunction(draw,original);}
    } restore{runtime,runtime.lookupFunction(draw)};
    // Observe the tail call before rendering needs retail character geometry.
    // All frame arithmetic and the scene dispatch use original instructions.
    runtime.replaceFunction(draw,[](uint8_t*,R5900Context* context,PS2Runtime* active) {
        active->memory().write32(0x29000u,getRegU32(context,4));
        active->memory().write32(0x29004u,getRegU32(context,5));
        context->pc=getRegU32(context,31);
        throw EeDispatcherTransfer{};
    });
    struct Case {float frame,total,step;bool loop;float expected;};
    const Case cases[]={{0,10,1,false,1},{8,10,1,false,9},{8,10,1,true,0},
        {8,10,20,true,1},{2,10,0.25f,true,2.25f},{3,1,1,true,0},{3,0,1,false,-1}};
    for(unsigned slot=0;slot<2u;++slot)for(const auto& c:cases) {
        const uint32_t model=0x27000u+slot*0x1000u;
        std::memset(ram+model-16u,0xa5,0xa0u);std::memset(ram+object,0,0x80u);
        runtime.memory().write32(slotTable+slot*4u,model);
        auto put=[&](uint32_t offset,float value){std::memcpy(ram+model+offset,&value,4u);};
        put(0x5cu,c.frame);put(0x60u,c.total);put(0x64u,c.step);
        runtime.memory().write8(model+0x78u,c.loop);runtime.memory().write8(object+0x30u,uint8_t(slot));
        runtime.memory().write32(object+0x1cu,update);runtime.memory().write32(0x29000u,0u);
        std::array<uint8_t,0xa0u> expected;std::memcpy(expected.data(),ram+model-16u,expected.size());
        std::memcpy(expected.data()+16u+0x5cu,&c.expected,4u);
        R5900Context context{};context.pc=0x001483e8u;
        context.r[16]=_mm_cvtsi32_si128(object);context.r[28]=_mm_cvtsi32_si128(gp);
        context.r[29]=_mm_cvtsi32_si128(0x01ffffc0u);
        for(unsigned step=0;step<128u && context.pc!=returned;++step) {
            if(!runtime.hasFunction(context.pc))throw std::runtime_error("Versus scene dispatcher reached a missing continuation");
            try { runtime.lookupFunction(context.pc)(ram,&context,&runtime); }
            catch(const EeDispatcherTransfer&) {}
        }
        if(context.pc!=returned || getRegU32(&context,29)!=0x01ffffc0u || getRegU32(&context,16)!=object
            || runtime.memory().read32(0x29000u)!=object || runtime.memory().read32(0x29004u)!=slot
            || std::memcmp(expected.data(),ram+model-16u,expected.size())!=0)
            throw std::runtime_error("Original versus animation changed frame wrapping, caller state, or memory guards");
    }
}
void verify_draw_setup(PS2Runtime& runtime,uint8_t* ram) {
    auto require=[](bool ok,const char* message) {
        if(!ok)throw std::runtime_error(message);
    };
    for(auto entry:translation::draw_setup_entries)
        require(runtime.hasFunction(entry),"Draw setup callback is not registered at its own instruction");
    constexpr uint32_t gp=0x003ac4f0u,physical=0x01df2680u,alias=0x21df2680u;
    constexpr uint32_t sp=0x01fffba0u,returned=0x0013a214u;
    auto& memory=runtime.memory();
    auto call=[&](uint32_t entry,uint32_t object) {
        R5900Context context{};context.pc=entry;
        context.r[4]=_mm_cvtsi32_si128(object);context.r[28]=_mm_cvtsi32_si128(gp);
        context.r[29]=_mm_cvtsi32_si128(sp);context.r[31]=_mm_cvtsi32_si128(returned);
        for(unsigned i=16;i<=23u;++i)context.r[i]=_mm_set_epi32(0x4400+i,0x3300+i,0x2200+i,0x1100+i);
        const auto saved=context;
        for(unsigned step=0;step<32u && context.pc!=returned;++step) {
            require(runtime.hasFunction(context.pc),"Draw setup reached an untranslated continuation");
            runtime.lookupFunction(context.pc)(ram,&context,&runtime);
        }
        require(context.pc==returned && getRegU32(&context,28)==gp && getRegU32(&context,29)==sp,
                "Draw setup did not return with its caller frame intact");
        for(unsigned i=16;i<=23u;++i)require(std::memcmp(&saved.r[i],&context.r[i],16u)==0,
                "Draw setup changed a saved register");
    };
    constexpr uint32_t globals[]={gp-0x7f6cu,gp-0x7884u,gp-0x7880u,gp-0x787cu,gp-0x7878u,gp-0x7874u};
    constexpr uint32_t values[]={0x3fa00000u,0x40200000u,0x40700000u,0x40900000u,0x40b00000u,0x40d00000u};
    for(unsigned variant=0;variant<8u;++variant)for(auto address:{physical,alias}) {
        std::memset(ram+physical-16u,0xa5u,0x2a0u);
        memory.write32(physical+0x260u,0x41200000u);memory.write32(physical+0x264u,0x41400000u);
        for(unsigned i=0;i<6u;++i)memory.write32(globals[i],values[i]);
        std::array<uint8_t,0x2a0u> expected{};std::memcpy(expected.data(),ram+physical-16u,expected.size());
        auto expect=[&](unsigned offset,uint32_t value){std::memcpy(expected.data()+16u+offset,&value,4u);};
        auto expectedGlobals=std::array<uint32_t,6>{};std::copy(std::begin(values),std::end(values),expectedGlobals.begin());
        switch(variant) {
        case 0:expect(0x24u,values[0]);break;
        case 1:expect(0x88u,values[1]);expect(0x8cu,values[2]);break;
        case 2:expect(0xf0u,0x54000u);expect(0x24u,values[5]);break;
        case 3:expect(0xf0u,0x50000u);break;
        case 4:expectedGlobals[1]=values[3];expectedGlobals[2]=values[4];break;
        case 5:expectedGlobals[3]=values[1];expectedGlobals[4]=values[2];break;
        case 6:expectedGlobals[1]=0x41200000u;expectedGlobals[2]=0x41400000u;break;
        case 7:expect(0x260u,values[1]);expect(0x264u,values[2]);break;
        }
        call(translation::draw_setup_entries[variant],address);
        require(std::memcmp(expected.data(),ram+physical-16u,expected.size())==0,
                "Draw setup changed the wrong packet fields or adjacent guards");
        for(unsigned i=0;i<6u;++i)require(memory.read32(globals[i])==expectedGlobals[i],
                "Draw setup changed the wrong shared float fields");
    }
    // Recreate the logged selector and the actual packet-builder JALR. The
    // object is the uncached 0x20000000 RAM alias seen in the retail trace.
    memory.write32(gp-0x7b48u,1u);
    R5900Context selector{};selector.pc=0x001a9460u;
    selector.r[4]=_mm_cvtsi32_si128(0x27000u);selector.r[6]=_mm_cvtsi32_si128(0x10802a1du);
    selector.r[28]=_mm_cvtsi32_si128(gp);selector.r[31]=_mm_cvtsi32_si128(returned);
    runtime.lookupFunction(selector.pc)(ram,&selector,&runtime);
    require(selector.pc==returned && memory.read32(0x003b3114u)==0x001a9380u,
            "Original selector did not choose the logged depth initializer");
    memory.write32(gp-0x7f6cu,0x3fc00000u);memory.write32(physical+0x24u,0xdeadbeefu);
    memory.write32(sp+0xd8u,0u);memory.write32(sp+0xd4u,0u);
    R5900Context caller{};caller.pc=0x0013a1f0u;
    caller.r[18]=_mm_cvtsi32_si128(alias);caller.r[28]=_mm_cvtsi32_si128(gp);caller.r[29]=_mm_cvtsi32_si128(sp);
    // Stop at the callback's return boundary, before the rest of the packet
    // builder consumes the scene's geometry. Run the original callback first;
    // the dispatcher transfer only bounds this isolated caller fixture.
    constexpr uint32_t bridge=0x001908e8u,initializer=0x001a9380u;
    struct Restore {
        PS2Runtime& runtime;uint32_t entry;PS2Runtime::RecompiledFunction original;
        ~Restore(){runtime.replaceFunction(entry,original);}
    } restoreBridge{runtime,bridge,runtime.lookupFunction(bridge)},
      restoreInitializer{runtime,initializer,runtime.lookupFunction(initializer)};
    runtime.replaceFunction(bridge,restoreInitializer.original);
    runtime.replaceFunction(initializer,[](uint8_t* data,R5900Context* context,PS2Runtime* active) {
        active->lookupFunction(bridge)(data,context,active);
        throw EeDispatcherTransfer{};
    });
    for(unsigned step=0;step<32u && caller.pc!=returned;++step) {
        require(runtime.hasFunction(caller.pc),"Original draw caller reached an untranslated callback");
        try { runtime.lookupFunction(caller.pc)(ram,&caller,&runtime); }
        catch(const EeDispatcherTransfer&) {}
    }
    require(caller.pc==returned && memory.read32(physical+0x24u)==0x3fc00000u
            && getRegU32(&caller,18)==alias && getRegU32(&caller,29)==sp,
            "Original packet caller did not run the depth initializer through the logged RAM alias");
}
}
