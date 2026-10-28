// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <vector>

namespace openkai2::spu2 {
// A second SPU2 that only listens. The emulated IOP keeps answering every
// register read, DMA and interrupt the game sees; each SPU2 register write
// and transfer is also replayed into PCSX2's SPU2 core, which synthesizes the
// voices, effects and streamed input the console would play. Times are IOP
// cycles.
void reset(uint8_t *iopRam, uint64_t now);
void write16(uint32_t address, uint16_t value, uint64_t now);
void dma_write(unsigned core, uint32_t address, uint32_t bytes, uint64_t now);
void dma_complete(unsigned core, uint64_t now);
void advance(uint64_t now);
// Interleaved stereo frames mixed since the last call, moved into samples.
void take_output(std::vector<int16_t> &samples);
} // namespace openkai2::spu2
