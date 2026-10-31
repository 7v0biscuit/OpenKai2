// SPDX-License-Identifier: GPL-3.0-only
#include "iop_checks.h"
#include "ps2x/iop/iop_subsystem.h"
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace openkai2 {
namespace {
using namespace ps2x::iop;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }

class CheckHost final : public IopHost {
public:
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x10000u);
    bool readGuest(uint32_t a, void* out, size_t n) const override {
        if (a>bytes.size() || n>bytes.size()-a) return false;
        if (n) std::memcpy(out,bytes.data()+a,n);
        return true;
    }
    bool writeGuest(uint32_t a, const void* in, size_t n) override {
        if (a>bytes.size() || n>bytes.size()-a) return false;
        if (n) std::memcpy(bytes.data()+a,in,n);
        return true;
    }
    bool zeroGuest(uint32_t a,size_t n) override {
        if (a>bytes.size() || n>bytes.size()-a) return false;
        std::fill_n(bytes.begin()+a,n,0); return true;
    }
    bool normalizeGuestAddress(uint32_t a,uint32_t& out) const override { out=a;return a<bytes.size(); }
    uint32_t allocateIopHandle(IopHandleKind) override { return 1; }
    uint32_t allocateGuest(uint32_t,uint32_t) override { return 0; }
    void freeGuest(uint32_t) override {}
    void audioCommand(uint32_t,uint32_t,GuestBuffer,GuestBuffer) override {}
    std::string hostPath(HostPathKind) const override { return {}; }
    std::string translateGuestPath(std::string_view path) const override { return std::string(path); }
    uint64_t openHostFile(std::string_view) override { return 0; }
    bool hostFileSize(uint64_t,uint64_t&) const override { return false; }
    bool readHostFile(uint64_t,uint64_t,void*,size_t,size_t&) override { return false; }
    void closeHostFile(uint64_t) override {}
    int32_t memoryCard(const MemoryCardRequest&) override { return 0; }
    bool hasGuestFunction(uint32_t) const override { return false; }
    bool invokeGuestFunction(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*) override { return false; }
    std::vector<std::string> logs;
    void log(LogLevel,std::string_view text) override { logs.emplace_back(text); }
};

void make_image(CheckHost& host, unsigned delayed) {
    auto half=[&](size_t at,uint16_t value) {
        host.bytes.at(at)=uint8_t(value); host.bytes.at(at+1)=uint8_t(value>>8);
    };
    auto word=[&](size_t at,uint32_t value) { half(at,uint16_t(value));half(at+2,uint16_t(value>>16)); };
    const size_t elf=0x100u, payload=elf+0x100u;
    word(elf,0x464C457Fu);word(elf+4,0x00010101u);
    half(elf+16,2);half(elf+18,8);word(elf+20,1);
    word(elf+24,0x10000u);word(elf+28,52);
    half(elf+40,52);half(elf+42,32);half(elf+44,1);
    word(elf+52,1);word(elf+56,0x100u);word(elf+60,0x10000u);word(elf+64,0x10000u);
    word(elf+68,0x500u);word(elf+72,0x500u);word(elf+76,7);word(elf+80,16);
    auto program=[&](size_t offset,std::initializer_list<uint32_t> code) {
        for (const auto instruction:code) { word(payload+offset,instruction);offset+=4; }
    };
    const auto jal=[](uint32_t address) { return 0x0C000000u | (address>>2); };
    program(0,{
        0x03E08021u,
        0x3C080001u,0x35080300u,
        0x3C040001u, delayed==1 ? 0x00002021u : 0x34840320u,
        jal(0x10214u),0x8D040000u,
        0x00402021u,
        0x24050000u | (delayed==2 ? 0u : 0x1234u),
        jal(0x1021Cu),0x8D050004u,
        0x00001021u,0x02000008u,0x00000000u,
    });
    program(0x80,{
        0x03E08821u,
        0x3C100001u,0x36100300u,
        0xAE040080u,
        jal(0x10224u), delayed==4 ? 0x8E02000Cu : 0u,
        0u,0xAE020084u,
        0x3C040001u,0x34840400u,
        0x3C058000u,0x34A50597u,
        0x3C060001u, delayed==3 ? 0x00003021u : 0x34C60100u,
        0x3C070001u,0x34E70480u,
        0xAFA00010u,0xAFA00014u,0xAFA00018u,
        jal(0x10254u),0x8E060008u,
        0x02200008u,0u,
    });
    program(0x100,{0x24820001u,0xACA20000u,0x00A01021u,0x03E00008u,0u});
    program(0x200,{0x41E00000u,0,0x101u,0x61626874u,0x00006573u,
                   0x03E00008u,0x24000004u,0x03E00008u,0x24000006u,
                   0x03E00008u,0x24000014u,0,0});
    program(0x240,{0x41E00000u,0,0x101u,0x63666973u,0x0000646Du,
                   0x03E00008u,0x24000011u,0,0});
    program(0x300,{0x10320u,0x1234u,0x10100u,0xBADu});
    program(0x320,{0,0,0x10080u,0x400u,40});
}

