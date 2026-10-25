// SPDX-License-Identifier: GPL-3.0-only
// Compile swizzled texture addressing with the CPU renderer so common sampler
// taps can inline format selection, address calculation and cache resolution.
#include "ps2_gs_memory.cpp"
#include "runtime/gs/gs_cpu_backend.cpp"
