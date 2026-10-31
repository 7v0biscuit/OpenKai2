// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <vector>
namespace openkai2::gpu {
inline constexpr unsigned VramBytes=4u*1024u*1024u,PageBytes=8192u,PageCount=VramBytes/PageBytes;
inline constexpr unsigned CommandWords=96u,VertexWord=40u,VertexWords=16u,TileSize=8u;
using Command=std::array<uint32_t,CommandWords>;
const std::vector<uint32_t>& address_tables();
uint32_t bit_address(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y);
std::bitset<PageCount> rectangle_pages(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y,unsigned w,unsigned h);
void verify_address_tables();
}
