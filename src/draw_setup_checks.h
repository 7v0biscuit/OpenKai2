// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
class PS2Runtime;
namespace openkai2 {
void verify_draw_setup(PS2Runtime&,uint8_t*);
void verify_versus_animation(PS2Runtime&,uint8_t*);
void verify_battle_initialization(PS2Runtime&,uint8_t*);
}
