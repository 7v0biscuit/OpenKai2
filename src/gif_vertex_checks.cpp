// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_checks.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_backend.h"
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
namespace openkai2 {
namespace {
struct VertexCapture final : GSRasterBackend {
    std::vector<GSPrimitiveBatch> batches;
    void Initialize(uint8_t*,uint32_t) override {}
    void Reset() override {batches.clear();}
    void Submit(const GSPrimitiveBatch& b) override {batches.push_back(b);}
    void LoadClut(const GSTex0Reg&,const GSTexClutReg&) override {}
    void BeginTransfer(const GSTransferCommand&) override {throw std::runtime_error("Unexpected GIF fixture transfer");}
    void UploadImage(const uint8_t*,uint32_t) override {throw std::runtime_error("Unexpected GIF fixture upload");}
    void Flush() override {}
    void TextureFlush() override {}
    void Sync(GSSyncReason) override {}
    PresentationFrame Present(const GSPresentationRequest&) override {return {};}
    bool ClearFramebuffer(const GSContext&,uint32_t) override {return false;}
    uint32_t ConsumeLocalToHostBytes(uint8_t*,uint32_t) override {return 0;}
    uint32_t ReadVram(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) const override {return 0;}
    void WriteVram(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) override {}
    void SnapshotVram(std::vector<uint8_t>& out) const override {out.clear();}
    GSTransferSnapshot GetTransferSnapshot() const override {return {};}
};
uint32_t bits(float value){uint32_t out;std::memcpy(&out,&value,4u);return out;}
struct Register {unsigned descriptor;uint64_t lo,hi;};
std::vector<uint64_t> packet(std::initializer_list<Register> regs) {
    uint64_t descriptors=0;unsigned i=0;for(const auto& r:regs)descriptors|=uint64_t(r.descriptor)<<(i++*4u);
    std::vector<uint64_t> data={1u|0x8000u|(uint64_t(regs.size())<<60u),descriptors};
    for(const auto& r:regs){data.push_back(r.lo);data.push_back(r.hi);}return data;
}
}
void verify_gif_vertices() {
    auto require=[](bool good,const char* why){if(!good)throw std::runtime_error(why);};
    for(bool native:{false,true}) {
        std::vector<uint8_t> vram(4u*1024u*1024u);GS gs;gs.init(vram.data(),unsigned(vram.size()));
        auto capture=std::make_unique<VertexCapture>();auto* out=capture.get();gs.setRasterBackend(std::move(capture));
        auto send=[&](const std::vector<uint64_t>& data){const auto* bytes=reinterpret_cast<const uint8_t*>(data.data());
            if(native)require(gs.processNativePackedGIFPacket(bytes,unsigned(data.size()*8u)),"Native GIF fixture was rejected");
            else gs.processGIFPacket(bytes,unsigned(data.size()*8u));};
        auto last=[&]() -> const GSVertex& {require(!out->batches.empty(),"GIF fixture emitted no vertex");return out->batches.back().vertices[0];};
        const Register vertex{5u,64u|(128ull<<32u),123u};
        const Register color{1u,71u|(97ull<<32u),131u|(128ull<<32u)};
        const Register stq{2u,uint64_t(bits(.75f))|(uint64_t(bits(.5f))<<32u),bits(3.0f)};
        gs.writeRegister(0u,0u);gs.writeRegister(1u,0x80836147u|(uint64_t(bits(2.0f))<<32u));
        send(packet({vertex}));require(last().q==2.0f,"Packed GIF reset the latched vertex Q at a tag boundary");
        send(packet({stq,vertex}));require(last().q==2.0f && last().s==.75f && last().t==.5f,"STQ changed vertex Q before RGBA latched it");
        send(packet({stq,color,vertex}));require(last().q==3.0f,"Packed RGBA did not latch the preceding STQ Q");
        send(packet({vertex}));require(last().q==3.0f,"A later packed tag discarded the previous RGBAQ Q");
        send(packet({color,stq,vertex}));require(last().q==1.0f,"STQ after RGBA changed already-latched Q");
        auto chained=packet({stq,color,vertex});const auto tail=packet({vertex});chained.insert(chained.end(),tail.begin(),tail.end());
        send(chained);require(last().q==3.0f,"Chained tags discarded the latched Q");
        send(packet({color,vertex}));require(last().q==1.0f,"Packed Q temporary was not reset at the next tag");
        send(packet({{14u,0x80836147u|(uint64_t(bits(5.0f))<<32u),1u},stq,vertex}));
        require(last().q==5.0f,"Packed AD RGBAQ did not retain its explicit Q across STQ");
        if(!native){
            // REGLIST RGBAQ supplies its own Q; ST has no Q component.
            send({1u|0x8000u|(1ull<<58u)|(1ull<<60u),1u,0x80836147u|(uint64_t(bits(4.0f))<<32u),0u});
            send({1u|0x8000u|(1ull<<58u)|(1ull<<60u),2u,uint64_t(bits(.25f))|(uint64_t(bits(.125f))<<32u),0u});
            send(packet({vertex}));require(last().q==4.0f && last().s==.25f && last().t==.125f,"REGLIST ST or a tag boundary changed explicit RGBAQ Q");
        }
        gs.reset();gs.writeRegister(0u,0u);send(packet({vertex}));require(last().q==1.0f,"GS reset did not restore vertex Q");
        for(uint32_t depth:{0u,0x00ffffffu,0x01000001u,0x7fffffffu,0xfffffffeu,0xffffffffu}) {
            gs.writeRegister(0u,0u);send(packet({{5u,64u|(128ull<<32u),depth}}));
            require(last().z==double(depth),"Packed XYZ2 lost 32-bit vertex depth precision");
            gs.writeRegister(0u,3u);send(packet({{13u,64u|(128ull<<32u),depth},{13u,128u|(128ull<<32u),depth},{5u,64u|(192ull<<32u),depth}}));
            const auto& b=out->batches.back();require(b.vertexCount==3u,"Packed XYZ3 fixture lost triangle assembly");
            for(const auto& v:b.vertices)require(v.z==double(depth),"Packed XYZ3 lost 32-bit vertex depth precision");
        }
    }
}
}
