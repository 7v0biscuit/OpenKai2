// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// SPU2 DMA completion and IRQ notifications. The emulated IOP raises the
// game-visible interrupts itself, so the mirrored core's are ignored.
void spu2DMA4Irq();
void spu2DMA7Irq();
void spu2Irq();
