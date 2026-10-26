// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/Pcsx2Defs.h"
struct BridgeCpu {
    u32 cycle = 0, code = 0;
    struct {
        u128 r[32];
    } GPR;
};
inline BridgeCpu cpuRegs;
