// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/Pcsx2Types.h"
// SPU2 DMA channel registers as the core sees them. The emulated IOP owns
// the real registers; these copies only keep the core's bookkeeping.
extern u32 spu2DmaRegisters[2][4];
#define HW_DMA4_MADR (spu2DmaRegisters[0][0])
#define HW_DMA4_BCR (spu2DmaRegisters[0][1])
#define HW_DMA4_CHCR (spu2DmaRegisters[0][2])
#define HW_DMA4_TADR (spu2DmaRegisters[0][3])
#define HW_DMA7_MADR (spu2DmaRegisters[1][0])
#define HW_DMA7_BCR (spu2DmaRegisters[1][1])
#define HW_DMA7_CHCR (spu2DmaRegisters[1][2])
#define HW_DMA7_TADR (spu2DmaRegisters[1][3])
// IOP physical memory, where AutoDMA streams are read from as they play.
u8 *iopPhysMem(u32 address);
