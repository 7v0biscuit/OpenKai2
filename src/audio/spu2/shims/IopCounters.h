// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/Pcsx2Types.h"
struct psxCounter { u64 count, target; u32 mode, rate, interrupt; u32 startCycle; s32 deltaCycles; };
extern psxCounter psxCounters[8];
extern s32 psxNextDeltaCounter;
extern u32 psxNextStartCounter;
