// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct VU1State;
using VectorPacketCallback = void (*)(void *, const uint8_t *, uint32_t);
bool bridgeInitializeNative() noexcept;
void bridgeRun(VU1State &, uint8_t *code, uint8_t *data, uint32_t budget, bool fresh,
               uint64_t generation, VectorPacketCallback, void *context, const void *owner,
               bool jit);
void bridgeForget(const void *owner = nullptr);
