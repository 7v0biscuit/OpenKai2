// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Vif.h"
#include <atomic>
struct BridgeMTVU {
    VIFregisters vifRegs;
    u32 vuFBRST = 0;
    std::atomic<u32> mtvuInterrupts{0};
    bool IsOpen() const { return false; }
    void Open() {}
    void WaitVU() {}
    void Get_MTVUChanges() {}
};
inline BridgeMTVU vu1Thread;

struct VU_Thread {
    static constexpr u32 InterruptFlagVUTBit = 1, InterruptFlagVUEBit = 2;
};
