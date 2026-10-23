// SPDX-License-Identifier: GPL-3.0-only
#include "save_checks.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
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
    call(runtime,0x00118270u);
    constexpr uint32_t path=0x26000u,source=0x28000u,output=0x2a000u,result=0x25000u;
    auto text=[&](const char* value) { std::strcpy(reinterpret_cast<char*>(memory+path),value); };
    auto finish=[&](uint32_t expectedStatus=2u) {
        for (unsigned i=0;i<64u;++i) {
            const auto busy=call(runtime,0x0013d020u,{1u});
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
    require(int32_t(runtime.memory().read32(0x003a4a4cu))==-4,"missing save directory did not report absence");
    require(call(runtime,0x0013d130u,{0,0,1u})==0,"confirmation card check did not queue");
    finish();
    text("/BASLUS-20779DBZ2");
    require(call(runtime,0x0013d390u,{0,0,path})==0,"game directory creation did not queue");
    finish();
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
