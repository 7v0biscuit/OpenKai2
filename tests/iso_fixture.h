// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace openkai2::test {
namespace fs=std::filesystem;
using Bytes=std::vector<uint8_t>;
inline constexpr size_t sector=2048;
inline void both16(Bytes& b, size_t p, uint16_t x) {
    b[p]=uint8_t(x); b[p+1]=uint8_t(x>>8); b[p+2]=uint8_t(x>>8); b[p+3]=uint8_t(x);
}
inline void both32(Bytes& b, size_t p, uint32_t x) {
    for (unsigned i=0;i<4;++i) { b[p+i]=uint8_t(x>>(8*i)); b[p+7-i]=uint8_t(x>>(8*i)); }
}
inline Bytes record(std::string name, uint32_t lba, uint32_t size, uint8_t flags=0) {
    Bytes r(33+name.size()+(name.size()%2==0 ? 1 : 0));
    r[0]=uint8_t(r.size()); both32(r,2,lba); both32(r,10,size);
    r[25]=flags; both16(r,28,1); r[32]=uint8_t(name.size());
    std::copy(name.begin(),name.end(),r.begin()+33);
    return r;
}
inline void place(Bytes& image, size_t pos, const Bytes& data) {
    std::copy(data.begin(),data.end(),image.begin()+pos);
}
inline void records(Bytes& image, size_t lba, const std::vector<Bytes>& entries) {
    size_t pos=lba*sector;
    for (const auto& entry:entries) { place(image,pos,entry); pos+=entry.size(); }
}
inline Bytes fixture() {
    Bytes image(32*sector);
    image[16*sector]=1; image[16*sector+6]=1;
    image[17*sector]=255; image[17*sector+6]=1;
    const std::string signature="CD001";
    std::copy(signature.begin(),signature.end(),image.begin()+16*sector+1);
    std::copy(signature.begin(),signature.end(),image.begin()+17*sector+1);
    both32(image,16*sector+80,32); both16(image,16*sector+120,1);
    both16(image,16*sector+124,1); both16(image,16*sector+128,sector);
    const auto root=record(std::string(1,'\0'),18,sector,2);
    place(image,16*sector+156,root);
    records(image,18,{root,record(std::string(1,'\1'),18,sector,2),
        record("SLUS_207.79;1",21,3),record("MODULES",19,sector,2),record("USR",20,sector,2)});
    records(image,19,{record(std::string(1,'\0'),19,sector,2),record(std::string(1,'\1'),18,sector,2),
        record("CRI_ADXI.IRX;1",22,52)});
    records(image,20,{record(std::string(1,'\0'),20,sector,2),record(std::string(1,'\1'),18,sector,2),
        record("DATA.BIN;1",23,6)});
    image[21*sector]='a'; image[21*sector+1]='b'; image[21*sector+2]='c';
    image[22*sector]=0x7f; image[22*sector+1]='E'; image[22*sector+2]='L'; image[22*sector+3]='F';
    const std::string data="abcdef";
    std::copy(data.begin(),data.end(),image.begin()+23*sector);
    return image;
}
inline void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    if (!out) throw std::runtime_error("Test fixture write failed");
}
inline std::string read(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
}