void make_startup_image(CheckHost& host,bool busy,bool delayed=false) {
    make_image(host,0);
    constexpr size_t payload=0x200u;
    auto word=[&](size_t at,uint32_t value) {
        for (unsigned i=0;i<4;++i) host.bytes.at(payload+at+i)=uint8_t(value>>(8*i));
    };
    auto program=[&](size_t at,std::initializer_list<uint32_t> code) {
        for (auto instruction:code) { word(at,instruction);at+=4; }
    };
    const auto jal=[](uint32_t address) { return 0x0C000000u | (address>>2); };
    std::copy_n(host.bytes.begin()+payload+0x80,0x5C,host.bytes.begin()+payload+0x180);
    program(0x1A8,{0x3C050000u,0x34A50011u});
    program(0x1D4,{jal(0x1022Cu),0u,0x3C080001u,0x24090001u,
                   0xAD090388u,0x1000FFFAu,0u});
    program(0x22C,{0x03E00008u,0x24000018u,0u});
    program(0x340,{0,0,0x10180u,0x400u,40});
    program(0x80,{
        0x3C040001u,0x34840340u,jal(0x10214u),0u,
        0x00402021u,0x24051234u,jal(0x1021Cu),0u,
        jal(0x1022Cu),0u,0x1000FFFDu,0u,
    });
    if (busy) program(0x80,{0x1000FFFFu,0u});
    if (delayed) {
        program(0x234,{0x03E00008u,0x24000021u,0u});
        program(0x80,{
            0x24042710u,jal(0x10234u),0u,
            0x3C040001u,0x34840340u,jal(0x10214u),0u,
            0x00402021u,0x24051234u,jal(0x1021Cu),0u,
            jal(0x1022Cu),0u,0x1000FFFDu,0u,
        });
    }
}
}

void verify_iop_import_pipeline() {
    for (unsigned delayed=0;delayed<=4;++delayed) {
        try {
        CheckHost host;
        make_image(host,delayed);
        IopSubsystem iop(host);
        const auto loaded=iop.loadModuleBuffer(0x100u);
        require(loaded.moduleId>0 && loaded.startResult==0, "IOP pipeline check could not start its module");
        iop.runEeCycles(8000u);
        uint32_t argument=0,thread=0;
        require(iop.readMemory(0x10380u,&argument,4) && argument==0x1234u,
                "IOP import did not consume its pending thread argument load");
        require(iop.readMemory(0x10384u,&thread,4) && thread==1u,
                "Pending IOP load overwrote an imported function's return value");
        require(iop.canBindRpc(0x80000597u), "IOP RPC handler argument load was not visible at registration");
        RpcRequest request{};
        request.sid=0x80000597u;request.function=41;request.receive={0x3000u,4u};
        const auto reply=iop.handleRpc(request);
        uint32_t answer=0;
        require(reply.handled && host.readGuest(0x3000u,&answer,4) && answer==42u,
                "IOP pipeline check could not execute its registered RPC handler");
        } catch (const std::exception& error) {
            throw std::runtime_error("IOP import pipeline case " + std::to_string(delayed) + ": " + error.what());
        }
    }
}

