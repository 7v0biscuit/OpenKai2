// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>

namespace openkai2 {
// This controls host window pacing. Guest clocks retain their original rate.
unsigned parse_window_fps_limit(std::string_view value);
void set_window_fps_limit(unsigned limit);
unsigned window_fps_limit() noexcept;
}
