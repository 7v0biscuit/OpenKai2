// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
class PS2Runtime;
namespace openkai2 {
void verify_original_save_codec(PS2Runtime &, uint8_t *);
}
