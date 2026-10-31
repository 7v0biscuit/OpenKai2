// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ps2x/iop/iop_types.h"
#include <memory>
#include <string>

namespace ps2x::iop { class IopHost; }
namespace openkai2 {
class IsoIndex;
class CdvdRpc {
    ps2x::iop::IopHost& host_;
    mutable std::unique_ptr<IsoIndex> index_;
    mutable std::string image_;
public:
    static constexpr uint32_t search_sid=0x80000597u;
    explicit CdvdRpc(ps2x::iop::IopHost& host);
    ~CdvdRpc();
    void reset();
    bool canBind(uint32_t sid) const noexcept;
    ps2x::iop::RpcResult handle(const ps2x::iop::RpcRequest& request);
};
}
