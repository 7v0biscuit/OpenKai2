// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Config.h"
#include "R5900.h"
#include "common/AlignedMalloc.h"
#include "common/Assertions.h"
#include "common/FPControl.h"
#include "common/Pcsx2Defs.h"
#include "common/VectorIntrin.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
inline constexpr int DMAC_VIF1 = 1;
void reportVectorError(const char *, ...) noexcept;
struct HostConsole {
    template <class... T> bool WriteLn(T &&...) const { return false; }
    template <class... T> bool Warning(T &&...) const { return false; }
    template <class... T> bool Error(const char *message, T &&...args) const {
        reportVectorError(message, args...);
        return false;
    }
    template <class... T> bool Status(T &&...) const { return false; }
};
inline HostConsole Console, DevCon, DbgCon;
using ConsoleColors = int;
inline constexpr int Color_Orange = 1, Color_Magenta = 2;
namespace SysMemory {
u8 *GetVU0Rec();
u8 *GetVU1Rec();
u8 *GetVU0RecEnd();
u8 *GetVU1RecEnd();
}
#define VUM_LOG(...) ((void)0)
#define CPU_LOG(...) ((void)0)
#define VIF_LOG(...) ((void)0)
inline constexpr int INTC_VU0 = 6, INTC_VU1 = 7;
void hwIntcIrq(int);
void CPU_INT(int, int);

inline constexpr int Color_Green = 3;
namespace EmuFolders {
inline std::string Logs = "/tmp";
}
