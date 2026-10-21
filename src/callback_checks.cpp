// SPDX-License-Identifier: GPL-3.0-only
#include "callback_checks.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr uint32_t main_entry=0x001908e0u, continuation=0x001908e8u;
constexpr uint32_t callback_entry=0x00190918u, finish=0x00190920u, marker=0x25000u;
void interrupted(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    auto& memory=runtime->memory();
    const uint32_t sp=getRegU32(context,29)-32u;
    context->r[29]=_mm_cvtsi32_si128(sp);
    memory.write64(sp+16u,uint64_t(getRegU32(context,31)));
    context->pc=continuation;
    auto& scheduler=runtime->eeScheduler();
    scheduler.addIrqHandler(true,9u,getRegU32(context,16),true,0u,0u,0u);
    scheduler.dispatchIrq(true,9u);
}
void resume(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    const uint32_t sp=getRegU32(context,29);
    const uint32_t returned=uint32_t(runtime->memory().read64(sp+16u));
    context->r[29]=_mm_cvtsi32_si128(sp+32u);
    context->r[31]=_mm_cvtsi32_si128(returned);
    context->pc=returned;
}
void callback(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    const uint32_t sp=getRegU32(context,29)-16u;
    runtime->memory().write64(sp,uint64_t(getRegU32(context,31)));
    context->pc=uint32_t(runtime->memory().read64(sp));
}
void finished(uint8_t*,R5900Context* context,PS2Runtime* runtime) {
    runtime->memory().write32(marker,37u);
    context->pc=0u;
    runtime->requestStop();
}
class Binding {
    PS2Runtime& runtime_;
    uint32_t address_;
    PS2Runtime::RecompiledFunction original_;
public:
    Binding(PS2Runtime& runtime,uint32_t address,PS2Runtime::RecompiledFunction function)
        : runtime_(runtime),address_(address),original_(runtime.hasFunction(address)?runtime.lookupFunction(address):nullptr) {
        require(runtime.replaceFunction(address,function),"Cannot register callback regression program");
    }
    ~Binding() { runtime_.replaceFunction(address_,original_); }
};
}
void verify_callback_resume(PS2Runtime& runtime,unsigned callback_address) {
    auto& memory=runtime.memory();
    auto* ram=memory.getRDRAM();
    require(ram!=nullptr,"Missing callback regression memory");
    Binding entry(runtime,main_entry,interrupted), resumed(runtime,continuation,resume), end(runtime,finish,finished);
    // The standalone test uses a minimal EE callback prologue. The packaged
    // self-test supplies the original translated file-query callback instead.
    Binding synthetic(runtime,callback_entry,callback);
    const uint32_t handler=callback_address?callback_address:callback_entry;
    require(runtime.hasFunction(handler),"Compiled callback regression entry is missing");
    R5900Context context{};
    context.pc=main_entry;
    context.r[16]=_mm_cvtsi32_si128(handler);
    context.r[29]=_mm_cvtsi32_si128(PS2_RAM_SIZE-16u);
    context.r[31]=_mm_cvtsi32_si128(finish);
    auto& scheduler=runtime.eeScheduler();
    scheduler.reset(ram,context);
    memory.write32(marker,0u);
    std::mutex mutex;
    std::condition_variable completed;
    bool done=false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!completed.wait_for(lock,std::chrono::seconds(2),[&] { return done; })) scheduler.requestStop();
    });
    std::exception_ptr failure;
    try { scheduler.run(); } catch (...) { failure=std::current_exception(); }
    { std::lock_guard lock(mutex);done=true; }
    completed.notify_one();
    watchdog.join();
    if (failure) std::rethrow_exception(failure);
    require(memory.read32(marker)==37u,"EE callback lost the interrupted continuation");
    const auto* main=scheduler.thread(1);
    require(main && getRegU32(&main->context,29)==PS2_RAM_SIZE-16u
            && getRegU32(&main->context,31)==finish,
            "EE callback did not preserve the caller stack and return address");
}
void verify_callback_stacks() {
    {
        auto scheduled=std::make_unique<PS2Runtime>();
        require(scheduled->memory().initialize(),"Cannot allocate scheduled callback regression memory");
        verify_callback_resume(*scheduled);
    }
    // Each runtime contains over 500 KiB of device state. Keep test fixtures
    // off the host stack, including MSVC's default 1 MiB executable stack.
    auto owned_runtime=std::make_unique<PS2Runtime>();
    auto& runtime=*owned_runtime;
    auto& memory=runtime.memory();
    require(memory.initialize(),"Cannot allocate callback stack regression memory");
    auto* ram=memory.getRDRAM();
    constexpr uint32_t main_sp=PS2_RAM_SIZE-0x30u, returned=0x00186490u;
    R5900Context context{};
    context.pc=0x001863e4u;
    context.r[29]=_mm_cvtsi32_si128(main_sp);
    context.r[31]=_mm_cvtsi32_si128(returned);
    auto& scheduler=runtime.eeScheduler();
    scheduler.bindMainContextForSyscall(context,ram);
    std::memset(ram+main_sp,0xa5,0x30);
    const uint64_t saved_return=returned;
    std::memcpy(ram+main_sp+0x10,&saved_return,8);
    const std::vector<uint8_t> frame(ram+main_sp,ram+PS2_RAM_SIZE);
    const uint32_t callback_sp=scheduler.invocationStackTop();
    // A normal EE callback saves its zero return sentinel after subtracting
    // 16 bytes from SP. This used to land in the live bootstrap return frame.
    memory.write64(callback_sp-16u,0u);
    require(std::equal(frame.begin(),frame.end(),ram+main_sp),
            "Callback stack overwrote the interrupted EE return frame");
    require(callback_sp<=PS2_RAM_SIZE-0x10000u && callback_sp>=0x01f00000u,
            "Callback stack is outside its arena below the bootstrap stack");
    require(scheduler.invocationStackTop()==callback_sp,"Callback stack was not reused at the same nesting depth");
    GuestInvocation invocation{};
    invocation.context.pc=0x00167af8u;
    scheduler.currentThread()->invocations.push_back(invocation);
    const auto nested_sp=scheduler.invocationStackTop();
    require(nested_sp+0x4000u<=callback_sp,"Nested callback stacks overlap");
    scheduler.currentThread()->invocations.pop_back();
    std::vector<uint32_t> stacks{callback_sp,nested_sp};
    for (unsigned attempt=0;attempt<128;++attempt) {
        const auto top=runtime.reserveAsyncCallbackStack(0x4000u,16u);
        if (top==0u) break;
        require(top+0x4000u<=stacks.back() && (top&15u)==0u,
                "Callback arena allocations overlap or lost alignment");
        stacks.push_back(top);
    }
    require(stacks.size()>2 && runtime.reserveAsyncCallbackStack(0x4000u,16u)==0u,
            "Callback arena did not stop at its lower bound");
    require(std::equal(frame.begin(),frame.end(),ram+main_sp),
            "Allocating callback stacks changed the interrupted frame");
    require(scheduler.thread(1)->status==EeThreadStatus::Running
            && scheduler.thread(1)->context.pc==context.pc,
            "Allocating callback stacks changed the running EE thread");
}
}
