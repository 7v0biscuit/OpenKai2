// SPDX-License-Identifier: GPL-3.0-only
#include "runtime_checks.h"
#include "sound_checks.h"
#include "dma_checks.h"
#include "startup_checks.h"
#include "callback_checks.h"
#include "thread_checks.h"
#include "stream_checks.h"
#include "sound_transfer_checks.h"
#include "asset_checks.h"
#include "fade_checks.h"
#include "iop_checks.h"
#include "iop_wait_checks.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "openkai2_translation_entries.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace openkai2 {
namespace {
void verify_compiled_worker_setup() {
    auto owned=std::make_unique<PS2Runtime>();
    auto& runtime=*owned;
    if (!runtime.memory().initialize()) throw std::runtime_error("Cannot allocate compiled worker regression memory");
    constexpr uint32_t bridge=0x001908e8u, finish=0x00190920u, marker=0x25000u;
    struct Binding {
        PS2Runtime& runtime;
        uint32_t address;
        PS2Runtime::RecompiledFunction original;
        ~Binding() { runtime.replaceFunction(address,original); }
    } resumed{runtime,bridge,runtime.lookupFunction(bridge)}, finished{runtime,finish,runtime.lookupFunction(finish)};
    runtime.replaceFunction(bridge,[](uint8_t*,R5900Context* context,PS2Runtime*) {
        context->r[31]=_mm_cvtsi32_si128(finish);
        context->pc=0x00158fd8u;
    });
    runtime.replaceFunction(finish,[](uint8_t*,R5900Context*,PS2Runtime* active) {
        active->memory().write32(marker,37u);
        active->requestStop();
    });
    R5900Context context{};
    context.pc=0x0010a8b8u; // Original translated SDK InitThread entry.
    context.r[29]=_mm_cvtsi32_si128(PS2_RAM_SIZE-16u);
    context.r[28]=_mm_cvtsi32_si128(0x003ac4f0u);
    context.r[31]=_mm_cvtsi32_si128(bridge);
    auto& scheduler=runtime.eeScheduler();
    scheduler.reset(runtime.memory().getRDRAM(),context);
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
    if (runtime.memory().read32(marker)!=37u || runtime.memory().read32(0x353394u)!=1u
        || runtime.memory().read32(0x35acb8u)!=1u)
        throw std::runtime_error("Runtime regression: compiled SDK initialization did not supply a usable inherited priority");
    const std::pair<uint32_t,uint32_t> workers[]={
        {0x353418u,0x158540u},{0x353420u,0x158630u},
        {0x353424u,0x158710u},{0x35342cu,0x1587f0u}};
    for (const auto& [slot,entry]:workers) {
        const auto* worker=scheduler.thread(static_cast<int>(runtime.memory().read32(slot)));
        if (!worker || worker->id<=1 || worker->entry!=entry || worker->initialPriority!=1)
            throw std::runtime_error("Runtime regression: compiled game worker creation failed");
    }
    for (const int id:{3,4}) {
        const auto* worker=scheduler.thread(id);
        if (!worker || worker->status!=EeThreadStatus::Waiting || worker->wait.reason!=EeWaitReason::Sleep)
            throw std::runtime_error("Runtime regression: compiled game workers did not reach their sleep points");
    }
    const auto* main=scheduler.thread(1);
    if (!main || getRegU32(&main->context,29)!=PS2_RAM_SIZE-16u
        || getRegU32(&main->context,28)!=0x003ac4f0u)
        throw std::runtime_error("Runtime regression: worker initialization changed the main thread frame");
}
void verify_file_callbacks(PS2Runtime& runtime,uint8_t* memory) {
    constexpr uint32_t object=0x23000u, stack=0x01fffe40u;
    struct Query { uint32_t argument, command, sectors; int64_t expected; };
    const Query queries[]={
        {0u,100u,0u,0}, {0u,299u,0u,0}, {object,100u,7u,0},
        {object,200u,0x00200003u,1}, {object,201u,0x00200003u,6144},
        {object,200u,0xFFFFFFFFu,-1}, {object,201u,0xFFFFFFFFu,-2048},
        {object,202u,7u,0}, {object,204u,7u,0},
        {object,299u,7u,1}, {object,300u,7u,0},
    };
    for (const auto& query:queries) {
        std::memset(memory+object,0xA5,64);
        std::memcpy(memory+object+0x14,&query.sectors,4);
        uint8_t original[64];std::memcpy(original,memory+object,64);
        std::memset(memory+stack-64,0xCC,128);
        R5900Context context{};
        context.pc=0x00167af8u;
        context.r[4]=_mm_cvtsi32_si128(query.argument);
        context.r[5]=_mm_cvtsi32_si128(query.command);
        for (unsigned reg=16;reg<=23;++reg) context.r[reg]=_mm_cvtsi32_si128(0x1000+reg);
        context.r[28]=_mm_cvtsi32_si128(0x003ac4f0);
        context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(0x00164948);
        if (!runtime.dispatchGuestBranch(memory,&context,0x00167af8u,0x00164940u,0x00164948u,
                PS2Runtime::GuestBranchKind::IndirectCall,"file query regression")
            || context.pc!=0x00164948u || _mm_cvtsi128_si64(context.r[2])!=query.expected
            || getRegU32(&context,29)!=stack || getRegU32(&context,31)!=0x00164948u
            || getRegU32(&context,28)!=0x003ac4f0u || std::memcmp(original,memory+object,64)!=0)
            throw std::runtime_error("Runtime regression: file query result, object, or caller frame changed");
        for (unsigned reg=16;reg<=23;++reg)
            if (getRegU32(&context,reg)!=0x1000u+reg)
                throw std::runtime_error("Runtime regression: file query changed a saved register");
        for (unsigned offset=0;offset<128;++offset)
            if ((offset<48u || offset>=64u) && memory[stack-64+offset]!=0xCC)
                throw std::runtime_error("Runtime regression: file query overwrote adjacent stack memory");
    }
    constexpr uint32_t bank=0x22000u;
    std::memcpy(memory+bank,translation::file_callback_slots.data(),translation::file_callback_slots.size()*4);
    // Enter the caller after its device lookup, with the saved return frame it
    // would have at this resume label. A real constant-return callback makes
    // executing slot 0x60 observable as well as checking the null query path.
    const std::pair<uint32_t,uint32_t> variants[]={
        {0x00167af8u,0u}, {0x00167958u,2048u}, {0u,0u}};
    for (const auto& variant : variants) {
        std::memcpy(memory+bank+0x60,&variant.first,4);
        const uint64_t returned=0x00164960u;
        std::memcpy(memory+stack-16,&returned,8);
        R5900Context context{};
        context.pc=0x0016492cu;
        context.r[3]=_mm_cvtsi32_si128(bank);
        context.r[4]=_mm_cvtsi32_si128(object);
        context.r[29]=_mm_cvtsi32_si128(stack-16);
        context.r[31]=_mm_cvtsi32_si128(returned);
        if (!runtime.dispatchGuestBranch(memory,&context,0x0016492cu,0x00164918u,uint32_t(returned),
                PS2Runtime::GuestBranchKind::IndirectCall,"file callback caller regression")
            || context.pc!=returned || getRegU32(&context,29)!=stack
            || getRegU32(&context,2)!=variant.second)
            throw std::runtime_error("Runtime regression: compiled file callback caller did not return correctly");
    }
}
}
int run_runtime_checks() {
    verify_thread_bootstrap();
    verify_compiled_worker_setup();
    std::cout << "Runtime thread bootstrap regression passed: SDK priority and four compiled game workers.\n";
    verify_callback_stacks();
    verify_startup_diagnostics();
    std::cout << "Startup diagnostics regression passed: bounded, read-only EE wait snapshots.\n";
    verify_iop_wait_diagnostics();
    std::cout << "IOP wait diagnostics regression passed: read-only semaphore, event, and instruction snapshots.\n";
    verify_iop_import_pipeline();
    std::cout << "IOP import pipeline regression passed: thread arguments, RPC handler, and return values.\n";
    verify_iop_module_startup();
    std::cout << "IOP module startup regression passed: nested RPC worker registration, sleep, and bounded scheduling.\n";
    verify_sound_registers();
    std::cout << "SPU2 initialization regression passed: both cores, DMA status, and guest polling.\n";
    verify_scratchpad_dma();
    constexpr uint32_t entry=0x00167188, caller=0x00162dec, returned=0x00162df4;
    PS2Runtime runtime;
    if (!runtime.hasFunction(entry))
        throw std::runtime_error("Runtime regression: startup callback 0x00167188 is not registered");
    if (!runtime.memory().initialize()) throw std::runtime_error("Cannot allocate regression-test guest memory");
    auto* memory=runtime.memory().getRDRAM();
    verify_stream_callbacks(runtime,memory);
    std::cout << "Runtime stream callback regression passed: nine methods, ring wraparound, and compiled cleanup caller.\n";
    verify_sound_transfer(runtime,memory);
    std::cout << "Runtime sound transfer regression passed: compiled SDK, IOP payload, completion, aliases, and logged caller.\n";
    verify_asset_relocation(runtime,memory);
    std::cout << "Runtime asset relocation regression passed: seven handlers, logged AMM dispatcher, nested assets, and repeat guards.\n";
    verify_fade_callbacks(runtime,memory);
    std::cout << "Runtime fade callback regression passed: four renderers, logged caller, draw packets, completion, and saved registers.\n";
    // Run the original compiled scratchpad-copy caller with the retail size
    // and destination, including its SADR write and receive SDK invocation.
    {
        constexpr uint32_t destination=0x003c4200u, stack=0x01fffe40u;
        auto* scratch=runtime.memory().getScratchpad();
        for (uint32_t i=0;i<PS2_SCRATCHPAD_SIZE;++i) scratch[i]=uint8_t(i*13u+(i>>8));
        std::memset(memory+destination-16,0xcc,PS2_SCRATCHPAD_SIZE+32);
        R5900Context context{};
        context.pc=0x001862e8u;
        context.r[4]=_mm_cvtsi32_si128(0);
        context.r[5]=_mm_cvtsi32_si128(0x4000);
        context.r[6]=_mm_cvtsi32_si128(destination);
        context.r[7]=_mm_cvtsi32_si128(12);
        for (unsigned reg=16;reg<=18;++reg) context.r[reg]=_mm_cvtsi32_si128(0x1000+reg);
        context.r[29]=_mm_cvtsi32_si128(stack);
        context.r[31]=_mm_cvtsi32_si128(0x00186350u);
        if (!runtime.dispatchGuestBranch(memory,&context,0x001862e8u,0u,0x00186350u,
                PS2Runtime::GuestBranchKind::DirectCall,"scratchpad copy regression")
            || context.pc!=0x00186350u || getRegU32(&context,2)!=0
            || getRegU32(&context,29)!=stack || getRegU32(&context,31)!=0x00186350u
            || std::memcmp(memory+destination,scratch,PS2_SCRATCHPAD_SIZE)!=0)
            throw std::runtime_error("Runtime regression: compiled scratchpad copy failed");
        for (unsigned reg=16;reg<=18;++reg)
            if (getRegU32(&context,reg)!=0x1000u+reg)
                throw std::runtime_error("Runtime regression: scratchpad caller changed a saved register");
        for (unsigned i=0;i<16;++i)
            if (memory[destination-16+i]!=0xcc || memory[destination+PS2_SCRATCHPAD_SIZE+i]!=0xcc)
                throw std::runtime_error("Runtime regression: compiled scratchpad copy overran destination");
    }
    std::cout << "Runtime scratchpad DMA regression passed: receive bytes, completion, aliases, and compiled caller.\n";
    {
        constexpr uint32_t saved=0x003c4200u, working=0x003c8200u, flag=0x00371d40u;
        constexpr uint32_t stack=0x01fffe40u, returned=0x00186490u;
        auto* scratch=runtime.memory().getScratchpad();
        for (uint32_t i=0;i<PS2_SCRATCHPAD_SIZE;++i) {
            scratch[i]=uint8_t(i*7u+(i>>8));
            memory[working+i]=uint8_t(i*29u+(i>>7));
        }
        const std::vector<uint8_t> original(scratch,scratch+PS2_SCRATCHPAD_SIZE);
        auto call=[&](uint32_t address) {
            R5900Context context{};
            context.pc=address;
            context.r[16]=_mm_cvtsi32_si128(0x1010);
            context.r[17]=_mm_cvtsi32_si128(0x1011);
            context.r[29]=_mm_cvtsi32_si128(stack);
            context.r[31]=_mm_cvtsi32_si128(returned);
            // Tail jumps intentionally unwind to the EE dispatcher. Resume
            // exact compiled labels until the outer caller has returned.
            for (unsigned step=0;step<128 && context.pc!=returned;++step) {
                auto function=runtime.lookupFunction(context.pc);
                function(memory,&context,&runtime);
            }
            if (context.pc!=returned || getRegU32(&context,29)!=stack
                || getRegU32(&context,31)!=returned || getRegU32(&context,16)!=0x1010u
                || getRegU32(&context,17)!=0x1011u)
                throw std::runtime_error("Runtime regression: scratchpad switch caller frame failed");
        };
        std::memset(memory+flag,0,4);
        call(0x001861d8u);
        uint32_t enabled=0;std::memcpy(&enabled,memory+flag,4);
        if (enabled!=1u || std::memcmp(memory+saved,original.data(),original.size())!=0
            || std::memcmp(scratch,memory+working,PS2_SCRATCHPAD_SIZE)!=0)
            throw std::runtime_error("Runtime regression: scratchpad switch did not save and load its banks");
        for (uint32_t i=0;i<PS2_SCRATCHPAD_SIZE;++i) scratch[i]=uint8_t(i*17u+(i>>6));
        const std::vector<uint8_t> modified(scratch,scratch+PS2_SCRATCHPAD_SIZE);
        call(0x00186258u);
        std::memcpy(&enabled,memory+flag,4);
        if (enabled!=0u || std::memcmp(memory+working,modified.data(),modified.size())!=0
            || std::memcmp(scratch,original.data(),original.size())!=0)
            throw std::runtime_error("Runtime regression: scratchpad switch did not restore the original bank");
    }
    std::cout << "Runtime scratchpad switch regression passed: compiled save, sync, send, and restore.\n";
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
    verify_file_callbacks(runtime,memory);
    std::cout << "Runtime file callback regression passed: 14 entries, query paths, and compiled caller.\n";
    verify_callback_resume(runtime,0x00167af8u);
    std::cout << "Runtime callback stack regression passed: disjoint stacks and compiled interrupt return.\n";
    return 0;
}
} // namespace openkai2
