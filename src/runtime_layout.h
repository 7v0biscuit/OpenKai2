// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
namespace openkai2 {
std::size_t compiled_vector_interpreter_size() noexcept;
std::size_t compiled_runtime_interpreter_size() noexcept;
std::size_t compiled_runtime_size() noexcept;
void verify_runtime_layout(std::size_t interpreterSize,std::size_t runtimeSize);
void verify_vector_runtime();
void verify_vector_runtime_gpu();
}
