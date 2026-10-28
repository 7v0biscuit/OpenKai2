// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// One instruction set: the reverb resamplers live in a single namespace.
#define MULTI_ISA_UNSHARED_START namespace isa_native {
#define MULTI_ISA_UNSHARED_END }
#define MULTI_ISA_DEF(...) namespace isa_native { __VA_ARGS__ }
#define MULTI_ISA_SELECT(name) isa_native::name
