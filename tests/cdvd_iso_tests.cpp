// SPDX-License-Identifier: GPL-3.0-only
#include "iso_fixture.h"
#include "disc_image.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "Syscalls/RPC.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_cdvd.h"
#include "ps2x/iop/iop_host.h"
#include "ps2x/iop/iop_subsystem.h"
#include <chrono>
#include <cstring>
#include <iostream>

using namespace ps2x::iop;
using namespace ps2x::iop::detail;
using namespace openkai2::test;

namespace {
void require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

class DiscHost final : public IopHost {
    fs::path image_, root_;
public:
    bool fail_reads=false;
    std::vector<uint8_t> guest=std::vector<uint8_t>(1024*1024);
    std::vector<std::string> logs;
    DiscHost(fs::path image, fs::path root) : image_(std::move(image)), root_(std::move(root)) {}
    bool readGuest(uint32_t a,void* data,size_t size) const override {
        a&=0x1FFFFFFFu;
        if (a>guest.size() || size>guest.size()-a) return false;
        std::memcpy(data,guest.data()+a,size);return true;
    }
    bool writeGuest(uint32_t a,const void* data,size_t size) override {
        a&=0x1FFFFFFFu;
        if (a>guest.size() || size>guest.size()-a) return false;
        std::memcpy(guest.data()+a,data,size);return true;
    }
    bool zeroGuest(uint32_t a,size_t size) override {
        a&=0x1FFFFFFFu;
        if (a>guest.size() || size>guest.size()-a) return false;
        std::fill_n(guest.begin()+a,size,0);return true;
    }
    bool normalizeGuestAddress(uint32_t a,uint32_t& out) const override {
        out=a&0x1FFFFFFFu;return out<guest.size();
    }
    uint32_t allocateIopHandle(IopHandleKind) override { return 1; }
    uint32_t allocateGuest(uint32_t,uint32_t) override { return 0; }
    void freeGuest(uint32_t) override {}
    void audioCommand(uint32_t,uint32_t,GuestBuffer,GuestBuffer) override {}
    std::string hostPath(HostPathKind kind) const override {
        if (kind==HostPathKind::CdImage) return image_.string();
        if (kind==HostPathKind::CdRoot) return root_.string();
        return {};
    }
    std::string translateGuestPath(std::string_view path) const override { return std::string(path); }
    uint64_t openHostFile(std::string_view path) override { return path==image_.string() ? 1 : 0; }
    bool hostFileSize(uint64_t,uint64_t& size) const override { size=fs::file_size(image_); return true; }
    bool readHostFile(uint64_t,uint64_t offset,void* data,size_t size,size_t& read_bytes) override {
        read_bytes=0;
        if (fail_reads) return false;
        std::ifstream stream(image_,std::ios::binary);
        stream.seekg(std::streamoff(offset));
        stream.read(static_cast<char*>(data),std::streamsize(size));
        read_bytes=size_t(stream.gcount());
        return read_bytes==size;
    }
    void closeHostFile(uint64_t) override {}
    int32_t memoryCard(const MemoryCardRequest&) override { return 0; }
    bool hasGuestFunction(uint32_t) const override { return false; }
    bool invokeGuestFunction(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*) override { return false; }
    void log(LogLevel,std::string_view text) override { logs.emplace_back(text); }
};
}

