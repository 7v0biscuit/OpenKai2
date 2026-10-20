// SPDX-License-Identifier: GPL-3.0-only
#include "iso_fixture.h"
#include "disc_image.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_cdvd.h"
#include "ps2x/iop/iop_host.h"
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
    std::ifstream stream_;
public:
    bool fail_reads=false;
    DiscHost(fs::path image, fs::path root) : image_(std::move(image)), root_(std::move(root)),
                                            stream_(image_,std::ios::binary) {}
    bool readGuest(uint32_t,void*,size_t) const override { return false; }
    bool writeGuest(uint32_t,const void*,size_t) override { return false; }
    bool zeroGuest(uint32_t,size_t) override { return false; }
    bool normalizeGuestAddress(uint32_t,uint32_t&) const override { return false; }
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
        stream_.clear(); stream_.seekg(std::streamoff(offset));
        stream_.read(static_cast<char*>(data),std::streamsize(size));
        read_bytes=size_t(stream_.gcount());
        return read_bytes==size;
    }
    void closeHostFile(uint64_t) override {}
    int32_t memoryCard(const MemoryCardRequest&) override { return 0; }
    bool hasGuestFunction(uint32_t) const override { return false; }
    bool invokeGuestFunction(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*) override { return false; }
    void log(LogLevel,std::string_view) override {}
};
}

int main() {
    const auto root=fs::temp_directory_path()/("openkai2-cdvd-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto image=root/"original.iso";
        auto data=fixture();
        place(data,20*sector+2*34,record("DATA.BIN;1",29,6));
        const std::string contents="abcdef";
        std::copy(contents.begin(),contents.end(),data.begin()+29*sector);
        write(image,data);
        openkai2::DiscOptions options;
        options.expected_executable_sha256="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        const auto disc=openkai2::prepare_disc(image,root/"cache",options);
        {
            DiscHost host(image,disc.root);
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
        std::cout << "Original ISO sector mapping passed for EE and IOP\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::error_code ignored; fs::remove_all(root,ignored);
        return 1;
    }
}
