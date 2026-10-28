// SPDX-License-Identifier: GPL-3.0-only
#include "audio/spu2_mirror.h"
#include "IopCounters.h"
#include "IopDma.h"
#include "IopHw.h"
#include "R3000A.h"
#include "SPU2/Dma.h"
#include "SPU2/defs.h"
#include "SPU2/spu2.h"
#include <utility>

// State the PCSX2 SPU2 core expects from the rest of the emulator.
psxRegisters psxRegs{};
psxCounter psxCounters[8]{};
s32 psxNextDeltaCounter = 0;
u32 psxNextStartCounter = 0;
u32 spu2DmaRegisters[2][4]{};
u32 lClocks = 0;
const StereoOut32 StereoOut32::Empty(0, 0);

namespace {
u8 *iopMemory = nullptr;
std::vector<int16_t> mixed;
} // namespace

u8 *iopPhysMem(u32 address) { return iopMemory ? iopMemory + (address & 0x1fffffu) : nullptr; }
void spu2DMA4Irq() {}
void spu2DMA7Irq() {}
void spu2Irq() {}

void spu2Output(StereoOut32 out) {
    mixed.push_back(static_cast<s16>(clamp_mix(out.Left)));
    mixed.push_back(static_cast<s16>(clamp_mix(out.Right)));
}

u32 SPU2::GetConsoleSampleRate() { return SAMPLE_RATE; }
bool SPU2::IsRunningPSXMode() { return false; }

namespace openkai2::spu2 {
namespace {
void clock(uint64_t now) { psxRegs.cycle = static_cast<u32>(now); }
} // namespace

void reset(uint8_t *iopRam, uint64_t now) {
    iopMemory = iopRam;
    clock(now);
    lClocks = psxRegs.cycle;
    mixed.clear();
    std::memset(spu2regs, 0, sizeof(spu2regs));
    std::memset(_spu2mem, 0, sizeof(_spu2mem));
    // The BIOS leaves these blocks looping so idle voices stay silent.
    std::memset(_spu2mem + 0x2800, 7, 0x10);
    std::memset(_spu2mem + 0xe870, 7, 0x10);
    Spdif.Info = 0;
    Cores[0].Init(0);
    Cores[1].Init(1);
    ReverbUpsample = MULTI_ISA_SELECT(ReverbUpsample);
    ReverbDownsample = MULTI_ISA_SELECT(ReverbDownsample);
}

void write16(uint32_t address, uint16_t value, uint64_t now) {
    if (!iopMemory)
        return;
    clock(now);
    TimeUpdate(psxRegs.cycle);
    SPU2_FastWrite(address, value);
}

void dma_write(unsigned core, uint32_t address, uint32_t bytes, uint64_t now) {
    if (!iopMemory || core > 1u || bytes < 2u || (address & 0x1fffffu) + bytes > 0x200000u)
        return;
    clock(now);
    TimeUpdate(psxRegs.cycle);
    spu2DmaRegisters[core][0] = address;
    Cores[core].DoDMAwrite(reinterpret_cast<u16 *>(iopPhysMem(address)), bytes / 2u);
}

void dma_complete(unsigned core, uint64_t now) {
    if (!iopMemory || core > 1u)
        return;
    clock(now);
    TimeUpdate(psxRegs.cycle);
    if (Cores[core].DmaMode)
        Cores[core].Regs.STATX |= 0x80;
    Cores[core].Regs.STATX &= ~0x400;
    Cores[core].TSA = Cores[core].ActiveTSA;
}

void advance(uint64_t now) {
    if (!iopMemory)
        return;
    clock(now);
    TimeUpdate(psxRegs.cycle);
}

void take_output(std::vector<int16_t> &samples) {
    samples.clear();
    std::swap(samples, mixed);
}
} // namespace openkai2::spu2
