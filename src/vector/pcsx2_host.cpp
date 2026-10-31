// SPDX-License-Identifier: GPL-3.0-only
#include "Common.h"
#include "Gif_Unit.h"
#include "VUmicro.h"
#include "common/FileSystem.h"
#include "common/Perf.h"
#include "runtime/ps2_vu1.h"
#include "vector/pcsx2_bridge.h"
#include <array>
#include <bitset>
#include <exception>
#include <iostream>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
VURegs vuRegs[2];
BaseVUmicroCPU *CpuVU0 = &CpuIntVU0;
BaseVUmicroCPU *CpuVU1 = &CpuIntVU1;
namespace {
constexpr size_t cacheBytes = 32u * 1024u * 1024u;
u8 *caches[2]{};
alignas(16) std::array<u8, 0x4000> nativeCode{}, nativeData{};
alignas(16) std::array<u8, 0x1000> vu0Code{}, vu0Data{};
std::array<u8, 65536> packet{};
size_t packetBytes = 0, parsedBytes = 0;
VectorPacketCallback callback = nullptr;
void *callbackContext = nullptr;
std::exception_ptr callbackError;
bool packetError = false;
std::array<char, 512> vectorError{};
bool jitActive = false, initialized = false, nativeInitialized = false,
     initializationFailed = false, previousBusy = false;
u8 *externalData = nullptr;
u8 *dataMemory = nativeData.data();
uint64_t codeGeneration = ~uint64_t(0);
const u8 *codeSource = nullptr;
const void *activeOwner = nullptr;
std::recursive_mutex executionMutex;
bool executing = false;
}
static u8 *nearby(size_t bytes, bool executable) {
    const uintptr_t anchor = reinterpret_cast<uintptr_t>(&vuRegs) & ~uintptr_t(0xffffu);
    for (uintptr_t distance = 64u * 1024u * 1024u; distance <= 1536u * 1024u * 1024u;
         distance += 64u * 1024u * 1024u) {
        for (bool above : {true, false}) {
            if ((above && anchor > std::numeric_limits<uintptr_t>::max() - distance - bytes) ||
                (!above && anchor < distance + 0x10000u))
                continue;
            const uintptr_t address = above ? anchor + distance : anchor - distance;
#ifdef _WIN32
            void *ptr = VirtualAlloc(reinterpret_cast<void *>(address), bytes, MEM_RESERVE | MEM_COMMIT,
                                     executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
            if (!ptr)
                continue;
#else
            void *ptr = mmap(reinterpret_cast<void *>(address), bytes,
                             PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0),
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (ptr == MAP_FAILED)
                continue;
#endif
            return static_cast<u8 *>(ptr);
        }
    }
    throw std::runtime_error("Unable to allocate vector memory near the runtime");
}
namespace {
constexpr size_t dataBlockBytes = 0x4000u, dataBlocks = 32u;
u8 *dataRegion = nullptr;
std::bitset<dataBlocks> dataUsed;
std::mutex dataMutex;
bool directData(const u8 *data) {
    return dataRegion && data >= dataRegion && data < dataRegion + dataBlockBytes * dataBlocks;
}
}
namespace openkai2 {
uint8_t *allocate_vector_data() {
    {
        std::lock_guard lock(dataMutex);
        try {
            if (!dataRegion)
                dataRegion = nearby(dataBlockBytes * dataBlocks, false);
            for (size_t i = 0; i < dataBlocks; ++i)
                if (!dataUsed[i]) {
                    dataUsed.set(i);
                    return dataRegion + i * dataBlockBytes;
                }
        } catch (const std::exception &) {
        }
    }
    return new uint8_t[dataBlockBytes];
}
void release_vector_data(uint8_t *data) {
    if (!data)
        return;
    {
        std::lock_guard lock(dataMutex);
        if (directData(data)) {
            dataUsed.reset(size_t(data - dataRegion) / dataBlockBytes);
            return;
        }
    }
    delete[] data;
}
}
namespace SysMemory {
u8 *cache(unsigned unit) {
    if (!caches[unit])
        caches[unit] = nearby(cacheBytes, true);
    return caches[unit];
}
u8 *GetVU0Rec() { return cache(0); }
u8 *GetVU1Rec() { return cache(1); }
u8 *GetVU0RecEnd() { return cache(0) + cacheBytes; }
u8 *GetVU1RecEnd() { return cache(1) + cacheBytes; }
}
namespace Perf {
Group any{nullptr}, ee{nullptr}, iop{nullptr}, vu0{nullptr}, vu1{nullptr}, vif{nullptr};
void Group::Register(const void *, size_t, const char *) {}
void Group::RegisterPC(const void *, size_t, u32) {}
void Group::RegisterKey(const void *, size_t, const char *, u64) {}
}
namespace FileSystem {
std::FILE *OpenCFile(const char *name, const char *mode, Error *) { return std::fopen(name, mode); }
}
namespace Path {
std::string Combine(std::string_view left, std::string_view right) {
    return std::string(left) + "/" + std::string(right);
}
}
void pxOnAssertFail(const char *file, int line, const char *, const char *message) {
    std::cerr << file << ':' << line << ' ' << message << '\n';
    std::abort();
}
void reportVectorError(const char *message, ...) noexcept {
    if (vectorError[0])
        return;
    va_list args;
    va_start(args, message);
    std::vsnprintf(vectorError.data(), vectorError.size(), message, args);
    va_end(args);
}
void hwIntcIrq(int) {}
void CPU_INT(int, int) {}
static u32 tagSize(const u8 *p) {
    u64 tag;
    std::memcpy(&tag, p, 8u);
    u32 loops = tag & 0x7fffu, regs = (tag >> 60u) & 15u, mode = (tag >> 58u) & 3u;
    if (!regs)
        regs = 16u;
    return 16u + (mode == 0u   ? loops * regs * 16u
                  : mode == 1u ? ((loops * regs + 1u) / 2u) * 16u
                               : loops * 16u);
}
u32 BridgeGif::GetGSPacketSize(int, const u8 *memory, u32 offset, u32 maximum, bool flush) {
    u32 total = 0;
    for (unsigned i = 0; i < 1024u; ++i) {
        const auto *p = memory + (offset & 0x3fffu);
        u64 tag;
        std::memcpy(&tag, p, 8u);
        u32 bytes = tagSize(p);
        offset += bytes;
        total += bytes;
        if (total >= 0x4000u)
            return 0;
        if (total >= maximum)
            return maximum;
        bool eop = (tag & 0x8000u) != 0;
        if (!jitActive && ((flush && eop) || !flush))
            return total | (u32(eop) << 31u);
        if (eop)
            return total;
    }
    return 0;
}
u32 BridgeGif::TransferGSPacketData(int, const u8 *p, u32 bytes, bool) {
    if (packetError || callbackError)
        return bytes;
    if (bytes > packet.size() - packetBytes) {
        packetError = true;
        return bytes;
    }
    std::memcpy(packet.data() + packetBytes, p, bytes);
    packetBytes += bytes;
    while (packetBytes - parsedBytes >= 16u) {
        const auto *tag = packet.data() + parsedBytes;
        const u32 size = tagSize(tag);
        if (size > packet.size() - parsedBytes) {
            packetError = true;
            return bytes;
        }
        if (size > packetBytes - parsedBytes)
            break;
        u64 word;
        std::memcpy(&word, tag, 8u);
        parsedBytes += size;
        if ((word & 0x8000u) == 0u)
            continue;
        if (callback) {
            try {
                if (externalData)
                    std::memcpy(externalData, nativeData.data(), nativeData.size());
                callback(callbackContext, packet.data(), static_cast<u32>(parsedBytes));
                if (externalData)
                    std::memcpy(nativeData.data(), externalData, nativeData.size());
            } catch (...) {
                callbackError = std::current_exception();
                return bytes;
            }
        }
        std::memmove(packet.data(), packet.data() + parsedBytes, packetBytes - parsedBytes);
        packetBytes -= parsedBytes;
        parsedBytes = 0;
    }
    return bytes;
}
void BridgeGifPath::CopyGSPacketData(const u8 *p, u32 bytes, bool aligned) {
    gifUnit.TransferGSPacketData(0, p, bytes, aligned);
}
static void initializeRegisters(u8 *code, u8 *data) {
    for (unsigned i = 0; i < 2u; ++i) {
        std::memset(&vuRegs[i], 0, sizeof(VURegs));
        vuRegs[i].idx = i;
        vuRegs[i].VF[0].f.w = 1.0f;
        vuRegs[i].VI[REG_R].UL = 0x3f800000u;
        vuRegs[i].VI[REG_Q].F = 1.0f;
        vuRegs[i].q.F = 1.0f;
        vuRegs[i].pending_q = 0x3f800000u;
    }
    VU0.Micro = vu0Code.data();
    VU0.Mem = vu0Data.data();
    VU1.Micro = code;
    VU1.Mem = data;
}
void bridgeForget(const void *owner) {
    std::lock_guard lock(executionMutex);
    if (owner && owner != activeOwner)
        return;
    if (executing)
        throw std::runtime_error("Vector reset during execution");
    activeOwner = nullptr;
    previousBusy = false;
    codeSource = nullptr;
    if (nativeInitialized)
        CpuMicroVU1.Clear(0u, 0x4000u);
}
bool bridgeInitializeNative() noexcept {
    std::lock_guard lock(executionMutex);
    if (nativeInitialized)
        return true;
    if (initializationFailed || previousBusy)
        return false;
    try {
        if (!initialized) {
            initializeRegisters(nativeCode.data(), dataMemory);
            initialized = true;
        }
        SysMemory::GetVU1Rec();
        CpuMicroVU1.Reserve();
        CpuMicroVU1.Reset();
        nativeInitialized = true;
        return true;
    } catch (const std::exception &error) {
        std::cerr << "[vector:initialization] " << error.what() << '\n';
        initializationFailed = true;
        return false;
    }
}
void bridgeRun(VU1State &state, u8 *code, u8 *data, u32 budget, bool fresh, uint64_t generation,
               VectorPacketCallback fn, void *context, const void *owner, bool jit) {
    std::lock_guard lock(executionMutex);
    if (executing)
        throw std::runtime_error("Recursive vector execution is unsupported");
    struct Guard {
        Guard() { executing = true; }
        ~Guard() {
            executing = false;
            callback = nullptr;
            callbackContext = nullptr;
            externalData = nullptr;
        }
    } guard;
    if (jitActive != jit && previousBusy)
        throw std::runtime_error("Cannot change vector engine during a program");
    if (!initialized) {
        initializeRegisters(nativeCode.data(), dataMemory);
        initialized = true;
    }
    if (jit && !bridgeInitializeNative())
        throw std::runtime_error("Native vector executable memory is unavailable");
    if (jitActive != jit) {
        if (previousBusy)
            throw std::runtime_error("Cannot change vector engine during a program");
        if (nativeInitialized)
            CpuMicroVU1.Reset();
        activeOwner = nullptr;
        codeSource = nullptr;
    }
    jitActive = jit;
    CpuVU1 = jit ? static_cast<BaseVUmicroCPU *>(&CpuMicroVU1)
                 : static_cast<BaseVUmicroCPU *>(&CpuIntVU1);
    const bool newOwner = owner != activeOwner;
    if (newOwner && previousBusy)
        throw std::runtime_error("Another vector context is still executing");
    if (!fresh && newOwner)
        throw std::runtime_error("Vector continuation has no active context");
    if (newOwner || codeSource != code || codeGeneration != generation) {
        if (newOwner || std::memcmp(nativeCode.data(), code, nativeCode.size()) != 0) {
            if (!fresh && previousBusy)
                throw std::runtime_error("Vector code changed during a suspended program");
            std::memcpy(nativeCode.data(), code, nativeCode.size());
            if (nativeInitialized)
                CpuMicroVU1.Clear(0u, 0x4000u);
        }
        codeSource = code;
        codeGeneration = generation;
    }
    u8 *const memory = directData(data) ? data : nativeData.data();
    if (memory != dataMemory) {
        if (previousBusy)
            throw std::runtime_error("Vector data memory changed during a suspended program");
        if (nativeInitialized)
            CpuMicroVU1.Reset();
        dataMemory = memory;
        VU1.Mem = memory;
    }
    const bool copied = memory != data;
    if (fresh) {
        if (previousBusy && nativeInitialized)
            CpuMicroVU1.Clear(0u, 0x4000u);
        initializeRegisters(nativeCode.data(), dataMemory);
        packetBytes = parsedBytes = 0u;
        std::memcpy(VU1.VF, state.vf, sizeof(state.vf));
        VU1.VF[0] = {};
        VU1.VF[0].f.w = 1.0f;
        for (unsigned i = 1u; i < 16u; ++i)
            VU1.VI[i].UL = uint16_t(state.vi[i]);
        std::memcpy(&VU1.ACC, state.acc, sizeof(state.acc));
        VU1.VI[REG_Q].F = VU1.q.F = state.q;
        std::memcpy(&VU1.pending_q, &state.q, 4u);
        VU1.VI[REG_P].F = VU1.p.F = state.p;
        std::memcpy(&VU1.pending_p, &state.p, 4u);
        VU1.VI[REG_I].F = state.i;
        VU1.VI[REG_R].UL = state.r;
        VU1.VI[REG_STATUS_FLAG].UL = VU1.statusflag = state.status;
        VU1.VI[REG_MAC_FLAG].UL = VU1.macflag = state.mac;
        VU1.VI[REG_CLIP_FLAG].UL = VU1.clipflag = state.clip;
        const u32 internalStatus = ((state.status >> 3u) & 0x18u) |
                                   ((state.status << 11u) & 0x1800u) |
                                   ((state.status << 14u) & 0x3cf0000u);
        for (unsigned i = 0; i < 4u; ++i) {
            VU1.micro_statusflags[i] = internalStatus;
            VU1.micro_macflags[i] = state.mac;
            VU1.micro_clipflags[i] = state.clip;
        }
        VU1.VI[REG_TPC].UL = (state.pc & 0x3fffu) >> 3u;
        VU1.cycle = static_cast<u32>(state.cycles);
        CpuVU1->SetStartPC(state.pc & 0x3fffu);
    }
    activeOwner = owner;
    if (copied)
        std::memcpy(nativeData.data(), data, nativeData.size());
    vif1Regs.top = state.top & 0x3ffu;
    vif1Regs.itop = state.itop & 0x3ffu;
    vif1Regs.stat.VEW = true;
    VU0.VI[REG_FBRST].UL = (state.dBitEnabled ? 0x400u : 0u) | (state.tBitEnabled ? 0x800u : 0u);
    VU0.VI[REG_VPU_STAT].UL = 0x100u;
    cpuRegs.cycle = VU1.cycle;
    externalData = copied ? data : nullptr;
    callback = fn;
    callbackContext = context;
    callbackError = nullptr;
    packetError = false;
    vectorError[0] = 0;
    const FPControlRegisterBackup fpcrBackup(FPControlRegister::GetCurrent());
    EmuConfig.Cpu.FPUFPCR = FPControlRegister::GetCurrent();
    const u32 before = VU1.cycle;
    CpuVU1->Execute(budget);
    if (copied)
        std::memcpy(data, nativeData.data(), nativeData.size());
    std::memcpy(state.vf, VU1.VF, sizeof(state.vf));
    std::memcpy(state.acc, &VU1.ACC, sizeof(state.acc));
    for (unsigned i = 0u; i < 16u; ++i)
        state.vi[i] = static_cast<int16_t>(VU1.VI[i].UL);
    state.q = VU1.VI[REG_Q].F;
    state.p = VU1.VI[REG_P].F;
    state.i = VU1.VI[REG_I].F;
    state.r = VU1.VI[REG_R].UL;
    state.pc = (VU1.VI[REG_TPC].UL << 3u) & 0x3fffu;
    state.cycles += u32(VU1.cycle - before);
    state.mac = VU1.VI[REG_MAC_FLAG].UL;
    state.clip = VU1.VI[REG_CLIP_FLAG].UL;
    state.status = VU1.VI[REG_STATUS_FLAG].UL;
    previousBusy = (VU0.VI[REG_VPU_STAT].UL & 0x100u) != 0u;
    state.stoppedByD = (VU0.VI[REG_VPU_STAT].UL & 0x200u) != 0u;
    state.stoppedByT = (VU0.VI[REG_VPU_STAT].UL & 0x400u) != 0u;
    state.ebit = !previousBusy && !state.stoppedByD && !state.stoppedByT;
    state.haltAfterDelaySlot = false;
    state.branchPending = false;
    state.branchDelay = 0;
    if (callbackError)
        std::rethrow_exception(callbackError);
    if (vectorError[0])
        throw std::runtime_error(std::string("Vector core error: ") + vectorError.data());
    if (packetError)
        throw std::runtime_error("Invalid vector graphics packet");
}
