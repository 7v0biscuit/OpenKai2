// SPDX-License-Identifier: GPL-3.0-only
#include "audio/spu2_mirror.h"
#include "state_archive.h"
#include "IopCounters.h"
#include "IopDma.h"
#include "IopHw.h"
#include "R3000A.h"
#include "SPU2/Dma.h"
#include "SPU2/defs.h"
#include "SPU2/spu2.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

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
std::array<std::vector<u16>, 2> remainders;
struct StreamCounts { uint64_t transfers = 0, bytes = 0, unread = 0, starved = 0, starvedCycles = 0, since = 0, last = 0, resumes = 0; bool starving = false; };
std::array<StreamCounts, 2> streamCounts;
std::FILE *dumpFile(const char *suffix) {
    const char *base = std::getenv("OPENKAI2_AUDIO_DUMP");
    if (!base)
        return nullptr;
    return std::fopen((std::string(base) + suffix).c_str(), "wb");
}
std::FILE *mixDump = dumpFile(".mix.raw");
std::array<std::FILE *, 2> inputDump{dumpFile(".adma0.raw"), dumpFile(".adma1.raw")};
void reportStream(unsigned core) {
    const auto &c = streamCounts[core];
    if (c.transfers >= 64u && (c.transfers & (c.transfers - 1u)) == 0u)
        std::cerr << "[audio:adma] core=" << core << " transfers=" << c.transfers << " bytes=" << c.bytes
                  << " unread_at_completion=" << c.unread << " starved=" << c.starved
                  << " starved_ms=" << c.starvedCycles / 36864u << '\n';
}
void observeStreams(uint64_t now) {
    for (unsigned core = 0; core < 2u; ++core) {
        auto &c = streamCounts[core];
        const bool starving = (Cores[core].AutoDMACtrl & ~3u) != 0u && (Cores[core].AutoDMACtrl & (core + 1u)) != 0u;
        if (starving && !c.starving) {
            c.since = now;
            ++c.starved;
        } else if (!starving && c.starving) {
            c.starvedCycles += now - c.since;
            if ((c.starved & (c.starved - 1u)) == 0u)
                std::cerr << "[audio:adma] core=" << core << " starved=" << c.starved << " starved_ms="
                          << c.starvedCycles / 36864u << " transfers=" << c.transfers << '\n';
        }
        c.starving = starving;
    }
}
}

u8 *iopPhysMem(u32 address) { return iopMemory ? iopMemory + (address & 0x1fffffu) : nullptr; }
namespace {
std::array<bool, 2> dmaIrq{};
}
void spu2DMA4Irq() { dmaIrq[0] = true; }
void spu2DMA7Irq() { dmaIrq[1] = true; }
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
}

void reset(uint8_t *iopRam, uint64_t now) {
    iopMemory = iopRam;
    clock(now);
    lClocks = psxRegs.cycle;
    mixed.clear();
    streamCounts = {};
    dmaIrq = {};
    std::memset(spu2regs, 0, sizeof(spu2regs));
    std::memset(_spu2mem, 0, sizeof(_spu2mem));
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
    observeStreams(now);
    SPU2_FastWrite(address, value);
}

void dma_write(unsigned core, uint32_t address, uint32_t bytes, uint64_t now) {
    if (!iopMemory || core > 1u || bytes < 2u || (address & 0x1fffffu) + bytes > 0x200000u)
        return;
    clock(now);
    TimeUpdate(psxRegs.cycle);
    observeStreams(now);
    spu2DmaRegisters[core][0] = address;
    if ((Cores[core].AutoDMACtrl & (core + 1u)) == core + 1u) {
        auto &counts = streamCounts[core];
        if (counts.transfers && now - counts.last > 36864000ull * 2u && counts.resumes++ < 2000u)
            std::cerr << "[audio:music] core=" << core << " resumed after_silence_s=" << (now - counts.last) / 36864000ull
                      << " at_s=" << now / 36864000ull << " transfers=" << counts.transfers << '\n';
        counts.last = now;
        ++streamCounts[core].transfers;
        streamCounts[core].bytes += bytes;
        if (inputDump[core]) {
            std::fwrite(iopPhysMem(address), 1, bytes, inputDump[core]);
            std::fflush(inputDump[core]);
        }
        reportStream(core);
    }
    Cores[core].DoDMAwrite(reinterpret_cast<u16 *>(iopPhysMem(address)), bytes / 2u);
}

