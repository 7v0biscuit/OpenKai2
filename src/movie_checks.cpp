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
}
void verify_movie_callbacks(PS2Runtime& runtime, uint8_t* memory) {
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
    // Reconstruct the original audio binding installed by mwPly's stream
    // setup: real pan/volume adapters, ADX handle, decoder, and sound state.
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
        // EE integer arguments occupy signed 64-bit lanes, including pan.
        context.r[5]=_mm_cvtsi64_si128(int64_t(int32_t(a1)));
        context.r[6]=_mm_cvtsi64_si128(int64_t(int32_t(a2)));
        context.r[28]=_mm_cvtsi32_si128(gp); context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(returned);
        for (unsigned reg=16;reg<=23;++reg)
            context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
        const uint32_t finish=caller ? 0x002a4f00u : returned;
        if (caller) {
            // Resume after the real caller has computed the movie-record
            // address. Execute its table loads, JALR, and complete SDK chain,
            // stopping at the continuation before it opens the movie file.
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
    // The reported volume is zero. Include attenuation and SDK clamping while
    // checking the exact ADX halfword and the final ADXS sound-state word.
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
    // An initialized movie with no audio stream leaves audio state unchanged.
    put(backend+0x9b8,0u); snapshot(); invoke(11,uint32_t(-200)); check();
    for (uint32_t state:{0u,1u,3u}) {
        put(object+8,state); snapshot();
        require(invoke(8)==state, "movie status query returned the wrong state"); check();
    }
}
}
