// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <vector>

namespace openkai2::launcher {
std::vector<uint8_t> decode_progress_save(const std::vector<uint8_t> &encrypted);
std::vector<uint8_t> encode_progress_save(const std::vector<uint8_t> &decoded);
bool unlock_progress_content(std::vector<uint8_t> &decoded);
}