void dma_complete(unsigned core, uint64_t now) {
    if (!iopMemory || core > 1u)
        return;
    clock(now);
    TimeUpdate(psxRegs.cycle);
    observeStreams(now);
    auto &state = Cores[core];
    if (state.AdmaInProgress && state.DMAPtr && state.InputDataLeft > 0u) {
        auto &kept = remainders[core];
        kept.assign(state.DMAPtr, state.DMAPtr + state.InputDataProgress + state.InputDataLeft);
        state.DMAPtr = kept.data();
        ++streamCounts[core].unread;
    }
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
    observeStreams(now);
}

bool take_dma_irq(unsigned core) {
    if (core > 1u || !dmaIrq[core])
        return false;
    dmaIrq[core] = false;
    return true;
}

void clear_dma_irq(unsigned core) {
    if (core <= 1u)
        dmaIrq[core] = false;
}

namespace {
int64_t cacheIndex(const s16 *buffer) {
    if (!buffer)
        return -1;
    const auto *base = reinterpret_cast<const uint8_t *>(pcm_cache_data);
    const auto offset = reinterpret_cast<const uint8_t *>(buffer) - base;
    if (offset < 0 || size_t(offset) >= sizeof(pcm_cache_data))
        return -1;
    return int64_t(size_t(offset) / sizeof(PcmCacheEntry));
}
int64_t dmaOffset(unsigned core, const u16 *pointer) {
    if (!pointer)
        return -1;
    const auto &kept = remainders[core];
    if (!kept.empty() && pointer >= kept.data() && pointer <= kept.data() + kept.size())
        return (int64_t(1) << 40) | int64_t(pointer - kept.data());
    if (iopMemory && reinterpret_cast<const u8 *>(pointer) >= iopMemory &&
        reinterpret_cast<const u8 *>(pointer) < iopMemory + 0x200000u)
        return int64_t(reinterpret_cast<const u8 *>(pointer) - iopMemory);
    return -1;
}
u16 *dmaPointer(unsigned core, int64_t offset) {
    if (offset < 0)
        return nullptr;
    if (offset >> 40)
        return remainders[core].data() + (offset & ((int64_t(1) << 40) - 1));
    return iopMemory ? reinterpret_cast<u16 *>(iopMemory + offset) : nullptr;
}
}

void serialize(StateArchive &archive) {
    archive.section(0x32555053u);
    std::array<std::array<int64_t, 24>, 2> buffers{};
    std::array<int64_t, 2> dma{}, dmaRead{};
    if (!archive.loading())
        for (unsigned core = 0; core < 2u; ++core) {
            for (unsigned voice = 0; voice < 24u; ++voice)
                buffers[core][voice] = cacheIndex(Cores[core].Voices[voice].SBuffer);
            dma[core] = dmaOffset(core, Cores[core].DMAPtr);
            dmaRead[core] = dmaOffset(core, Cores[core].DMARPtr);
        }
    archive(remainders, buffers, dma, dmaRead, dmaIrq);
    archive.bytes(spu2regs, sizeof spu2regs);
    archive.bytes(_spu2mem, sizeof _spu2mem);
    archive.bytes(Cores, sizeof Cores);
    archive.bytes(&Spdif, sizeof Spdif);
    archive.bytes(&DCFilterIn, sizeof DCFilterIn);
    archive.bytes(&DCFilterOut, sizeof DCFilterOut);
    archive(OutPos, InputPos, Cycles, PlayMode, lClocks, psxRegs.cycle, psxNextDeltaCounter, psxNextStartCounter);
    archive.bytes(psxCounters, sizeof psxCounters);
    archive.bytes(spu2DmaRegisters, sizeof spu2DmaRegisters);
    if (!archive.loading())
        return;
    for (unsigned core = 0; core < 2u; ++core) {
        for (unsigned voice = 0; voice < 24u; ++voice)
            Cores[core].Voices[voice].SBuffer =
                buffers[core][voice] >= 0 ? pcm_cache_data[buffers[core][voice]].Sampledata : nullptr;
        Cores[core].DMAPtr = dmaPointer(core, dma[core]);
        Cores[core].DMARPtr = dmaPointer(core, dmaRead[core]);
    }
    for (auto &entry : pcm_cache_data)
        entry.Validated = false;
    mixed.clear();
}

void take_output(std::vector<int16_t> &samples) {
    if (mixDump && !mixed.empty()) {
        std::fwrite(mixed.data(), sizeof(int16_t), mixed.size(), mixDump);
        std::fflush(mixDump);
    }
    samples.clear();
    std::swap(samples, mixed);
}
}
