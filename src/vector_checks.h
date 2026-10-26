// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace openkai2 {
void verify_vector_zero_register();
struct VectorWorkResult { uint64_t hash,cycles,packets;double nsPerRun; };
VectorWorkResult vector_workload(unsigned mode,unsigned iterations);
VectorWorkResult vector_pipeline_workload(unsigned seed,unsigned budget,unsigned iterations);
VectorWorkResult vector_transfer_workload(unsigned seed,unsigned budget,unsigned iterations);
void verify_vector_transfers();
void verify_vector_pipeline_scheduling();
void verify_vector_execution();
void verify_vector_rounding();
}
