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
uint8_t *allocate_vector_data();
void release_vector_data(uint8_t *data);
class VectorReferenceScope {
    bool previous_;

  public:
    VectorReferenceScope();
    ~VectorReferenceScope();
    VectorReferenceScope(const VectorReferenceScope &) = delete;
    VectorReferenceScope &operator=(const VectorReferenceScope &) = delete;
};
}
