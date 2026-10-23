// SPDX-License-Identifier: GPL-3.0-only
#include "dma_checks.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <stdexcept>
#include <vector>

namespace openkai2 {
namespace {
void require(bool passed,const char* message) {
    if (!passed) throw std::runtime_error(message);
}
}
void verify_empty_graphics_dma() {
    auto owned=std::make_unique<PS2Runtime>();auto& memory=owned->memory();
    require(memory.initialize(),"Cannot allocate empty DMA regression memory");
    constexpr uint32_t tags=0x26000u,status=0x1000e010u;
    const std::pair<uint32_t,uint32_t> channels[]={{0x10008000u,0u},{0x10009000u,1u},{0x1000a000u,2u}};
    for(const auto& [channel,cause]:channels) for(unsigned variant=0;variant<6u;++variant) {
        memory.write32(status,0x3ffu);memory.consumeCompletedDmacCauses();
        memory.write32(channel,0u);memory.write32(channel+0x10u,tags);
        memory.write32(channel+0x20u,variant==4u ? 1u : 0u);memory.write32(channel+0x30u,tags);
        // Normal zero-QWC, END(0), REFE(0), and IRQ-terminated CNT(0).
        // Include ordinary nonempty normal/chain transfers to retain their IRQ behavior.
        const uint64_t tag=variant==1u ? 0x70000000ull : variant==3u ? 0x90000000ull
            : variant==5u ? 0x70000001ull : 0ull;
        memory.write64(tags,tag);memory.write64(tags+8u,0u);
        memory.write64(tags+16u,0u);memory.write64(tags+24u,0u);
        memory.write32(channel,variant==0u || variant==4u ? 0x101u : variant==3u ? 0x185u : 0x105u);
        memory.processPendingTransfers();
        require(memory.consumeCompletedDmacCauses()==std::vector<uint32_t>{cause},
                "Empty graphics DMA lost its completion interrupt");
        require((memory.read32(channel)&0x100u)==0u && memory.read32(channel+0x20u)==0u
                && (memory.read32(status)&(1u<<cause))!=0u,"Empty graphics DMA remained busy");
        memory.processPendingTransfers();
        require(memory.consumeCompletedDmacCauses().empty(),"Empty graphics DMA completed twice");
    }
    for(const auto& [channel,cause]:channels) for(unsigned variant=0;variant<2u;++variant) {
        memory.write32(channel,0u);memory.write32(status,0x3ffu);memory.consumeCompletedDmacCauses();
        memory.write32(channel+0x20u,0u);
        memory.write32(channel+0x30u,variant ? PS2_RAM_SIZE : tags);
        memory.write64(tags,(uint64_t(tags)<<32u)|0x20000000u); // Endless NEXT(0).
        memory.write32(channel,0x105u);memory.processPendingTransfers();
        require(memory.consumeCompletedDmacCauses().empty() && (memory.read32(status)&(1u<<cause))==0u
                && (memory.read32(channel)&0x100u)!=0u && (memory.read32(channel)&0x100u)!=0u,
                "Invalid empty graphics chain fabricated completion or polling cleared its busy bit");
    }
}
void verify_ipu_input_dma() {
    PS2Runtime runtime;auto& memory=runtime.memory();
    require(memory.initialize(),"Cannot allocate IPU DMA regression memory");
    constexpr uint32_t channel=0x1000b400u,status=0x1000e010u,source=0x26000u,tags=0x27000u;
    // The reset command is self-clearing, including when CRI saves/restores
    // IPU control around a picture. Preserve unrelated channel status bits.
    memory.write32(0x10002010u,0x40000000u);
    require((memory.read32(0x10002010u)&0xc0000000u)==0u,"IPU reset or busy bit remained set");
    memory.write32(channel+0x10u,0xa0000000u|source);
    memory.write32(channel+0x20u,2u);memory.write32(channel,0x101u);
    require(memory.read32(channel+0x10u)==(0xa0000000u|source)+32u
            && memory.read32(channel+0x20u)==0u && (memory.read32(channel)&0x100u)==0u
            && (memory.read32(status)&(1u<<5u))!=0u,"Normal IPU DMA did not retire its actual input");
    for (unsigned test=0;test<4u;++test) {
        memory.write32(channel,0u);memory.write32(status,1u<<5u);
        const uint32_t address=test==0u ? PS2_RAM_SIZE-16u : source;
        memory.write32(channel+0x10u,address);memory.write32(channel+0x20u,test==1u ? 0u : 2u);
        memory.write32(channel+0x30u,tags);
        // A non-terminating NEXT chain must exhaust its bounded tag budget
        // without delivering partial bytes or inventing completion.
        memory.write64(tags,uint64_t(tags)<<32u | 0x20000000u);
        const uint32_t start=test==1u ? 0x105u : test==2u ? 0x109u : test==3u ? 0x100u : 0x101u;
        memory.write32(channel,start);
        require((memory.read32(channel)&0x100u)!=0u && memory.read32(channel+0x10u)==address
                && (memory.read32(status)&(1u<<5u))==0u,
                "Invalid or unsupported IPU DMA fabricated completion or consumed bytes");
    }
    memory.write32(channel,0u);memory.write32(0x10002010u,0x40000000u);
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
