// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <vector>

namespace openkai2::launcher {
// USA Budokai 2's SPF save envelope. Both checksum layers are checked before
// decoding; encoding preserves its seed and all payload bytes not edited.
std::vector<uint8_t> decode_progress_save(const std::vector<uint8_t> &encrypted);
std::vector<uint8_t> encode_progress_save(const std::vector<uint8_t> &decoded);
// Unlock the character and stage capsules and their corresponding flags.
// Returns false if every supported unlock was already present.
bool unlock_progress_content(std::vector<uint8_t> &decoded);
} // namespace openkai2::launcher
