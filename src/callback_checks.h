// SPDX-License-Identifier: GPL-3.0-only
#pragma once
class PS2Runtime;
namespace openkai2 {
void verify_callback_stacks();
void verify_callback_resume(PS2Runtime&,unsigned callback_address=0);
}