int main() {
    const auto root=fs::temp_directory_path()/("openkai2-cdvd-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto image=root/"original.iso";
        auto data=fixture();
        auto fileRecord=record("DATA.BIN;1",29,6,1);
        const uint8_t date[]={124,5,6,7,8,9,0};
        std::copy(std::begin(date),std::end(date),fileRecord.begin()+18);
        place(data,20*sector+2*34,fileRecord);
        const std::string contents="abcdef";
        std::copy(contents.begin(),contents.end(),data.begin()+29*sector);
        write(image,data);
        openkai2::DiscOptions options;
        options.expected_executable_sha256="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        const auto disc=openkai2::prepare_disc(image,root/"cache",options);
        {
            DiscHost host(image,disc.root);
            {
                IopSubsystem iop(host);
                for (unsigned attempt=0; attempt<65537u; ++attempt)
                    require(!iop.canBindRpc(0xCAFE0597u), "Unregistered RPC server was reported ready");
                require(host.logs.size()==4u, "Pending RPC diagnostics did not stop after four samples");
                for (size_t sample=0; sample<host.logs.size(); ++sample) {
                    require(host.logs[sample].find("waiting sid=0xcafe0597")!=std::string::npos,
                            "RPC diagnostic lost the requested SID");
                    require(host.logs[sample].find("stat0=0x0")!=std::string::npos,
                            "RPC diagnostic lost SPU2 status");
                }
                iop.reset();
                require(!iop.canBindRpc(0xCAFE0597u) && host.logs.size()==5u,
                        "IOP reset did not restart bounded RPC diagnostics");
                require(iop.canBindRpc(0x80000597u), "ISO file-search RPC service is unavailable");
                auto put=[&](size_t at,uint32_t value) {
                    for (unsigned byte=0;byte<4;++byte) host.guest.at(at+byte)=uint8_t(value>>(8*byte));
                };
                auto get=[&](size_t at) {
                    uint32_t value=0;for (unsigned byte=0;byte<4;++byte) value|=uint32_t(host.guest.at(at+byte))<<(8*byte);
                    return value;
                };
                RpcRequest request{};
                request.sid=0x80000597u;request.send.address=0x4000u;request.receive={0x7000u,4};
                const char search[]="cdrom0:\\usr\\data.bin;1";
                for (const uint32_t size:{292u,296u,300u}) {
                    const size_t metadata=size==292u ? 32u : 36u;
                    std::fill(host.guest.begin(),host.guest.end(),0xCC);
                    std::fill_n(host.guest.begin()+0x4000,size,0);
                    std::memcpy(host.guest.data()+0x4000+metadata,search,sizeof(search));
                    put(0x4000+metadata+256,0x80006000u);
                    request.send.size=size;
                    const auto response=iop.handleRpc(request);
                    require(response.handled && response.serverDispatchPolicy==ServerDispatchPolicy::Suppress
                            && get(0x7000)==1,"File-search RPC did not report success");
                    require(get(0x6000)==29 && get(0x6004)==6,"File-search RPC lost original ISO sector or size");
                    require(std::memcmp(host.guest.data()+0x6008,"DATA.BIN;1",10)==0,
                            "File-search RPC lost the ISO filename");
                    const uint8_t expectedDate[]={1,9,8,7,6,5,0xE8,7};
                    require(std::memcmp(host.guest.data()+0x6018,expectedDate,8)==0,
                            "File-search RPC lost the ISO recording date");
                    require(host.guest[0x5FFF]==0xCC && host.guest[0x6000+metadata]==0xCC,
                            "File-search RPC overwrote adjacent memory");
                    if (metadata==36u) require(get(0x6020)==1,"Extended search lost file attributes");
                }
                const auto descriptor=std::vector<uint8_t>(host.guest.begin()+0x6000,host.guest.begin()+0x6024);
                auto failed=[&](const char* message) {
                    put(0x7000,123);
                    require(iop.handleRpc(request).handled && get(0x7000)==0,message);
                    require(std::equal(descriptor.begin(),descriptor.end(),host.guest.begin()+0x6000),
                            "Failed search changed the previous file descriptor");
                };
                std::memcpy(host.guest.data()+0x4024,"\\USR\\MISSING.BIN;1",19);
                failed("Missing ISO file was reported as found");
                std::fill_n(host.guest.begin()+0x4024,256,'A');
                failed("Unterminated search path was accepted");
                std::fill_n(host.guest.begin()+0x4024,256,0);
                std::memcpy(host.guest.data()+0x4024,search,sizeof(search));
                put(0x4128,1);
                failed("Unsupported disc layer was reported as found");
                put(0x4128,0);
                put(0x4124,0xFFFFFFFFu);
                failed("Invalid search output pointer was accepted");
                put(0x4124,0x80007000u);
                failed("Overlapping search descriptor and reply were accepted");
                put(0x4124,0x80006000u);
                request.send.size=291;
                failed("Truncated search packet was accepted");
                request.send.size=300;
                fs::rename(image,root/"temporarily-missing.iso");
                require(!iop.canBindRpc(0x80000597u),"Missing original ISO retained its file-search service");
                failed("Cached index replaced a missing ISO during RPC");
                fs::rename(root/"temporarily-missing.iso",image);
                iop.reset();
                require(iop.canBindRpc(0x80000597u),"Reset did not remount the original ISO for search");
                fs::rename(image,root/"valid.iso");
                write(image,std::vector<uint8_t>(32));
                iop.reset();
                require(!iop.canBindRpc(0x80000597u),"Invalid ISO was accepted by file-search service");
                fs::remove(image);
                fs::rename(root/"valid.iso",image);
            }
            IopMemory memory;
            IopKernel kernel(memory);
            kernel.reset();
            IopCdvd cdvd(host,memory,kernel);
            cdvd.reset();
            const auto buffer=memory.allocate(sector,16);
            require(buffer!=0,"IOP CD buffer allocation failed");
            const char path[]="cdrom0:\\usr\\data.bin;1";
            require(memory.writeRam(0x2400,path,sizeof(path)),"IOP test path write failed");
            IopCpuState cpu{};
            cpu.gpr[4]=0x2000; cpu.gpr[5]=0x2400;
            require(cdvd.dispatchImport(10,cpu) && cpu.gpr[2]==1,"IOP file search failed");
            require(memory.read32(0x2000)==29,"IOP search did not preserve the original ISO sector");
            require(memory.read32(0x2004)==6,"IOP search returned an incorrect size");
            cpu={}; cpu.gpr[4]=29; cpu.gpr[5]=1; cpu.gpr[6]=buffer;
            require(cdvd.dispatchImport(6,cpu) && cpu.gpr[2]==1,"IOP ISO sector read failed");
            uint8_t bytes[6]{};
            require(memory.readRam(buffer,bytes,sizeof(bytes)) && std::memcmp(bytes,"abcdef",6)==0,
                    "IOP sector contents differ from original ISO");
            host.fail_reads=true;
            cpu={}; cpu.gpr[4]=16; cpu.gpr[5]=1; cpu.gpr[6]=buffer;
            require(cdvd.dispatchImport(6,cpu) && cpu.gpr[2]==0,
                    "Unreadable original ISO was replaced with synthetic disc data");
        }
        PS2Runtime::IoPaths paths;
        paths.cdRoot=disc.root; paths.cdImage=image;
        paths.hostRoot=disc.root; paths.elfDirectory=disc.root;
        PS2Runtime::setIoPaths(paths);
        {
            PS2Runtime runtime;
            require(runtime.memory().initialize(),"Cannot allocate EE RPC integration memory");
            auto* ram=runtime.memory().getRDRAM();
            R5900Context context{};
            ps2_stubs::sceSifInitRpc(ram,&context,&runtime);
            context.r[4]=_mm_cvtsi32_si128(0x20000);
            context.r[5]=_mm_cvtsi32_si128(0x80000597u);
            ps2_stubs::sceSifBindRpc(ram,&context,&runtime);
            uint32_t server=0;std::memcpy(&server,ram+0x20024,4);
            require(server!=0,"EE SIF bind did not reach the original-ISO search service");
            constexpr uint32_t packet=0x24000,reply=0x28000;
            const char search[]="\\USR\\DATA.BIN;1";
            std::memcpy(ram+packet+36,search,sizeof(search));
            std::memcpy(ram+packet+292,&packet,4);
            context.r[5]=_mm_setzero_si128();context.r[6]=_mm_setzero_si128();
            context.r[7]=_mm_cvtsi32_si128(packet);context.r[8]=_mm_cvtsi32_si128(300);
            context.r[9]=_mm_cvtsi32_si128(reply);context.r[10]=_mm_cvtsi32_si128(4);
            context.r[29]=_mm_cvtsi32_si128(0x2F000);
            ps2_syscalls::sceSifCallRpc(ram,&context,&runtime);
            uint32_t found=0,lba=0;std::memcpy(&found,ram+reply,4);std::memcpy(&lba,ram+packet,4);
            require(getRegU32(&context,2)==0 && found==1 && lba==29,
                    "EE SIF call did not complete a real ISO file search");
        }
        std::vector<uint8_t> memory(PS2_RAM_SIZE);
        const char name[]="cdrom0:\\USR\\DATA.BIN;1";
        std::memcpy(memory.data()+0x2400,name,sizeof(name));
        R5900Context cpu{};
        cpu.r[4]=_mm_cvtsi32_si128(0x2000); cpu.r[5]=_mm_cvtsi32_si128(0x2400);
        ps2_stubs::sceCdSearchFile(memory.data(),&cpu,nullptr);
        require(getRegU32(&cpu,2)==1,"EE file search failed");
        uint32_t lba=0; std::memcpy(&lba,memory.data()+0x2000,4);
        require(lba==29,"EE file search did not preserve the original ISO sector");
        cpu.r[4]=_mm_cvtsi32_si128(29); cpu.r[5]=_mm_cvtsi32_si128(1); cpu.r[6]=_mm_cvtsi32_si128(0x3000);
        ps2_stubs::sceCdRead(memory.data(),&cpu,nullptr);
        require(getRegU32(&cpu,2)==1 && std::memcmp(memory.data()+0x3000,"abcdef",6)==0,
                "EE original-ISO sector read failed");
        fs::remove_all(root);
        std::cout << "Original ISO sector mapping and CD/DVD search RPC passed for EE and IOP\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::error_code ignored; fs::remove_all(root,ignored);
        return 1;
    }
}
