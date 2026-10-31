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
    if (getRegU32(&context,2)!=1u || scheduler.currentThread()->currentPriority!=1)
        throw std::runtime_error("SDK InitThread left the bootstrap thread at an unusable worker priority: result="
            +std::to_string(getRegU32(&context,2))+", priority="
            +std::to_string(scheduler.currentThread()->currentPriority));
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
    for(unsigned restart=0;restart<2u;++restart){
        auto* target=scheduler.thread(id);
        require(scheduler.startThread(id,0x1234u,context,true)==0,"Cannot start VU worker fixture");
        alignas(16) float constant[4];_mm_store_ps(constant,target->context.vu0_vf[0]);
        require(constant[0]==0.0f && constant[1]==0.0f && constant[2]==0.0f && constant[3]==1.0f,
            "Started EE worker lost its constant VU vector");
        require(_mm_cvtsi128_si32(_mm_castps_si128(target->context.vu0_r))==0x3f800000,
            "Started EE worker lost its VU random seed");
        target->context.vu0_vf[0]=_mm_setzero_ps();uint32_t stack=0;
        require(scheduler.terminateThread(id,stack,true)==0,"Cannot stop VU worker fixture");
    }
}
void verify_thread_wakeup() {
    auto owned=std::make_unique<PS2Runtime>();
    auto& runtime=*owned;
    require(runtime.memory().initialize(),"Cannot allocate EE wake regression memory");
    auto* ram=runtime.memory().getRDRAM();
    R5900Context context{};
    runtime.eeScheduler().bindMainContextForSyscall(context,ram);
    auto& scheduler=runtime.eeScheduler();
    EeThreadCreateParams params{};
    params.entry=0x001587f0u;params.stack=0x003584c0u;
    params.stackSize=0x800u;params.priority=25;
    const int id=scheduler.createThread(params);
    auto* worker=scheduler.thread(id);
    require(worker!=nullptr,"Cannot create EE wake regression worker");
    worker->status=EeThreadStatus::WaitingSuspended;
    worker->suspendCount=1;
    worker->wait={EeWaitReason::Sleep,std::monostate{}};
    context.r[4]=_mm_cvtsi32_si128(id);
    ps2_syscalls::ResumeThread(ram,&context,&runtime);
    require(int(getRegU32(&context,2))==id && worker->status==EeThreadStatus::Waiting
            && worker->wait.reason==EeWaitReason::Sleep && worker->suspendCount==0,
            "ResumeThread did not return the target ID while preserving its sleep wait");
    ps2_syscalls::WakeupThread(ram,&context,&runtime);
    require(int(getRegU32(&context,2))==id && worker->status==EeThreadStatus::Ready
            && worker->wait.reason==EeWaitReason::None && worker->wakeupCount==0u,
            "WakeupThread did not return the target ID and release the sleep wait");
    ps2_syscalls::iWakeupThread(ram,&context,&runtime);
    require(int(getRegU32(&context,2))==id && worker->wakeupCount==1u,
            "iWakeupThread did not return the target ID and queue a wake");
    ps2_syscalls::ResumeThread(ram,&context,&runtime);
    require(int32_t(getRegU32(&context,2))==scheduler.resumeThread(id,false)
            && int32_t(getRegU32(&context,2))<0,"ResumeThread hid a non-suspended error");
    for (const int invalid:{0,1,127}) {
        context.r[4]=_mm_cvtsi32_si128(invalid);
        const int expected=scheduler.wakeupThread(invalid,false);
        ps2_syscalls::WakeupThread(ram,&context,&runtime);
        require(expected<0 && int32_t(getRegU32(&context,2))==expected,
                "WakeupThread changed an invalid/self thread error");
        ps2_syscalls::iWakeupThread(ram,&context,&runtime);
        require(int32_t(getRegU32(&context,2))==expected,"iWakeupThread changed an error");
    }
    const int dormantId=scheduler.createThread(params);
    require(scheduler.thread(dormantId)!=nullptr,"Cannot create dormant wake regression worker");
    context.r[4]=_mm_cvtsi32_si128(dormantId);
    const int dormant=scheduler.wakeupThread(dormantId,false);
    ps2_syscalls::WakeupThread(ram,&context,&runtime);
    require(dormant<0 && int32_t(getRegU32(&context,2))==dormant,
            "WakeupThread accepted a dormant thread");
}
void verify_thread_priority() {
    auto owned=std::make_unique<PS2Runtime>();
    auto& runtime=*owned;
    require(runtime.memory().initialize(),"Cannot allocate EE priority regression memory");
    auto* ram=runtime.memory().getRDRAM();
    auto& ee=runtime.eeScheduler();
    R5900Context context{};
    ee.bindMainContextForSyscall(context,ram);
    int old=0;
    require(ee.changePriority(1,24,false,old)==0,"Cannot set initial EE priority");
    for (const int id:{1,0}) {
        context.r[4]=_mm_cvtsi32_si128(id);
        context.r[5]=_mm_cvtsi32_si128(1);
        ps2_syscalls::ChangeThreadPriority(ram,&context,&runtime);
        const int saved=int(getRegU32(&context,2));
        require(saved==24 && ee.currentThread()->currentPriority==1,
                "ChangeThreadPriority did not return the previous priority");
        context.r[5]=_mm_cvtsi32_si128(saved);
        ps2_syscalls::ChangeThreadPriority(ram,&context,&runtime);
        require(getRegU32(&context,2)==1u && ee.currentThread()->currentPriority==24,
                "ChangeThreadPriority result could not restore the caller's priority");
    }
    EeThreadCreateParams params{};
    params.entry=0x001587f0u;params.stack=0x003584c0u;
    params.stackSize=0x800u;params.priority=25;
    const int id=ee.createThread(params);
    require(ee.thread(id)!=nullptr,"Cannot create priority regression worker");
    context.r[4]=_mm_cvtsi32_si128(id);
    context.r[5]=_mm_cvtsi32_si128(30);
    ps2_syscalls::iChangeThreadPriority(ram,&context,&runtime);
    require(getRegU32(&context,2)==25u && ee.thread(id)->currentPriority==30,
            "iChangeThreadPriority did not return the worker's previous priority");
    context.r[5]=_mm_cvtsi32_si128(25);
    ps2_syscalls::iChangeThreadPriority(ram,&context,&runtime);
    require(getRegU32(&context,2)==30u && ee.thread(id)->currentPriority==25,
            "Interrupt-safe priority restoration failed");
    for (const int invalid:{-1,0,128}) {
        const int expected=ee.changePriority(id,invalid,false,old);
        context.r[5]=_mm_cvtsi32_si128(invalid);
        ps2_syscalls::ChangeThreadPriority(ram,&context,&runtime);
        require(expected<0 && int32_t(getRegU32(&context,2))==expected
                && ee.thread(id)->currentPriority==25,"Invalid priority changed the thread or error result");
        ps2_syscalls::iChangeThreadPriority(ram,&context,&runtime);
        require(int32_t(getRegU32(&context,2))==expected,"Interrupt-safe priority change hid an error");
    }
    context.r[4]=_mm_cvtsi32_si128(127);
    context.r[5]=_mm_cvtsi32_si128(24);
    const int unknown=ee.changePriority(127,24,false,old);
    ps2_syscalls::ChangeThreadPriority(ram,&context,&runtime);
    require(unknown<0 && int32_t(getRegU32(&context,2))==unknown,
            "ChangeThreadPriority accepted an unknown thread");
}
}
