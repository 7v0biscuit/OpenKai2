// SPDX-License-Identifier: GPL-3.0-only
#include "movie_checks.h"
#include "ps2_runtime.h"
#include "openkai2_translation_entries.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace openkai2 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime movie callback regression: ")+message);
}
void verify_movie_timing(PS2Runtime& runtime,uint8_t* memory) {
    constexpr uint32_t input=0x00258000u,outputs=0x00258100u;
    constexpr uint32_t stack=0x01ffff60u,returned=0x0019de98u,gp=0x003ac4f0u;
    std::memcpy(memory+0x00387420u,translation::movie_timing_rates.data(),
                translation::movie_timing_rates.size()*4);
    std::memcpy(memory+0x00387448u,translation::movie_timing_slots.data(),
                translation::movie_timing_slots.size()*4);
    const std::array<uint32_t,5> times[]={
        {0,0,0,0,0}, {0,0,1,0,0}, {0,1,0,0,0}, {0,9,59,17,1},
        {0,10,0,0,0}, {1,0,0,0,0}, {23,59,59,29,1},
    };
    for (unsigned rateIndex:{4u,0u,1u,2u,3u,5u,6u,7u,8u}) {
        const uint32_t rate=translation::movie_timing_rates[rateIndex];
        for (uint32_t mode:{0u,1u}) for (const auto& time:times) {
            std::memset(memory+input-16u,0xa5,64u);
            std::memset(memory+input,0,32u);
            std::memset(memory+outputs,0xcc,64u);
            std::memset(memory+stack-0x810u,0xa5,0x830u);
            runtime.memory().write32(input,rateIndex);
            runtime.memory().write32(input+4u,mode);
            std::memcpy(memory+input+8u,time.data(),time.size()*4);
            const std::vector<uint8_t> original(memory+input-16u,memory+input+48u);
            R5900Context context{};
            context.pc=0x0019d3a8u;
            context.r[4]=_mm_cvtsi32_si128(input);
            context.r[5]=_mm_cvtsi32_si128(outputs+16u);
            context.r[6]=_mm_cvtsi32_si128(outputs+32u);
            context.r[28]=_mm_cvtsi32_si128(gp);context.r[29]=_mm_cvtsi32_si128(stack);
            context.r[30]=_mm_cvtsi32_si128(0x1030u);context.r[31]=_mm_cvtsi32_si128(returned);
            for (unsigned reg=16;reg<=23;++reg)
                context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
            const auto saved=context;
            for (unsigned step=0;step<1024u && context.pc!=returned;++step) {
                require(runtime.hasFunction(context.pc),"time-code caller reached an unregistered continuation");
                runtime.lookupFunction(context.pc)(memory,&context,&runtime);
            }
            require(context.pc==returned && getRegU32(&context,29)==stack
                    && getRegU32(&context,28)==gp && getRegU32(&context,30)==0x1030u
                    && getRegU32(&context,31)==returned,
                    "time-code dispatcher did not restore its original frame");
            for (unsigned reg=16;reg<=23;++reg)
                require(std::memcmp(&context.r[reg],&saved.r[reg],16)==0,
                        "time-code dispatcher changed a saved register");
            const uint64_t h=time[0],m=time[1],s=time[2],f=time[3]+time[4];
            uint64_t clock=rate;
            if (rateIndex==1u) clock=24000u;
            if (rateIndex==4u) clock=30000u;
            if (rateIndex==7u) clock=60000u;
            uint32_t expected=uint32_t((h*3600u+m*60u+s)*clock+f*1000u);
            if (mode==1u && rateIndex==1u)
                expected=uint32_t((h*86292u+m*1438u+s*24u+2u*(m/10u)+f)*1000u);
            if (mode==1u && rateIndex==4u)
                expected=uint32_t((h*107892u+m*1798u+s*30u+2u*(m/10u)+f)*1000u);
            if (mode==1u && rateIndex==7u)
                expected=uint32_t((h*215892u+m*3598u+s*60u+2u*(m/10u)+f)*1000u);
            if (runtime.memory().read32(outputs+16u)!=(rateIndex ? expected : 0u)
                    || runtime.memory().read32(outputs+32u)!=(rateIndex ? rate : 1u))
                throw std::runtime_error("Runtime movie time-code conversion: rate="+std::to_string(rate)
                    +" mode="+std::to_string(mode)+" time="+std::to_string(h)+":"+std::to_string(m)
                    +":"+std::to_string(s)+":"+std::to_string(f)
                    +" expected="+std::to_string(rateIndex ? expected : 0u)+"/"+std::to_string(rateIndex ? rate : 1u)
                    +" actual="+std::to_string(runtime.memory().read32(outputs+16u))+"/"
                    +std::to_string(runtime.memory().read32(outputs+32u)));
            require(std::memcmp(memory+input-16u,original.data(),original.size())==0,
                    "time-code converter changed its input or neighboring memory");
            for (unsigned i=0;i<64u;++i)
                if (!(i>=16u && i<20u) && !(i>=32u && i<36u))
                    require(memory[outputs+i]==0xcc,"time-code converter crossed an output boundary");
            for (unsigned i=0;i<16u;++i)
                require(memory[stack+i]==0xa5 && memory[stack-0x810u+i]==0xa5,
                        "time-code converter crossed its stack boundary");
        }
    }
    for (auto entry:translation::movie_timing_entries)
        require(runtime.hasFunction(entry),"movie timing handler is not registered");
}
void verify_movie_packets(PS2Runtime& runtime,uint8_t* memory) {
    for (auto entry:translation::movie_packet_entries)
        require(runtime.hasFunction(entry),"movie packet handler is not registered");
    constexpr uint32_t backend=0x00250000u,work=0x00253400u,parser=0x00254000u;
    constexpr uint32_t ring=0x00255000u,payload=0x00260000u,buffer=0x00270000u;
    constexpr uint32_t outputs=0x00258000u,capacity=4096u,length=0x7eeu;
    constexpr uint32_t stack=0x01ffff60u,returned=0x0019de98u,gp=0x003ac4f0u;
    std::memcpy(memory+0x00387180u,translation::movie_packet_slots.data(),16);
    std::memcpy(memory+0x00367b68u,translation::stream_callback_slots.data(),
                translation::stream_callback_slots.size()*4);
    runtime.memory().write32(0x003c07a0u,0u);
    runtime.memory().write32(0x003c07a8u,0u);
    auto put=[&](uint32_t address,uint32_t value) { runtime.memory().write32(address,value); };
    struct Case { uint32_t type,position,free,maximum,bytes;bool copy,success; };
    const Case cases[]={
        {0u,0u,capacity,length,length,false,true},
        {1u,0u,capacity,length,length,false,true},
        {2u,0u,capacity,length,length,true,true},
        {3u,0u,capacity,length,length,false,true},
        {2u,capacity-6u,capacity,length,length,true,true},
        {2u,0u,length-1u,length,length,false,false},
        {2u,0u,capacity,length-1u,length,false,false},
        {2u,0u,capacity,0u,0u,false,true},
    };
    for (const auto& test:cases) {
        std::memset(memory+backend,0,0x3000u);
        std::memset(memory+work-0x2bcu,0,0x800u);
        std::memset(memory+parser,0,0x200u);
        std::memset(memory+ring-16u,0xa5,96u);
        std::memset(memory+ring,0,64u);
        std::memset(memory+payload-32u,0,length+64u);
        std::memset(memory+buffer-16u,0xcc,capacity+32u);
        std::memset(memory+outputs-16u,0xa5,40u);
        std::memset(memory+stack-0x810u,0xa5,0x830u);
        for (uint32_t i=0;i<length;++i) memory[payload+i]=uint8_t(i*17u+(i>>5));
        put(backend+0x1aecu,work);put(backend+0x1b00u,1u);
        put(work,parser);put(parser,2u);
        put(parser+0xa4u,test.type);put(parser+0xa8u,2u);
        put(parser+0xbcu,test.bytes);
        put(backend+0x1150u+0x74u+2u*16u,ring);
        put(ring,0x00367b68u);put(ring+4u,1u);put(ring+8u,0x003848c8u);
        put(ring+0x10u,test.free);put(ring+0x14u,test.position);put(ring+0x18u,test.position);
        put(ring+0x1cu,buffer);put(ring+0x20u,capacity);
        const std::vector<uint8_t> beforeBackend(memory+backend,memory+backend+0x3000u);
        R5900Context context{};
        context.pc=0x00191048u;
        context.r[4]=_mm_cvtsi32_si128(backend);context.r[5]=_mm_cvtsi32_si128(payload);
        context.r[6]=_mm_cvtsi32_si128(test.maximum);context.r[7]=_mm_cvtsi32_si128(outputs);
        context.r[8]=_mm_cvtsi32_si128(outputs+4u);
        context.r[28]=_mm_cvtsi32_si128(gp);context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(returned);
        for (unsigned reg=16;reg<=23;++reg)
            context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
        context.r[30]=_mm_cvtsi32_si128(0x1030u);
        const auto saved=context;
        for (unsigned step=0;step<1024u && context.pc!=returned;++step) {
            require(runtime.hasFunction(context.pc),"packet caller reached an unregistered continuation");
            runtime.lookupFunction(context.pc)(memory,&context,&runtime);
        }
        require(context.pc==returned && getRegU32(&context,2)==0u
                && getRegU32(&context,29)==stack && getRegU32(&context,28)==gp
                && getRegU32(&context,31)==returned && getRegU32(&context,30)==0x1030u,
                "packet caller did not return through its original frame");
        for (unsigned reg=16;reg<=23;++reg)
            require(std::memcmp(&context.r[reg],&saved.r[reg],16)==0,"packet caller changed a saved register");
        require(runtime.memory().read32(outputs)==(test.success ? test.bytes : 0u)
                && runtime.memory().read32(outputs+4u)==uint32_t(test.success),
                "packet caller lost its consumed-byte or completion result");
        const uint32_t written=test.copy ? test.bytes : 0u;
        require(runtime.memory().read32(ring+0xcu)==written
                && runtime.memory().read32(ring+0x10u)==test.free-written
                && runtime.memory().read32(ring+0x14u)==(test.position+written)%capacity
                && runtime.memory().read32(ring+0x18u)==test.position,
                "packet callback lost data, free space, or ring positions");
        for (uint32_t i=0;i<capacity;++i) {
            const uint32_t offset=(i+capacity-test.position)%capacity;
            const uint8_t expected=offset<written ? memory[payload+offset] : 0xcc;
            require(memory[buffer+i]==expected,"packet callback copied the wrong bytes or crossed the ring boundary");
        }
        for (uint32_t i=0;i<length;++i)
            require(memory[payload+i]==uint8_t(i*17u+(i>>5)),"packet callback changed its source payload");
        require(std::memcmp(memory+backend,beforeBackend.data(),beforeBackend.size())==0,
                "packet dispatch changed its backend configuration");
        for (unsigned i=0;i<16u;++i)
            require(memory[buffer-16u+i]==0xcc && memory[buffer+capacity+i]==0xcc
                    && memory[ring-16u+i]==0xa5 && memory[ring+64u+i]==0xa5
                    && memory[outputs-16u+i]==0xa5 && memory[outputs+8u+i]==0xa5
                    && memory[stack+i]==0xa5 && memory[stack-0x810u+i]==0xa5,
                    "packet callback overwrote neighboring memory");
    }
}
}
void verify_movie_callbacks(PS2Runtime& runtime, uint8_t* memory) {
    verify_movie_timing(runtime,memory);
    verify_movie_packets(runtime,memory);
    constexpr uint32_t table=0x00370ae0u, object=0x00370cb8u;
    constexpr uint32_t backend=0x00250000u, bridge=0x00253100u, adx=0x00254000u;
    constexpr uint32_t decoder=0x00255000u, sound=0x00256000u, metadata=0x00257000u;
    constexpr uint32_t gp=0x003ac4f0u, stack=0x01ffff60u, returned=0x0019de98u;
    for (auto entry:translation::movie_callback_entries)
        require(runtime.hasFunction(entry), "movie method is not registered");
    std::memcpy(memory+table,translation::movie_callback_slots.data(),translation::movie_callback_slots.size()*4);
    struct Region { uint32_t address, size; std::vector<uint8_t> expected; };
    std::array<Region,7> regions{{{object,0x210u,{}},{backend,0x3000u,{}},{bridge,0x10u,{}},
        {adx,0x100u,{}},{decoder,0x100u,{}},{sound,0x100u,{}},{metadata,0x100u,{}}}};
    auto put=[&](uint32_t address,uint32_t value) { runtime.memory().write32(address,value); };
    for (const auto& region:regions) {
        std::memset(memory+region.address-16,0xa5,region.size+32);
        std::memset(memory+region.address,0,region.size);
    }
    put(object,table); put(object+4,1u); put(object+0x0c,1u); put(object+0x40,backend);
    put(backend+0x48,1u); put(backend+0x9b8,1u);
    put(backend+0x1c84,backend+0x2a5c); put(backend+0x1b74,bridge); put(bridge,adx);
    put(backend+0x2a60,0x0018b1c8u); put(backend+0x2a64,0x0018b1e8u);
    put(backend+0x2a68,0x0018b208u); put(backend+0x2a6c,0x0018b228u);
    put(adx+4,decoder); put(adx+0x0c,sound); memory[adx+3]=2;
    put(decoder+4,metadata);
    auto snapshot=[&] {
        for (auto& region:regions)
            region.expected.assign(memory+region.address-16,memory+region.address+region.size+16);
    };
    auto expect=[&](uint32_t address,uint32_t value,unsigned width) {
        for (auto& region:regions) {
            if (address>=region.address && address+width<=region.address+region.size) {
                std::memcpy(region.expected.data()+16+address-region.address,&value,width);
                return;
            }
        }
        throw std::runtime_error("Movie regression expected write is outside its fixture");
    };
    auto check=[&] {
        for (const auto& region:regions) {
            const auto mismatch=std::mismatch(region.expected.begin(),region.expected.end(),memory+region.address-16);
            if (mismatch.first!=region.expected.end()) {
                const auto offset=uint32_t(mismatch.first-region.expected.begin());
                throw std::runtime_error("Runtime movie callback regression: unexpected byte at "
                    +std::to_string(region.address-16+offset)+" expected "+std::to_string(*mismatch.first)
                    +" got "+std::to_string(*mismatch.second));
            }
        }
    };
    auto invoke=[&](unsigned slot,uint32_t a1=0u,uint32_t a2=0u,bool caller=false) {
        R5900Context context{};
        context.pc=caller ? 0x002a4eecu : translation::movie_callback_slots[slot];
        context.r[4]=_mm_cvtsi32_si128(object);
        context.r[5]=_mm_cvtsi64_si128(int64_t(int32_t(a1)));
        context.r[6]=_mm_cvtsi64_si128(int64_t(int32_t(a2)));
        context.r[28]=_mm_cvtsi32_si128(gp); context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(returned);
        for (unsigned reg=16;reg<=23;++reg)
            context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
        const uint32_t finish=caller ? 0x002a4f00u : returned;
        if (caller) {
            context.r[2]=_mm_cvtsi32_si128(0x003b4f80u);
            context.r[16]=_mm_setzero_si128();
            context.r[17]=_mm_cvtsi32_si128(0x003b5a30u);
            put(0x003b4f80u,object);
        }
        const auto saved=context;
        for (unsigned step=0;step<1024 && context.pc!=finish;++step) {
            require(runtime.hasFunction(context.pc), "movie call reached an unregistered continuation");
            runtime.lookupFunction(context.pc)(memory,&context,&runtime);
        }
        require(context.pc==finish && getRegU32(&context,29)==stack
                && getRegU32(&context,31)==finish && getRegU32(&context,28)==gp,
                "movie call did not restore the caller frame");
        for (unsigned reg=16;reg<=23;++reg)
            require(std::memcmp(&context.r[reg],&saved.r[reg],16)==0, "movie call changed a saved register");
        return getRegU32(&context,2);
    };
    for (int32_t volume:{0,-123,-999,-2000,15}) {
        snapshot(); expect(adx+0x40,uint32_t(volume),2);
        expect(sound+0x30,uint32_t(std::clamp(volume,-999,0)),4);
        invoke(11,uint32_t(volume)); check();
        require(runtime.memory().read32(0x00371d78u)==backend, "movie backend validity check did not execute");
        require(invoke(12)==uint32_t(volume), "movie output-volume getter lost the signed value"); check();
    }
    snapshot(); expect(adx+0x40,0u,2); expect(sound+0x30,0u,4);
    invoke(11,0u,0x007fffffu,true); check();

    for (unsigned channel:{0u,1u}) {
        for (int32_t pan:{-20,0,20}) {
            snapshot(); expect(adx+0x42+channel*2,uint32_t(pan),2);
            expect(sound+0x38+channel*4,uint32_t(std::clamp(pan,-15,15)),4);
            invoke(13,channel,uint32_t(pan)); check();
            require(invoke(14,channel)==uint32_t(pan), "movie pan getter lost its channel or signed value"); check();
        }
    }
    put(backend+0x9b8,0u); snapshot(); invoke(11,uint32_t(-200)); check();
    for (uint32_t state:{0u,1u,3u}) {
        put(object+8,state); snapshot();
        require(invoke(8)==state, "movie status query returned the wrong state"); check();
    }
}
}
