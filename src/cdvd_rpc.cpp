// SPDX-License-Identifier: GPL-3.0-only
#include "cdvd_rpc.h"
#include "disc_image.h"
#include "ps2x/iop/iop_host.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <sstream>

namespace openkai2 {
namespace {
uint32_t read32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | uint32_t(bytes[1])<<8 | uint32_t(bytes[2])<<16 | uint32_t(bytes[3])<<24;
}
void write32(uint8_t* bytes,uint32_t value) {
    for (unsigned i=0;i<4;++i) bytes[i]=uint8_t(value>>(8*i));
}
}
CdvdRpc::CdvdRpc(ps2x::iop::IopHost& host):host_(host) {}
CdvdRpc::~CdvdRpc()=default;
void CdvdRpc::reset() { index_.reset();image_.clear(); }

bool CdvdRpc::canBind(uint32_t sid) const noexcept {
    if (sid!=search_sid) return false;
    try {
        const auto image=host_.hostPath(ps2x::iop::HostPathKind::CdImage);
        if (image.empty() || !std::filesystem::is_regular_file(image)) { index_.reset();return false; }
        if (!index_ || image_!=image) {
            auto index=std::make_unique<IsoIndex>(image);
            image_=image;index_=std::move(index);
        }
        return true;
    } catch (...) { index_.reset();return false; }
}

ps2x::iop::RpcResult CdvdRpc::handle(const ps2x::iop::RpcRequest& request) {
    using namespace ps2x::iop;
    if (request.sid!=search_sid) return {};
    RpcResult result{};
    result.handled=true;
    result.resultAddress=request.receive.address;
    result.serverDispatchPolicy=ServerDispatchPolicy::Suppress;
    result.signalNowaitCompletion=true;
    result.signalCompletion=true;
    std::array<uint8_t,300> packet{};
    const bool layout=request.send.size==292u || request.send.size==296u || request.send.size==300u;
    const bool readable=layout && host_.readGuest(request.send.address,packet.data(),request.send.size);
    std::array<uint8_t,4> reply{};
    if (!request.receive.address || request.receive.size<4u
        || !host_.readGuest(request.receive.address,reply.data(),reply.size())) return result;
    reply.fill(0);
    auto fail=[&]() {
        host_.writeGuest(request.receive.address,reply.data(),reply.size());
        return result;
    };
    if (!readable || !canBind(request.sid)) return fail();
    const size_t metadata=request.send.size==292u ? 32u : 36u;
    const auto* name=packet.data()+metadata;
    const auto* end=std::find(name,name+256,uint8_t(0));
    if (end==name+256 || (request.send.size==300u && read32(packet.data()+296)!=0u)) return fail();
    const auto destination=read32(packet.data()+metadata+256u);
    std::array<uint8_t,36> descriptor{};
    if (!destination || !host_.readGuest(destination,descriptor.data(),metadata)) return fail();
    uint32_t normalizedDestination=0,normalizedReply=0;
    if (!host_.normalizeGuestAddress(destination,normalizedDestination)
        || !host_.normalizeGuestAddress(request.receive.address,normalizedReply)
        || (uint64_t(normalizedDestination)<uint64_t(normalizedReply)+4u
            && uint64_t(normalizedReply)<uint64_t(normalizedDestination)+metadata)) return fail();
    const auto path=std::string(reinterpret_cast<const char*>(name),size_t(end-name));
    const auto* entry=index_->find(path);
    if (!entry || entry->size>std::numeric_limits<uint32_t>::max()) {
        host_.log(LogLevel::Info,"[CDVD RPC] search not found: "+path);
        return fail();
    }
    descriptor.fill(0);
    write32(descriptor.data(),entry->sector);
    write32(descriptor.data()+4,uint32_t(entry->size));
    std::copy_n(entry->identifier.begin(),std::min<size_t>(16,entry->identifier.size()),descriptor.begin()+8);
    descriptor[24]=entry->flags;
    for (unsigned i=1;i<=5;++i) descriptor[24+i]=entry->recording_date[6-i];
    const auto year=uint16_t(1900u+entry->recording_date[0]);
    descriptor[30]=uint8_t(year);descriptor[31]=uint8_t(year>>8);
    if (metadata==36u) write32(descriptor.data()+32,entry->flags);
    if (!host_.writeGuest(destination,descriptor.data(),metadata)) return fail();
    write32(reply.data(),1);
    if (!host_.writeGuest(request.receive.address,reply.data(),reply.size())) return result;
    std::ostringstream message;
    message << "[CDVD RPC] search " << path << " sector=" << entry->sector << " size=" << entry->size;
    host_.log(LogLevel::Info,message.str());
    return result;
}
}
