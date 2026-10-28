// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/Pcsx2Types.h"
// The IOP clock the SPU2 core times itself against; spu2_host.cpp sets it
// from the emulated IOP's cycle count.
struct psxRegisters { u32 cycle; };
extern psxRegisters psxRegs;
