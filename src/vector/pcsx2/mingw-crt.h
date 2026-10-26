// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Read the CRT's inline functions before PCSX2 defines its internal force-inline
// macro. This keeps MinGW secure string helpers from becoming external definitions.
#include <cstring>
#include <cwchar>
#include <windows.h>

#ifdef __forceinline
#undef __forceinline
#endif
