// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
class PS2Runtime;
namespace openkai2 {
void verify_battle_updates(PS2Runtime&,uint8_t*);
void verify_battle_draws(PS2Runtime&,uint8_t*);
}
