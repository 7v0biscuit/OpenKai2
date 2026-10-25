// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace openkai2 {
struct HotpathResult {std::uint64_t hash,operations;double nanosecondsPerOperation;};
HotpathResult operand_workload(unsigned group,unsigned iterations);
HotpathResult product_workload(unsigned group,unsigned iterations);
void verify_vector_products();
void verify_vector_operands();
HotpathResult arithmetic_workload(unsigned group,unsigned iterations);
HotpathResult unpack_workload(unsigned group,unsigned iterations);
HotpathResult unpack_stream_workload(unsigned group,unsigned iterations);
void verify_vector_arithmetic();
void verify_vif_unpack();
}
