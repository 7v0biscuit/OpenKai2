// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace openkai2 {
struct VectorWorkResult { uint64_t hash,cycles,packets;double nsPerRun; };
VectorWorkResult vector_workload(unsigned mode,unsigned iterations);
void verify_vector_execution();
void verify_vector_rounding();
}
