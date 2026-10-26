// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/Pcsx2Defs.h"
struct VIFregisters {
    union Stat {
        struct {
            u32 VPS : 2, VEW : 1, VGW : 1, reserved : 28;
        };
        u32 _u32;
        constexpr Stat() : _u32(0) {}
    } stat;
    u32 top = 0, tops = 0, itop = 0, itops = 0;
};
inline VIFregisters vif0Regs, vif1Regs;
