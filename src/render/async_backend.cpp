// SPDX-License-Identifier: GPL-3.0-only
#include "async_backend.h"
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace openkai2 {

namespace {
std::atomic<bool> asynchronousRaster{false};
}

void set_asynchronous_raster(bool enabled) {
    const char *value = std::getenv("OPENKAI2_SYNC_RASTER");
    asynchronousRaster.store(enabled && !(value && *value == '1'), std::memory_order_relaxed);
}

bool asynchronous_raster_enabled() { return asynchronousRaster.load(std::memory_order_relaxed); }

AsyncRasterBackend::AsyncRasterBackend(std::unique_ptr<GSRasterBackend> inner, size_t capacity)
    : inner_(std::move(inner)), ring_(capacity < 2 ? 2 : capacity) {
    worker_ = std::thread([this] { run(); });
}

AsyncRasterBackend::~AsyncRasterBackend() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    work_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

AsyncRasterBackend::Op &AsyncRasterBackend::claim(std::unique_lock<std::mutex> &lock) {
    done_.wait(lock, [&] { return pushed_ - processed_ < ring_.size() || failure_; });
    if (failure_)
        std::rethrow_exception(failure_);
    return ring_[size_t(pushed_ % ring_.size())];
}

void AsyncRasterBackend::publish(std::unique_lock<std::mutex> &lock, bool urgent) {
    ++pushed_;
    const bool wake = idle_ && (urgent || pushed_ - processed_ >= 32u);
    lock.unlock();
    if (wake)
        work_.notify_one();
}

void AsyncRasterBackend::drain() const {
    std::unique_lock lock(mutex_);
    const uint64_t target = pushed_;
    if (idle_ && processed_ < target)
        work_.notify_one();
    done_.wait(lock, [&] { return processed_ >= target || failure_; });
    if (failure_)
        std::rethrow_exception(failure_);
}

void AsyncRasterBackend::run() {
    std::unique_lock lock(mutex_);
    for (;;) {
        idle_ = true;
        work_.wait(lock, [&] { return stopping_ || pushed_ != processed_; });
        idle_ = false;
        if (pushed_ == processed_ && stopping_)
            return;
        const uint64_t first = processed_, last = pushed_;
        lock.unlock();
        std::exception_ptr error;
        uint64_t finished = first;
        try {
            for (; finished < last; ++finished)
                execute(ring_[size_t(finished % ring_.size())]);
        } catch (...) {
            error = std::current_exception();
        }
        lock.lock();
        processed_ = error ? last : finished;
        if (error && !failure_)
            failure_ = error;
        done_.notify_all();
    }
}

void AsyncRasterBackend::execute(Op &op) {
    switch (op.kind) {
    case Kind::submit:
        inner_->Submit(op.batch);
        break;
    case Kind::clut:
        inner_->LoadClut(op.tex0, op.texclut);
        break;
    case Kind::transfer:
        inner_->BeginTransfer(op.transfer);
        break;
    case Kind::upload:
        inner_->UploadImage(op.bytes.data(), uint32_t(op.bytes.size()));
        if (op.bytes.capacity() > (256u << 10))
            std::vector<uint8_t>().swap(op.bytes);
        break;
    case Kind::flush:
        inner_->Flush();
        break;
    case Kind::textureFlush:
        inner_->TextureFlush();
        break;
    case Kind::write:
        inner_->WriteVram(op.write[0], op.write[1], op.write[2], op.write[3], op.write[4], op.write[5]);
        break;
    case Kind::sync:
        inner_->Sync(op.reason);
        break;
    case Kind::clear:
        inner_->ClearFramebuffer(op.context, op.write[0]);
        break;
    }
}

void AsyncRasterBackend::Initialize(uint8_t *vram, uint32_t vramSize) {
    drain();
    inner_->Initialize(vram, vramSize);
}

void AsyncRasterBackend::Reset() {
    drain();
    inner_->Reset();
}

void AsyncRasterBackend::Submit(const GSPrimitiveBatch &batch) {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::submit;
    op.batch = batch;
    publish(lock);
}

void AsyncRasterBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::clut;
    op.tex0 = tex0;
    op.texclut = texclut;
    publish(lock);
}

void AsyncRasterBackend::BeginTransfer(const GSTransferCommand &command) {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::transfer;
    op.transfer = command;
    publish(lock);
}

void AsyncRasterBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes) {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::upload;
    if (data && sizeBytes)
        op.bytes.assign(data, data + sizeBytes);
    else
        op.bytes.clear();
    publish(lock);
}

void AsyncRasterBackend::Flush() {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::flush;
    publish(lock, true);
}

void AsyncRasterBackend::TextureFlush() {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::textureFlush;
    publish(lock);
}

void AsyncRasterBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::write;
    const uint32_t values[6] = {psm, base, bw, x, y, value};
    std::memcpy(op.write, values, sizeof(values));
    publish(lock);
}

void AsyncRasterBackend::Sync(GSSyncReason reason) {
    if (reason == GSSyncReason::Finish) {
        std::unique_lock lock(mutex_);
        auto &op = claim(lock);
        op.kind = Kind::sync;
        op.reason = reason;
        publish(lock, true);
        return;
    }
    drain();
    inner_->Sync(reason);
}

PresentationFrame AsyncRasterBackend::Present(const GSPresentationRequest &request) {
    drain();
    return inner_->Present(request);
}

bool AsyncRasterBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba) {
    std::unique_lock lock(mutex_);
    auto &op = claim(lock);
    op.kind = Kind::clear;
    op.context = context;
    op.write[0] = rgba;
    publish(lock, true);
    return true;
}

uint32_t AsyncRasterBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) {
    drain();
    return inner_->ConsumeLocalToHostBytes(dst, maxBytes);
}

uint32_t AsyncRasterBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const {
    drain();
    return inner_->ReadVram(psm, base, bw, x, y);
}

void AsyncRasterBackend::SnapshotVram(std::vector<uint8_t> &out) const {
    drain();
    inner_->SnapshotVram(out);
}

GSTransferSnapshot AsyncRasterBackend::GetTransferSnapshot() const { return inner_->GetTransferSnapshot(); }

}
