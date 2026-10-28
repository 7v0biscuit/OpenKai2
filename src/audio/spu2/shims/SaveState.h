// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/Pcsx2Types.h"
enum class FreezeAction { Load, Save, Size };
struct freezeData { int size; u8 *data; };
