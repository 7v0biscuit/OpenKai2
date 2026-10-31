// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/vu/ps2_vu1_core.cpp"
#include "runtime/vu/ps2_vu1_upper.cpp"
#include "ps2_vu1_lower.cpp"

#include "runtime_layout.h"
namespace openkai2 {
std::size_t compiled_vector_interpreter_size() noexcept {return sizeof(VU1Interpreter);}
}
