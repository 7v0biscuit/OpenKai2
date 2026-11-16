// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "runtime/gs/gs_backend.h"
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace openkai2 {

class AsyncRasterBackend final : public GSRasterBackend {
public:
    explicit AsyncRasterBackend(std::unique_ptr<GSRasterBackend> inner, size_t capacity = 2048);
    ~AsyncRasterBackend() override;

    void Initialize(uint8_t *vram, uint32_t vramSize) override;
    void Reset() override;
    void Submit(const GSPrimitiveBatch &batch) override;
    void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override;
    void BeginTransfer(const GSTransferCommand &command) override;
    void UploadImage(const uint8_t *data, uint32_t sizeBytes) override;
    void Flush() override;
    void TextureFlush() override;
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest &request) override;
    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override;
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t> &out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

    GSRasterBackend &inner() { return *inner_; }
    void drain() const;

private:
    enum class Kind : uint8_t { submit, clut, transfer, upload, flush, textureFlush, write };
    struct Op {
        Kind kind = Kind::flush;
        GSPrimitiveBatch batch{};
        GSTex0Reg tex0{};
        GSTexClutReg texclut{};
        GSTransferCommand transfer{};
        std::vector<uint8_t> bytes;
        uint32_t write[6]{};
    };
    Op &claim(std::unique_lock<std::mutex> &lock);
    void publish(std::unique_lock<std::mutex> &lock);
    void run();
    void execute(Op &op);

    std::unique_ptr<GSRasterBackend> inner_;
    std::vector<Op> ring_;
    mutable std::mutex mutex_;
    mutable std::condition_variable work_, done_;
    uint64_t pushed_ = 0, processed_ = 0;
    bool stopping_ = false, idle_ = false;
    std::exception_ptr failure_;
    std::thread worker_;
};

bool asynchronous_raster_enabled();
void set_asynchronous_raster(bool enabled);

}
