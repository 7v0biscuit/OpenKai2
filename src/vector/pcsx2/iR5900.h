// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "VUmicro.h"
#include "common/emitter/x86emitter.h"
#define RFASTMEMBASE x86Emitter::rbp
struct BridgeGprMap {
    int inuse = 0, needed = 0, counter = 0, type = 0, reg = 0, mode = 0, extra = 0;
};
inline BridgeGprMap x86regs[16];
using _xmmregs = BridgeGprMap;
inline _xmmregs xmmregs[16];
inline constexpr int X86TYPE_VIREG = 1, XMMTYPE_VFREG = 2, MODE_READ = 1, MODE_WRITE = 2,
                     MODE_COP2 = 4;
inline int _allocVFtoXMMreg(int, int) {
    throw std::runtime_error("EE macro JIT allocation is unavailable in the VU bridge");
}
inline int _allocX86reg(int, int, int) {
    throw std::runtime_error("EE macro JIT allocation is unavailable in the VU bridge");
}
inline void _freeXMMreg(int) {
    throw std::runtime_error("EE macro JIT allocation is unavailable in the VU bridge");
}
inline void _freeX86regWithoutWriteback(int) {
    throw std::runtime_error("EE macro JIT allocation is unavailable in the VU bridge");
}

inline int g_x86AllocCounter = 0;
