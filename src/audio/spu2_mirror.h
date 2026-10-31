// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <vector>

namespace openkai2::spu2 {
void reset(uint8_t *iopRam, uint64_t now);
void write16(uint32_t address, uint16_t value, uint64_t now);
void dma_write(unsigned core, uint32_t address, uint32_t bytes, uint64_t now);
void dma_complete(unsigned core, uint64_t now);
void advance(uint64_t now);
void take_output(std::vector<int16_t> &samples);
}
