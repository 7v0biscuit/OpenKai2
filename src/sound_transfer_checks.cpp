// SPDX-License-Identifier: GPL-3.0-only
#include "sound_transfer_checks.h"
#include "ps2_runtime.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace openkai2 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime sound transfer regression: ") + message);
}
}
void verify_sound_transfer(PS2Runtime& runtime, uint8_t* memory) {
    constexpr uint32_t entry=0x00116268u, returned=0x0019de98u;
    constexpr uint32_t source=0x006c1180u, destination=0x0017d500u, length=0x800u;
    constexpr uint32_t stack=0x01ffffc0u, gp=0x003ac4f0u, descriptor=0x003ad700u;
    std::array<uint8_t, length+32> expected{}, actual{}, untouched{};
    expected.fill(0xa5);
    untouched.fill(0xa5);
    for (uint32_t i=0; i<length; ++i) expected[i+16]=uint8_t(i*17u+(i>>8));
    std::memcpy(memory+source-16, expected.data(), expected.size());
    auto call=[&](uint32_t src, uint32_t dst, uint32_t size, uint32_t mode, bool caller=false) {
        require(runtime.writeIopMemory(destination-16, untouched.data(), untouched.size()), "cannot seed IOP memory");
        std::memset(memory+descriptor, 0x66, 16);
        R5900Context context{};
        context.pc=entry;
        const std::array<uint32_t,4> args{src,dst,size,mode};
        for (unsigned i=0; i<args.size(); ++i) context.r[4+i]=_mm_cvtsi32_si128(args[i]);
        for (unsigned reg=16; reg<=23; ++reg)
            context.r[reg]=_mm_set_epi32(0x4400+reg,0x3300+reg,0x2200+reg,0x1100+reg);
        const auto saved=context;
        context.r[28]=_mm_cvtsi32_si128(gp);
        context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(caller ? 0x00146b10u : returned);
        if (caller) {
            std::memcpy(memory+stack+0x10, &saved.r[16], 16);
            runtime.memory().write64(stack+0x20, returned);
            runtime.memory().write32(gp-0x7a14u, 0u);
            context.r[16]=_mm_cvtsi32_si128(length);
        }
        for (unsigned step=0; step<256 && context.pc!=returned; ++step) {
            require(runtime.hasFunction(context.pc), "unregistered SDK or caller continuation");
            runtime.lookupFunction(context.pc)(memory, &context, &runtime);
        }
        require(context.pc==returned, "transfer did not finish within the execution bound");
        require(getRegU32(&context,29)==stack+(caller ? 0x30u : 0u)
                && getRegU32(&context,31)==returned && getRegU32(&context,28)==gp,
                "transfer changed the outer frame");
        for (unsigned reg=16; reg<=23; ++reg)
            require(std::memcmp(&context.r[reg], &saved.r[reg], 16)==0, "transfer changed a saved register");
        require(std::memcmp(memory+source-16, expected.data(), expected.size())==0, "transfer modified EE source bytes");
        require(runtime.readIopMemory(destination-16, actual.data(), actual.size()), "cannot inspect IOP memory");
        if (caller) require(runtime.memory().read32(gp-0x7a14u)==length, "caller did not advance the transferred count");
        return getRegU32(&context,2);
    };
    for (const auto mode : {1u,2u}) {
        require(call(source,destination,length,mode)==0u && actual==expected, "sound payload or transfer return value is wrong");
        require(runtime.memory().read32(descriptor)==source
                && runtime.memory().read32(descriptor+4)==destination
                && runtime.memory().read32(descriptor+8)==length
                && runtime.memory().read32(descriptor+12)==0u, "SDK DMA descriptor is wrong");
    }
    for (const auto alias : {0x80000000u,0xa0000000u,0x20000000u})
        require(call(source|alias,destination,length,1u)==0u && actual==expected, "EE source alias copy failed");
    for (const auto alias : {0x80000000u,0xa0000000u})
        require(call(source,destination|alias,length,1u)==0u && actual==expected, "IOP destination alias copy failed");
    require(call(source,destination,length,0u)==0xffffffffu && actual==untouched,
            "mode zero did not reject the transfer");
    for (unsigned i=0; i<16; ++i) require(memory[descriptor+i]==0x66, "mode zero submitted a descriptor");
    require(call(source,0x00200000u,length,1u)==0xffffffffu && actual==untouched, "invalid IOP destination was accepted");
    require(call(source,0x001ffff0u,length,1u)==0xffffffffu && actual==untouched, "IOP boundary crossing was accepted");
    require(call(source,destination,PS2_RAM_SIZE+1u,1u)==0xffffffffu && actual==untouched, "oversized transfer was accepted");
    require(call(source,destination,length,1u,true)==0u && actual==expected, "logged sound caller failed");
}
}
