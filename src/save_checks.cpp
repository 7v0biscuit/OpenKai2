// SPDX-License-Identifier: GPL-3.0-only
#include "save_checks.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace openkai2 {
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(std::string("Runtime save regression: ")+message);
}
void end_session(PS2Runtime& runtime) {
    R5900Context context{};
    // End has no translated entry in the stripped executable's registry.
    // Use the real SDK implementation to close descriptors and reset state.
    ps2_stubs::sceMcEnd(runtime.memory().getRDRAM(),&context,&runtime);
}
int32_t call(PS2Runtime& runtime,uint32_t entry,std::array<uint32_t,6> args={}) {
    constexpr uint32_t stack=0x01fffe00u,gp=0x003ac4f0u,returned=0x0019de98u;
    R5900Context context{};context.pc=entry;
    for (unsigned i=0;i<args.size();++i) context.r[4+i]=_mm_cvtsi32_si128(args[i]);
    for (unsigned r=16;r<=23u;++r) context.r[r]=_mm_cvtsi32_si128(0x1000u+r);
    context.r[28]=_mm_cvtsi32_si128(gp);context.r[29]=_mm_cvtsi32_si128(stack);
    context.r[31]=_mm_cvtsi32_si128(returned);
    for (unsigned step=0;step<4096u && context.pc!=returned;++step) {
        if (!runtime.hasFunction(context.pc)) {
            std::ostringstream error;error << "Runtime save regression: unregistered function 0x" << std::hex << context.pc;
            throw std::runtime_error(error.str());
        }
        runtime.lookupFunction(context.pc)(runtime.memory().getRDRAM(),&context,&runtime);
    }
    require(context.pc==returned && getRegU32(&context,28)==gp && getRegU32(&context,29)==stack,
            "native save chain lost its caller frame");
    for (unsigned r=16;r<=23u;++r)
        require(getRegU32(&context,r)==0x1000u+r,"native save chain changed a saved register");
    return int32_t(getRegU32(&context,2));
}
void verify_scheduled_card_check() {
    auto owned=std::make_unique<PS2Runtime>();auto& runtime=*owned;auto& memory=runtime.memory();
    require(memory.initialize(),"cannot allocate scheduled card-check memory");
    call(runtime,0x00118270u);
    constexpr uint32_t bridge=0x001908e8u,interrupt=0x00190918u,marker=0x25000u,stack=0x01fffe00u;
    struct Binding {
        PS2Runtime& runtime;uint32_t entry;PS2Runtime::RecompiledFunction original;
        ~Binding() { runtime.replaceFunction(entry,original); }
    } continued{runtime,bridge,runtime.lookupFunction(bridge)},irq{runtime,interrupt,runtime.lookupFunction(interrupt)},
        sync{runtime,0x00118c80u,runtime.lookupFunction(0x00118c80u)};
    runtime.replaceFunction(interrupt,[](uint8_t*,R5900Context* context,PS2Runtime* active) {
        auto& memory=active->memory();memory.write32(marker+4u,memory.read32(marker+4u)+1u);
        context->pc=getRegU32(context,31);
    });
    runtime.replaceFunction(0x00118c80u,[](uint8_t* ram,R5900Context* context,PS2Runtime* active) {
        // The real SDK supplies the result. Queue an interrupt at the same
        // boundary as an asynchronous completion, before the game's JALR.
        context->pc=getRegU32(context,31);ps2_stubs::sceMcSync(ram,context,active);
        if(getRegU32(context,2)==1u)active->eeScheduler().dispatchIrq(true,9u);
    });
    runtime.replaceFunction(bridge,[](uint8_t*,R5900Context* context,PS2Runtime* active) {
        auto& memory=active->memory();const auto polls=memory.read32(marker)+1u;memory.write32(marker,polls);
        const auto state=memory.read32(0x003a4a48u);
        if(state!=1u || polls>64u) { active->eeScheduler().requestStop();return; }
        context->r[31]=_mm_cvtsi32_si128(bridge);context->pc=0x0013cfa0u;
    });
    R5900Context context{};context.pc=0x0013d130u;
    context.r[6]=_mm_cvtsi32_si128(1);context.r[28]=_mm_cvtsi32_si128(0x003ac4f0u);
    context.r[29]=_mm_cvtsi32_si128(stack);context.r[31]=_mm_cvtsi32_si128(bridge);
    auto& ee=runtime.eeScheduler();ee.reset(memory.getRDRAM(),context);
    ee.addIrqHandler(true,9u,interrupt,true,0u,0x003ac4f0u,0u);
    std::mutex mutex;std::condition_variable changed;bool done=false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if(!changed.wait_for(lock,std::chrono::seconds(3),[&] { return done; }))ee.requestStop();
    });
    std::exception_ptr failure;
    try { ee.run(); } catch(...) { failure=std::current_exception(); }
    { std::lock_guard lock(mutex);done=true; }changed.notify_one();watchdog.join();
    if(failure)std::rethrow_exception(failure);
    require(memory.read32(0x003a4a48u)==2u && memory.read32(0x003a4a4cu)==0u
            && memory.read32(marker+4u)==21u,"card stability check did not finish across scheduled completion interrupts");
    const auto* main=ee.thread(1);
    require(main && getRegU32(&main->context,29)==stack && getRegU32(&main->context,28)==0x003ac4f0u,
            "scheduled card callback lost its caller frame");
    end_session(runtime);
}
}
void verify_save_creation() {
    auto owned=std::make_unique<PS2Runtime>();auto& runtime=*owned;
    require(runtime.memory().initialize(),"cannot allocate guest memory");
    auto* memory=runtime.memory().getRDRAM();
    const auto previous=PS2Runtime::getIoPaths();
    const auto root=std::filesystem::temp_directory_path()/
        ("openkai2-save-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(root),"cannot allocate an isolated save directory");
    struct Scope {
        PS2Runtime& runtime;PS2Runtime::IoPaths previous;std::filesystem::path root;
        ~Scope() {
            try { end_session(runtime); } catch (...) { }
            PS2Runtime::setIoPaths(previous);
            std::error_code error;std::filesystem::remove_all(root,error);
        }
    } scope{runtime,previous,root};
    auto paths=previous;paths.mcRoot=root/"mc0";PS2Runtime::setIoPaths(paths);
    verify_scheduled_card_check();
    call(runtime,0x00118270u);
    constexpr uint32_t path=0x26000u,source=0x28000u,output=0x2a000u,result=0x25000u;
    auto text=[&](const char* value) { std::strcpy(reinterpret_cast<char*>(memory+path),value); };
    auto finish=[&](uint32_t expectedStatus=2u,bool framePoll=false) {
        for (unsigned i=0;i<64u;++i) {
            int32_t busy=0;
            if(framePoll) { call(runtime,0x0013cfa0u);busy=runtime.memory().read32(0x003a4a48u)==1u; }
            else busy=call(runtime,0x0013d020u,{1u});
            if (busy==0) {
                require(runtime.memory().read32(0x003a4a48u)==expectedStatus,"game save command ended in an error state");
                return;
            }
        }
        require(false,"game save command never completed");
    };
    // Begin where the retail log stops: card detection must complete all 21
    // stability samples, and the initial directory search must report absence.
    require(call(runtime,0x0013d130u,{0,0,1u})==0,"initial card check did not queue");
    finish();
    require(runtime.memory().read32(0x003b4a80u)==2u
            && runtime.memory().read32(0x003b4a88u)==1u
            && runtime.memory().read32(0x003a4a4cu)==0u,"formatted PS2 card detection failed");
    text("/BASLUS-20779DBZ2/*");
    require(call(runtime,0x0013d270u,{0,0,path,output,5u})==0,"initial save search did not queue");
    finish(3u);
    require(call(runtime,0x0013d130u,{0,0,1u})==0,"confirmation card check did not queue");
    finish(2u,true);
    // Run the original menu transition, rather than directly asking the SDK
    // to create a directory. These are the executable's literal path strings.
    constexpr uint32_t object=0x2c000u;
    runtime.memory().write32(0x003a51acu,object);
    runtime.memory().write32(0x003a51b0u,object+0x100u);
    std::strcpy(reinterpret_cast<char*>(memory+0x003a32b0u),"/BASLUS-20779DBZ2/*");
    std::strcpy(reinterpret_cast<char*>(memory+0x003a3280u),"/BASLUS-20779DBZ2");
    call(runtime,0x0026d360u,{object});
    require(runtime.memory().read32(object+28u)==0x0026c870u
            && runtime.memory().read32(0x003a4a40u)==13u,"completed card detection did not advance the original menu to directory search");
    finish(3u,true);
    require(int32_t(runtime.memory().read32(0x003a4a4cu))==-4,"missing save directory did not report absence");
    call(runtime,0x0026c870u,{object});
    require(runtime.memory().read32(0x003a4a40u)==11u
            && runtime.memory().read32(object+28u)==0x0026dca0u,"missing directory did not advance the original menu to save creation");
    finish(2u,true);
    require(std::filesystem::is_directory(root/"mc0/BASLUS-20779DBZ2"),"save directory was not created");
    std::vector<uint8_t> expected(4096u);
    for (unsigned i=0;i<expected.size();++i) expected[i]=uint8_t(i*17u+(i>>4u));
    text("/BASLUS-20779DBZ2/OPENKAI2-TEST");
    std::memcpy(memory+source,expected.data(),expected.size());
    // The original game routine queues open/create, then its completion
    // callbacks write and close. No host-created file supplies the result.
    require(call(runtime,0x0013d6c0u,{0,0,path,source,uint32_t(expected.size())})==0,
            "game save creation did not queue");
    finish();
    const auto file=root/"mc0/BASLUS-20779DBZ2/OPENKAI2-TEST";
    std::ifstream input(file,std::ios::binary);
    const std::vector<uint8_t> actual((std::istreambuf_iterator<char>(input)),{});
    require(actual==expected,"native save callbacks did not persist the complete payload");
    require(std::memcmp(memory+source,expected.data(),expected.size())==0,"saving changed its source buffer");
    // Reinitialize the SDK, then read from disk to verify persistence across
    // a new memory-card session and return the real file contents to EE RAM.
    end_session(runtime);call(runtime,0x00118270u);
    require(call(runtime,0x00118658u,{0,0,path,1u})==0,"saved file did not reopen");
    call(runtime,0x00118c80u,{0,result,result+4u});
    const auto fd=runtime.memory().read32(result+4u);
    require(fd>0u && int32_t(fd)>0,"reopening the saved file failed");
    std::memset(memory+output-16u,0xa5,expected.size()+32u);
    call(runtime,0x001189f0u,{fd,output,uint32_t(expected.size())});
    call(runtime,0x00118c80u,{0,result,result+4u});
    require(runtime.memory().read32(result+4u)==expected.size()
            && std::memcmp(memory+output,expected.data(),expected.size())==0,"saved data did not survive SDK reinitialization");
    for (unsigned i=0;i<16u;++i)
        require(memory[output-16u+i]==0xa5 && memory[output+expected.size()+i]==0xa5,
                "save readback crossed its destination bounds");
    call(runtime,0x001187b8u,{fd});call(runtime,0x00118c80u,{0,result,result+4u});
}
}
