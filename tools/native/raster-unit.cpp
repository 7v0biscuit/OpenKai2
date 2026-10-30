// SPDX-License-Identifier: GPL-3.0-only
// Compile swizzled texture addressing with the CPU renderer so common sampler
// taps can inline format selection, address calculation and cache resolution.
#include "ps2_gs_memory.cpp"
#include "runtime/gs/gs_cpu_backend.cpp"

#include "render/gpu_layout.h"
#include <bit>
#include <stdexcept>
namespace openkai2::gpu {
namespace {
template<GSMem::PixelStorageMode P,class Table>
void addAddressTable(std::vector<uint32_t>& out,const Table& table) {
    using Traits=GSMem::PixelStorageTraits<P>;
    const auto extent=Traits::PageExtent();const unsigned descriptor=unsigned(P)*8u;
    out[descriptor]=extent.x;out[descriptor+1u]=extent.y;
    out[descriptor+2u]=GSMem::UnpackedBitWidth(P);out[descriptor+3u]=GSMem::BitsPerPixel(P);
    out[descriptor+4u]=Traits::BitOffset();out[descriptor+5u]=unsigned(out.size());
    if(!std::has_single_bit(unsigned(extent.x)) || !std::has_single_bit(unsigned(extent.y)))
        throw std::runtime_error("GPU swizzle page dimensions must be powers of two");
    out[descriptor+6u]=std::countr_zero(unsigned(extent.x));
    out[descriptor+7u]=std::countr_zero(unsigned(extent.y));
    for(unsigned y=0;y<extent.y;++y)for(unsigned x=0;x<extent.x;++x)
        out.push_back(Traits::Address(table,0u,2u,x,y)*unsigned(GSMem::UnpackedBitWidth(P))+unsigned(Traits::BitOffset()));
}
}
const std::vector<uint32_t>& address_tables() {
    static const auto tables=[] {
        // Reuse the CPU constructor's existing one-time initialization guard;
        // never rewrite lookup tables while another renderer is reading them.
        GSCpuBackend bootstrap;
        std::vector<uint32_t> out(64u*8u);
#define ADD(name,table) addAddressTable<GSMem::name>(out,GSMem::table)
        ADD(C32,PageTableC32);ADD(C24,PageTableC32);ADD(C16,PageTableC16);ADD(C16S,PageTableC16S);
        ADD(P8,PageTableP8);ADD(P4,PageTableP4);ADD(P8H,PageTableC32);ADD(P4HL,PageTableC32);ADD(P4HH,PageTableC32);
        ADD(Z32,PageTableZ32);ADD(Z24,PageTableZ32);ADD(Z16,PageTableZ16);ADD(Z16S,PageTableZ16S);
#undef ADD
        return out;
    }();return tables;
}
uint32_t bit_address(unsigned psm,unsigned base,unsigned width,unsigned x,unsigned y) {
    const auto& lut=address_tables();const auto d=(psm&63u)*8u;
    const unsigned pw=lut[d],ph=lut[d+1u];if(!pw || !ph)throw std::runtime_error("Unsupported GPU pixel storage format");
    const unsigned page=(y>>lut[d+7u])*((width*64u)>>lut[d+6u])+(x>>lut[d+6u]);
    return (base*2048u+page*65536u+lut[lut[d+5u]+(y&(ph-1u))*pw+(x&(pw-1u))])&((VramBytes*8u)-1u);
}
void verify_address_tables() {
    using namespace GSMem;
    const auto check=[&]<PixelStorageMode P>(const auto& table){
        using T=PixelStorageTraits<P>;
        constexpr unsigned coordinates[]={0u,1u,7u,8u,15u,16u,31u,32u,63u,64u,127u,128u,255u,511u,1023u,2047u};
        for(unsigned base:{0u,1u,15u,31u,32u,33u,255u,16383u})for(unsigned width:{0u,1u,2u,3u,8u,16u,63u})
            for(auto y:coordinates)for(auto x:coordinates){
                const auto expected=(T::Address(table,base,width,x,y)*unsigned(UnpackedBitWidth(P))+unsigned(T::BitOffset()))&((VramBytes*8u)-1u);
                if(bit_address(unsigned(P),base,width,x,y)!=expected)throw std::runtime_error("GPU address table differs from the CPU swizzle reference");
            }
    };
#define CHECK(name,table) check.template operator()<name>(table)
    CHECK(C32,PageTableC32);CHECK(C24,PageTableC32);CHECK(C16,PageTableC16);CHECK(C16S,PageTableC16S);
    CHECK(P8,PageTableP8);CHECK(P4,PageTableP4);CHECK(P8H,PageTableC32);CHECK(P4HL,PageTableC32);CHECK(P4HH,PageTableC32);
    CHECK(Z32,PageTableZ32);CHECK(Z24,PageTableZ32);CHECK(Z16,PageTableZ16);CHECK(Z16S,PageTableZ16S);
#undef CHECK
}
}
