// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
struct VU1State;
class GS;
class PS2Memory;
namespace openkai2 {
bool run_vector_backend(VU1State &, uint8_t *code, uint32_t codeSize, uint8_t *data,
                        uint32_t dataSize, GS &, PS2Memory *, uint32_t budget, bool fresh);
void reset_vector_backend(const void *owner);
// Original interpreter snapshots include its precise cycle and transfer schedule.
// Scope their execution explicitly while retaining native coverage elsewhere.
class VectorReferenceScope {
    bool previous_;

  public:
    VectorReferenceScope();
    ~VectorReferenceScope();
    VectorReferenceScope(const VectorReferenceScope &) = delete;
    VectorReferenceScope &operator=(const VectorReferenceScope &) = delete;
};
} // namespace openkai2
