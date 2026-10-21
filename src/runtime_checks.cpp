// SPDX-License-Identifier: GPL-3.0-only
#include "runtime_checks.h"
#include "sound_checks.h"
#include "iop_checks.h"
#include "iop_wait_checks.h"
#include "ps2_runtime.h"
#include "openkai2_translation_entries.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace openkai2 {
int run_runtime_checks() {
    verify_iop_wait_diagnostics();
    std::cout << "IOP wait diagnostics regression passed: read-only semaphore, event, and instruction snapshots.\n";
    verify_iop_import_pipeline();
    std::cout << "IOP import pipeline regression passed: thread arguments, RPC handler, and return values.\n";
    verify_iop_module_startup();
    std::cout << "IOP module startup regression passed: nested RPC worker registration, sleep, and bounded scheduling.\n";
    verify_sound_registers();
    std::cout << "SPU2 initialization regression passed: both cores, DMA status, and guest polling.\n";
    constexpr uint32_t entry=0x00167188, caller=0x00162dec, returned=0x00162df4;
    PS2Runtime runtime;
    if (!runtime.hasFunction(entry))
        throw std::runtime_error("Runtime regression: startup callback 0x00167188 is not registered");
    if (!runtime.memory().initialize()) throw std::runtime_error("Cannot allocate regression-test guest memory");
    auto* memory=runtime.memory().getRDRAM();
    for (const uint32_t second : {0u,0x12345678u}) {
        R5900Context context{};
        context.pc=entry;
        context.r[4]=_mm_cvtsi32_si128(0x00162b48);
        context.r[5]=_mm_cvtsi32_si128(second);
        context.r[29]=_mm_cvtsi32_si128(0x01fffe40);
        context.r[31]=_mm_cvtsi32_si128(returned);
        std::memset(memory+0x0035fb20,0xcc,16);
        const auto resumed=runtime.dispatchGuestBranch(memory,&context,entry,caller,returned,
            PS2Runtime::GuestBranchKind::IndirectCall,"JALR regression");
        uint32_t words[4]{};
        std::memcpy(words,memory+0x0035fb20,sizeof(words));
        if (!resumed || context.pc!=returned || getRegU32(&context,29)!=0x01fffe40
            || getRegU32(&context,31)!=returned || words[0]!=0xcccccccc
            || words[1]!=0x00162b48 || words[2]!=second || words[3]!=0xcccccccc)
            throw std::runtime_error("Runtime regression: startup callback dispatch, memory writes, or return failed");
    }
    std::cout << "Runtime callback regression passed: 0x00167188, argument stores, delay slot, and return.\n";
    for (const auto address : translation::entries)
        if (!runtime.hasFunction(address))
            throw std::runtime_error("Runtime regression: reviewed indirect method is not registered");
    constexpr uint32_t stack=0x01fffe40u;
    for (const auto address : translation::zero_return_entries) {
        R5900Context context{};
        context.pc=address;
        for (unsigned reg=1;reg<32;++reg)
            context.r[reg]=_mm_set_epi32(0x11223344,0x55667788,0x12345678,int(0x20000000u+reg));
        context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(0x0019dfa0);
        const auto original=context;
        std::memset(memory+stack-64,0xA5,128);
        if (!runtime.dispatchGuestBranch(memory,&context,address,0x0019df98u,0x0019dfa0u,
                PS2Runtime::GuestBranchKind::IndirectCall,"JALR method regression")
            || context.pc!=0x0019dfa0u || _mm_cvtsi128_si64(context.r[2])!=0)
            throw std::runtime_error("Runtime regression: indirect method return or delay slot failed");
        for (unsigned reg=0;reg<32;++reg)
            if (reg!=2 && _mm_movemask_epi8(_mm_cmpeq_epi8(context.r[reg],original.r[reg]))!=0xFFFF)
                throw std::runtime_error("Runtime regression: leaf method changed a preserved register");
        for (unsigned offset=0;offset<128;++offset)
            if (memory[stack-64+offset]!=0xA5)
                throw std::runtime_error("Runtime regression: leaf method changed the caller's stack");
    }
    // Run the actual startup dispatcher against two isolated method tables.
    // Both original leaf methods return zero, so it must advance to the second
    // table and then return through its own saved caller frame.
    const uint32_t tables[]={0x21000u,0x21100u,0u};
    const uint32_t methods[]={0x001908e0u,0x00192390u};
    std::memcpy(memory+0x20000,tables,sizeof(tables));
    std::memcpy(memory+tables[0],&methods[0],4);
    std::memcpy(memory+tables[1],&methods[1],4);
    R5900Context dispatcher{};
    dispatcher.pc=0x0019df48u;
    dispatcher.r[4]=_mm_cvtsi32_si128(0x20000);
    dispatcher.r[16]=_mm_cvtsi32_si128(0x1234);
    dispatcher.r[17]=_mm_cvtsi32_si128(0x2345);
    dispatcher.r[18]=_mm_cvtsi32_si128(0x3456);
    dispatcher.r[29]=_mm_cvtsi32_si128(stack);
    dispatcher.r[31]=_mm_cvtsi32_si128(0x0019de98);
    if (!runtime.dispatchGuestBranch(memory,&dispatcher,0x0019df48u,0x0019de90u,0x0019de98u,
            PS2Runtime::GuestBranchKind::IndirectCall,"startup dispatcher regression")
        || dispatcher.pc!=0x0019de98u || getRegU32(&dispatcher,29)!=stack
        || getRegU32(&dispatcher,16)!=0x1234u || getRegU32(&dispatcher,17)!=0x2345u
        || getRegU32(&dispatcher,18)!=0x3456u || getRegU32(&dispatcher,4)!=0u)
        throw std::runtime_error("Runtime regression: startup method-table dispatch did not return correctly");
    std::cout << "Runtime dispatch table regression passed: 98 methods, 39 leaf returns, and compiled caller.\n";
    return 0;
}
} // namespace openkai2
