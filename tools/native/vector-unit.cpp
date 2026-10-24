// SPDX-License-Identifier: GPL-3.0-only
// Compile the vector pipelines together so operand normalization, flag updates,
// and scheduler operations can be inlined across instruction handlers.
#include "runtime/vu/ps2_vu1_core.cpp"
#include "ps2_vu1_upper.cpp"
#include "ps2_vu1_lower.cpp"
