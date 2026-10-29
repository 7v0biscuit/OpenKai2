// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
class PS2Runtime;
namespace openkai2 {
void verify_ee_signed_branch_math(PS2Runtime &runtime, uint8_t *ram);
}
