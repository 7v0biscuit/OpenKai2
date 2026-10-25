// SPDX-License-Identifier: GPL-3.0-only
// Compile the vector pipelines together so operand normalization, flag updates,
// and scheduler operations can be inlined across instruction handlers.
#include "runtime/vu/ps2_vu1_core.cpp"
#include "ps2_vu1_upper.cpp"
#include "ps2_vu1_lower.cpp"

#include "runtime_layout.h"
namespace openkai2 {
std::size_t compiled_vector_interpreter_size() noexcept {return sizeof(VU1Interpreter);}
}
