// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/FPControl.h"
struct BridgeConfig {
    struct {
        FPControlRegister FPUFPCR{0x1f80}, VU0FPCR{0xffc0}, VU1FPCR{0xffc0};
    } Cpu;
    struct {
        bool FullVU0SyncHack = false, IbitHack = false, VUSyncHack = false;
    } Gamefixes;
    struct {
        int EECycleRate = 0, EECycleSkip = 0;
        bool vuFlagHack = false;
    } Speedhacks;
};
inline BridgeConfig EmuConfig;
#define THREAD_VU1 false
#define CHECK_FASTMEM false
#define CHECK_VUADDSUBHACK false
#define CHECK_XGKICKHACK false
#define CHECK_VUOVERFLOWHACK false
#define CHECK_VU_OVERFLOW(unit) true
#define CHECK_VU_EXTRA_OVERFLOW(unit) false
#define CHECK_VU_SIGN_OVERFLOW(unit) false
#define CHECK_VU_UNDERFLOW(unit) true

#define INSTANT_VU1 false
