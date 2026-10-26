// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common/Pcsx2Defs.h"
inline constexpr int GIF_PATH_1 = 0, GIF_TRANS_XGKICK = 0;
struct BridgeGifPath {
    void CopyGSPacketData(const u8 *, u32, bool);
};
struct BridgeGif {
    BridgeGifPath gifPath[3];
    u32 GetGSPacketSize(int, const u8 *, u32, u32, bool);
    u32 TransferGSPacketData(int, const u8 *, u32, bool);
};
inline BridgeGif gifUnit;
