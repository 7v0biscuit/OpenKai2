// SPDX-License-Identifier: GPL-3.0-only
#include "asset_checks.h"
#include "ps2_runtime.h"
#include "openkai2_translation_entries.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace openkai2 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime asset relocation regression: ")+message);
}
}
void verify_asset_relocation(PS2Runtime& runtime, uint8_t* memory) {
    constexpr uint32_t object=0x003f9910u, size=0x1000u;
    constexpr uint32_t stack=0x01ffff70u, returned=0x0019de98u, gp=0x003ac4f0u;
    for (auto entry:translation::asset_relocation_entries)
        require(runtime.hasFunction(entry), "asset handler is not registered");
    std::memcpy(memory+0x0033d680u, translation::asset_relocation_words.data(),
                translation::asset_relocation_words.size()*4);
    std::array<uint8_t,size+32> expected{};
    auto reset=[&](unsigned handler, uint32_t count) {
        std::memset(memory+object-16, 0xa5, size+32);
        std::memset(memory+object, 0, size);
        memory[object]='#';
        const uint32_t tag=translation::asset_relocation_words[handler*2+1];
        std::memcpy(memory+object+1, &tag, 3);
        runtime.memory().write32(object+4, 0x20u);
        runtime.memory().write32(object+0x0c, 2u);
        runtime.memory().write32(object+0x10, count);
    };
    auto put=[&](uint32_t offset, uint32_t value) { runtime.memory().write32(object+offset, value); };
    auto snapshot=[&] { std::memcpy(expected.data(), memory+object-16, expected.size()); expected[16]='!'; };
    auto expect=[&](uint32_t offset, uint32_t value) { std::memcpy(expected.data()+16+offset, &value, 4); };
    auto relocate=[&](uint32_t offset) { expect(offset, object+runtime.memory().read32(object+offset)); };
    auto call=[&] {
        R5900Context context{};
        context.pc=0x0012a680u;
        context.r[4]=_mm_cvtsi32_si128(object);
        context.r[28]=_mm_cvtsi32_si128(gp);
        context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(returned);
        for (unsigned reg=16; reg<=23; ++reg)
            context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
        const auto saved=context;
        for (unsigned step=0; step<256 && context.pc!=returned; ++step) {
            require(runtime.hasFunction(context.pc), "dispatcher reached an unregistered continuation");
            runtime.lookupFunction(context.pc)(memory, &context, &runtime);
        }
        require(context.pc==returned && getRegU32(&context,29)==stack
                && getRegU32(&context,31)==returned && getRegU32(&context,28)==gp,
                "compiled dispatcher did not restore the caller frame");
        for (unsigned reg=16; reg<=23; ++reg)
            require(std::memcmp(&context.r[reg], &saved.r[reg], 16)==0, "dispatcher changed a saved register");
        return getRegU32(&context,2);
    };
    auto check=[&] {
        require(call()==1u, "dispatcher did not finish relocation");
        require(std::memcmp(expected.data(), memory+object-16, expected.size())==0,
                "relocated pointers, payload, or adjacent guards differ");
        require(call()==0u, "already relocated asset did not return zero");
        require(std::memcmp(expected.data(), memory+object-16, expected.size())==0,
                "already relocated asset was modified again");
    };
    for (unsigned handler=0; handler<7; ++handler) {
        reset(handler, 0u); snapshot(); check();
        reset(handler, 2u); snapshot(); check();
    }

    reset(2, 4); put(0x14,0x40); put(0x18,2); put(0x1c,0x300);
    put(0x40,9); put(0x4c,0x100); put(0x50,25); put(0x5c,0x180);
    put(0x60,0x77); put(0x6c,0x240);
    put(0x100,0x280); put(0x10c,0x290);
    put(0x180,0x2a0); put(0x188,0x2b0); put(0x190,0x2c0);
    snapshot();
    for (auto offset:{0x14u,0x1cu,0x4cu,0x5cu,0x6cu,0x100u,0x10cu,0x180u,0x188u,0x190u}) relocate(offset);
    check();

    reset(1,2); put(0x14,0x40); put(0x1c,0x300); put(0x4c,0x100); put(0x100,0x200);
    snapshot(); check();

    reset(0,2); put(0x14,0x40); put(0x1c,0x300);
    put(0x74,0x100); put(0x78,0x200); put(0x80,0x240);
    put(0x104,0x280); put(0x10c,0x2c0);
    snapshot();
    for (auto offset:{0x14u,0x1cu,0x74u,0x78u,0x80u,0x104u,0x10cu}) relocate(offset);
    expect(0xd8,1u);
    check();

    reset(4,2); put(0x14,0x40); put(0x1c,0x300);
    put(0x40,13); put(0x4c,0x100); put(0x50,29); put(0x5c,0x180);
    put(0x100,0x200); put(0x108,0x240); put(0x184,0x280); put(0x18c,0x2c0);
    snapshot();
    for (auto offset:{0x14u,0x1cu,0x4cu,0x5cu,0x100u,0x108u,0x184u,0x18cu}) relocate(offset);
    check();

    reset(5,2); put(0x14,0x40); put(0x1c,0x300);
    put(0x40,10); put(0x4c,0x100); put(0x50,75); put(0x5c,0x180);
    put(0x100,0x200); put(0x184,0x240); put(0x18c,0x280);
    snapshot();
    for (auto offset:{0x14u,0x1cu,0x4cu,0x5cu,0x100u,0x184u,0x18cu}) relocate(offset);
    check();

    reset(6,2); put(0x14,0x40); put(0x18,3); put(0x1c,0x300); put(0x4c,0x100);
    put(0x100,0x200); put(0x108,0x240);
    snapshot();
    for (auto offset:{0x14u,0x1cu,0x4cu,0x100u,0x108u}) relocate(offset);
    check();

    reset(3,1); put(0x14,0x40); put(0x18,1); put(0x1c,0x300); put(0x20,1); put(0x24,0x280);
    put(0x44,0x100); put(0x48,0x180); put(0x4c,0x200); put(0x50,0x240);
    put(0x108,0x2c0); put(0x300,0x400);
    std::memcpy(memory+object+0x400,"#AMM",4);
    put(0x410,1); put(0x414,0x40); put(0x418,1); put(0x440,9); put(0x44c,0x80); put(0x480,0xc0);
    snapshot();
    for (auto offset:{0x14u,0x1cu,0x24u,0x44u,0x48u,0x4cu,0x50u,0x108u,0x300u}) relocate(offset);
    expected[16+0x400]='!';
    expect(0x414,object+0x440); expect(0x44c,object+0x480); expect(0x480,object+0x4c0);
    check();
}
}