void verify_iop_module_startup() {
    CheckHost host;
    make_startup_image(host,false);
    IopSubsystem iop(host);
    const auto loaded=iop.loadModuleBuffer(0x100u);
    require(loaded.moduleId>0 && loaded.startResult==0,"Cannot load startup-check module");
    require(iop.canBindRpc(0x11u),"Module load returned before its nested RPC worker registered");
    uint32_t argument=0,thread=0;
    require(iop.readMemory(0x10380u,&argument,4) && argument==0x1234u,
            "Nested startup worker lost its argument");
    require(iop.readMemory(0x10384u,&thread,4) && thread==2u,
            "Nested startup worker ran without its thread identity");
    RpcRequest request{};
    request.sid=0x11u;request.function=41;request.receive={0x3000u,4u};
    uint32_t answer=0;
    require(iop.handleRpc(request).handled && host.readGuest(0x3000u,&answer,4) && answer==42u,
            "Startup bind did not reach a real interpreted RPC handler");
    require(!iop.canBindRpc(0x12u),"Startup scheduling fabricated an unregistered server");
    uint32_t resumed=0;
    require(iop.readMemory(0x10388u,&resumed,4) && resumed==0u,
            "Module startup resumed a sleeping worker without a wakeup");
    iop.runEeCycles(8000u);
    require(iop.readMemory(0x10388u,&resumed,4) && resumed==0u,
            "Scheduler resumed a sleeping startup worker without a wakeup");
    iop.reset();
    require(!iop.canBindRpc(0x11u),"IOP reset retained the startup server");
    host.logs.clear();
    make_startup_image(host,true);
    const auto busy=iop.loadModuleBuffer(0x100u);
    require(busy.moduleId>0 && !iop.canBindRpc(0x11u),
            "Busy startup worker was accepted as a registered RPC server");
    require(std::any_of(host.logs.begin(),host.logs.end(),[](const auto& log) {
        return log.find("module startup reached scheduling budget")!=std::string::npos;
    }),"Nonblocking module startup did not report its exhausted scheduling budget");
    iop.reset();
    make_startup_image(host,false,true);
    require(iop.loadModuleBuffer(0x100u).moduleId>0 && !iop.canBindRpc(0x11u),
            "Module startup fast-forwarded a delayed worker's wake deadline");
    iop.runEeCycles(4'000'000u);
    require(iop.canBindRpc(0x11u),"Delayed startup worker did not register after its real wake deadline");
    CheckHost bulkHost,fragmentedHost;
    make_startup_image(bulkHost,false,true);make_startup_image(fragmentedHost,false,true);
    IopSubsystem bulk(bulkHost),fragmented(fragmentedHost);
    require(bulk.loadModuleBuffer(0x100u).moduleId>0 && fragmented.loadModuleBuffer(0x100u).moduleId>0,
        "Cannot load idle-clock fixtures");
    const auto before=fragmented.debugSnapshot();
    for(unsigned i=0;i<10000u;++i)fragmented.runEeCycles(7u);
    const auto idle=fragmented.debugSnapshot();
    require(idle.emulatorCycles==before.emulatorCycles+8750u
        && idle.emulatorInstructions==before.emulatorInstructions && !fragmented.canBindRpc(0x11u),
        "Idle clock skipped a wake deadline or executed a sleeping thread");
    constexpr uint64_t total=3200003u;
    uint64_t charged=70000u;
    while(charged+7u<=total){fragmented.runEeCycles(7u);charged+=7u;}
    fragmented.runEeCycles(total-charged);bulk.runEeCycles(total);
    auto a=bulk.debugSnapshot(),b=fragmented.debugSnapshot();
    require(bulk.canBindRpc(0x11u) && fragmented.canBindRpc(0x11u)
        && a.emulatorCycles==b.emulatorCycles && a.emulatorInstructions==b.emulatorInstructions,
        "Fragmented IOP clock changed delayed worker scheduling");
    bulk.runEeCycles(5u);fragmented.runEeCycles(5u);
    require(bulk.debugSnapshot().emulatorCycles==a.emulatorCycles+1u
        && fragmented.debugSnapshot().emulatorCycles==b.emulatorCycles+1u,
        "Idle fast path lost fractional EE-to-IOP cycles");
    request.function=41;answer=0;
    require(fragmented.handleRpc(request).handled && fragmentedHost.readGuest(0x3000u,&answer,4u) && answer==42u,
        "An idle IOP did not service an external RPC request");

}
}
