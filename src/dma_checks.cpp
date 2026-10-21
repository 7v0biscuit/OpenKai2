// SPDX-License-Identifier: GPL-3.0-only
#include "dma_checks.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace openkai2 {
namespace {
void require(bool passed,const char* message) {
    if (!passed) throw std::runtime_error(message);
}
}
void verify_scratchpad_dma() {
    constexpr uint32_t channel=0x1000d000u, destination=0x003c4200u;
    PS2Runtime runtime;
    auto& memory=runtime.memory();
    require(memory.initialize(),"Cannot allocate scratchpad DMA regression memory");
    auto* ram=memory.getRDRAM();
    auto* scratch=memory.getScratchpad();
    R5900Context context{};
    auto receive=[&](uint32_t selector,uint32_t address,uint32_t count) {
        context.r[4]=_mm_cvtsi32_si128(selector);
        context.r[5]=_mm_cvtsi32_si128(address);
        context.r[6]=_mm_cvtsi32_si128(count);
        context.r[7]=_mm_cvtsi32_si128(12); // Retail caller leaves this unused argument set.
        ps2_stubs::sceDmaRecvN(ram,&context,&runtime);
        return int32_t(getRegU32(&context,2));
    };
    for (uint32_t i=0;i<PS2_SCRATCHPAD_SIZE;++i) scratch[i]=uint8_t(i*37u+(i>>8));
    const std::vector<uint8_t> original(scratch,scratch+PS2_SCRATCHPAD_SIZE);
    struct Transfer { uint32_t selector,address,start,count; };
    const Transfer cases[]={
        {channel,destination,0u,0x400u}, // Exact channel, address and count from the startup log.
        {8u,0x803c4200u,0x3ff0u,3u}, // Cached alias, wrapping scratchpad source.
        {channel,0xa03c4200u,0x120u,17u}, // Uncached alias, nonzero scratchpad offset.
        {channel,destination,0x230u,0u},
    };
    for (const auto& transfer:cases) {
        const uint32_t bytes=transfer.count*16u;
        std::memset(ram+destination-16,0xcc,PS2_SCRATCHPAD_SIZE+32);
        memory.writeIORegister(0x1000e010u,1u<<8); // Clear the previous completion status.
        memory.writeIORegister(channel+0x80,transfer.start);
        memory.writeIORegister(channel,0xcdu); // Preserve unrelated bits, clear DIR and MOD.
        require(receive(transfer.selector,transfer.address,transfer.count)==0,
                "Scratchpad receive was rejected");
        for (uint32_t i=0;i<bytes;++i)
            require(ram[destination+i]==original[(transfer.start+i)&0x3fffu],
                    "Scratchpad DMA did not copy the original bytes");
        require(std::equal(original.begin(),original.end(),scratch),"Receive changed scratchpad source bytes");
        for (uint32_t i=0;i<16;++i)
            require(ram[destination-16+i]==0xcc && ram[destination+bytes+i]==0xcc,
                    "Scratchpad DMA overwrote adjacent main memory");
        require(memory.readIORegister(channel)==0xc0u && memory.readIORegister(channel+0x20)==0u,
                "Receive did not clear STR/QWC or preserve CHCR flags");
        require(memory.readIORegister(channel+0x10)==destination+bytes
                && memory.readIORegister(channel+0x80)==((transfer.start+bytes)&0x3ff0u),
                "Receive did not advance MADR/SADR");
        require((memory.readIORegister(0x1000e010u)&(1u<<8))!=0,
                "Receive did not raise the scratchpad DMA completion status");
        context.r[4]=_mm_cvtsi32_si128(channel);
        context.r[5]=_mm_cvtsi32_si128(1);
        ps2_stubs::sceDmaSync(ram,&context,&runtime);
        require(getRegU32(&context,2)==0,"Completed receive still polls busy");
    }
    // Exercise both SDK directions rather than testing only direct register writes.
    constexpr uint32_t source=0x24000u;
    for (uint32_t i=0;i<64;++i) ram[source+i]=uint8_t(255u-i);
    memory.writeIORegister(0x1000d480u,0x3fe0u);
    context.r[4]=_mm_cvtsi32_si128(9);
    context.r[5]=_mm_cvtsi32_si128(source);
    context.r[6]=_mm_cvtsi32_si128(4);
    ps2_stubs::sceDmaSendN(ram,&context,&runtime);
    require(getRegU32(&context,2)==0,"SDK scratchpad send failed");
    memory.writeIORegister(channel+0x80,0x3fe0u);
    require(receive(channel,destination,4u)==0 && std::memcmp(ram+source,ram+destination,64)==0,
            "SDK scratchpad send/receive round trip lost data");
    const auto previousMadr=memory.readIORegister(channel+0x10);
    require(receive(channel,PS2_RAM_SIZE-16u,2u)==-1
            && memory.readIORegister(channel+0x10)==previousMadr,
            "Out-of-range receive changed the channel");
    require(receive(channel,destination,0x10000u)==-1,"Excessive receive count was accepted");
    require(receive(0x12345678u,destination,1u)==-1,"Invalid DMA channel was accepted");
    bool unsupported=false;
    try { receive(3u,destination,1u); }
    catch (const std::runtime_error&) { unsupported=true; }
    require(unsupported,"Unmodeled receive device reported success");
    // A pending channel must not be overwritten by a second submission.
    memory.writeIORegister(0x1000e000u,0);
    memory.writeIORegister(0x1000e010u,1u<<8);
    memory.writeIORegister(channel,0x100u);
    require(receive(channel,destination,1u)==-1
            && memory.readIORegister(channel)==0x100u
            && memory.readIORegister(channel+0x10)==previousMadr
            && (memory.readIORegister(0x1000e010u)&(1u<<8))==0,
            "Receive replaced an active transfer or invented completion");
    memory.writeIORegister(channel,0);
    memory.writeIORegister(0x1000e000u,1);
    context.r[4]=_mm_cvtsi32_si128(channel);
    ps2_stubs::sceDmaRecvN(ram,&context,nullptr);
    require(int32_t(getRegU32(&context,2))==-1,"Receive accepted a missing runtime");
}
}
