// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openkai2::crypto {

using Digest512 = std::array<uint8_t, 64>;
Digest512 sha512(const uint8_t *data, size_t size);

using Seed = std::array<uint8_t, 32>;
using PublicKey = std::array<uint8_t, 32>;
using Signature = std::array<uint8_t, 64>;

PublicKey public_key(const Seed &seed);
Signature sign(const Seed &seed, const uint8_t *message, size_t size);
bool verify(const PublicKey &key, const uint8_t *message, size_t size, const Signature &signature);

std::string to_hex(const uint8_t *data, size_t size);
bool from_hex(const std::string &text, uint8_t *out, size_t size);

}
