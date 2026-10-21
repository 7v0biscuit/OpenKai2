// SPDX-License-Identifier: GPL-3.0-only
#include "thread_checks.h"
#include "ps2_syscalls.h"
#include "runtime/ee_scheduler.h"
#include <memory>
#include <stdexcept>

namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
void verify_thread_bootstrap() {
    auto owned=std::make_unique<PS2Runtime>();
    auto& runtime=*owned;
    require(runtime.memory().initialize(),"Cannot allocate EE thread regression memory");
    auto* ram=runtime.memory().getRDRAM();
    auto& scheduler=runtime.eeScheduler();
    R5900Context context{};
    context.pc=0x00112e00u;
    context.r[29]=_mm_cvtsi32_si128(PS2_RAM_SIZE-16u);
    context.r[28]=_mm_cvtsi32_si128(0x003ac4f0u);
    scheduler.bindMainContextForSyscall(context,ram);
    require(scheduler.currentThread()->currentPriority==0,"Bootstrap thread did not start at kernel priority zero");
    ps2_syscalls::InitThread(ram,&context,&runtime);
    require(getRegU32(&context,2)==1u && scheduler.currentThread()->currentPriority==1,
            "SDK InitThread left the bootstrap thread at an unusable worker priority");
    constexpr uint32_t descriptor=0x24000u, status=0x24100u;
    context.r[4]=_mm_cvtsi32_si128(1);
    context.r[5]=_mm_cvtsi32_si128(status);
    ps2_syscalls::ReferThreadStatus(ram,&context,&runtime);
    require(getRegU32(&context,2)==0u,"Cannot query initialized EE thread priority");
    const auto inherited=runtime.memory().read32(status+0x18u);
    require(inherited==1u,"ReferThreadStatus did not expose the initialized priority");
    runtime.memory().write32(descriptor+4u,0x00158540u);
    runtime.memory().write32(descriptor+8u,0x003534b0u);
    runtime.memory().write32(descriptor+12u,0x800u);
    runtime.memory().write32(descriptor+16u,0x003ac4f0u);
    runtime.memory().write32(descriptor+20u,inherited);
    context.r[4]=_mm_cvtsi32_si128(descriptor);
    ps2_syscalls::CreateThread(ram,&context,&runtime);
    const int id=static_cast<int>(getRegU32(&context,2));
    const auto* worker=scheduler.thread(id);
    require(id==2 && worker && worker->currentPriority==1 && worker->entry==0x00158540u
            && worker->stack==0x003534b0u && worker->stackSize==0x800u
            && worker->gp==0x003ac4f0u && worker->status==EeThreadStatus::Dormant,
            "EE worker could not inherit the SDK-initialized main thread priority");
    for (const int invalid:{-1,0,128}) {
        runtime.memory().write32(descriptor+20u,static_cast<uint32_t>(invalid));
        context.r[4]=_mm_cvtsi32_si128(descriptor);
        ps2_syscalls::CreateThread(ram,&context,&runtime);
        require(static_cast<int32_t>(getRegU32(&context,2))==-403,
                "Invalid EE worker priority was accepted");
    }
    int old=0;
    require(scheduler.changePriority(1,24,false,old)==0,"Cannot set initialized thread priority");
    ps2_syscalls::InitThread(ram,&context,&runtime);
    require(scheduler.currentThread()->currentPriority==24 && scheduler.thread(3)==nullptr
            && getRegU32(&context,29)==PS2_RAM_SIZE-16u
            && getRegU32(&context,28)==0x003ac4f0u,
            "Repeated SDK initialization changed configured priority, registers, or thread state");
}
}
