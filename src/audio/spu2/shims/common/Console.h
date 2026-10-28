// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// SPU2 diagnostics are compiled out; console messages are discarded.
struct Spu2Console {
    template <typename... Arguments> void WriteLn(Arguments &&...) const {}
    template <typename... Arguments> void Warning(Arguments &&...) const {}
    template <typename... Arguments> void Error(Arguments &&...) const {}
};
inline constexpr Spu2Console Console{}, DevCon{};
