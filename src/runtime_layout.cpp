// SPDX-License-Identifier: GPL-3.0-only
// Include the enclosing runtime first, matching the production runtime unit.
#include "ps2_runtime.h"
#include "runtime_layout.h"
#include <stdexcept>
namespace openkai2 {
std::size_t compiled_runtime_interpreter_size() noexcept {return sizeof(VU1Interpreter);}
std::size_t compiled_runtime_size() noexcept {return sizeof(PS2Runtime);}
void verify_runtime_layout(std::size_t interpreterSize,std::size_t runtimeSize) {
    if(interpreterSize!=compiled_vector_interpreter_size()
        || interpreterSize!=compiled_runtime_interpreter_size()
        || runtimeSize!=compiled_runtime_size())
        throw std::runtime_error("Runtime components have incompatible layouts. Install a complete current release or rebuild all targets.");
}
}
