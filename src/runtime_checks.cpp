// SPDX-License-Identifier: GPL-3.0-only
#include "runtime_checks.h"
#include "sound_checks.h"
#include "iop_checks.h"
#include "ps2_runtime.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace openkai2 {
int run_runtime_checks() {
    verify_iop_import_pipeline();
    std::cout << "IOP import pipeline regression passed: thread arguments, RPC handler, and return values.\n";
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
    return 0;
}
} // namespace openkai2
