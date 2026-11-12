// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "state_archive.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace openkai2 {
class DeltaStates : public StateBlocks {
  public:
    static constexpr size_t page = 4096;
    struct Stats {
        size_t pages = 0, small = 0, regions = 0;
        double compareMs = 0;
    };

    explicit DeltaStates(size_t capacity) : capacity_(capacity ? capacity : 1) {}

    template <typename Serialize>
    void capture(uint64_t id, Serialize &&serialize) {
        if (!frames_.empty() && id <= frames_.back().id)
            throw std::runtime_error("Rollback frames must be captured in order");
        Frame frame;
        frame.id = id;
        for (auto &region : regions_)
            region.present = false;
        current_ = &frame;
        mode_ = Mode::capture;
        StateArchive archive(frame.small);
        archive.blocks(this);
        try {
            serialize(archive);
        } catch (...) {
            mode_ = Mode::idle;
            current_ = nullptr;
            throw;
        }
        mode_ = Mode::idle;
        current_ = nullptr;
        stats_ = {frame.undo.size(), frame.small.size(), frame.order.size(), compareMs_};
        compareMs_ = 0;
        for (auto &region : regions_)
            region.synced = false;
        frames_.push_back(std::move(frame));
        while (frames_.size() > capacity_)
            frames_.pop_front();
        retire(frames_.front().id);
    }

    template <typename Serialize>
    bool restore(uint64_t id, Serialize &&serialize) {
        size_t at = frames_.size();
        for (size_t i = 0; i < frames_.size(); ++i)
            if (frames_[i].id == id)
                at = i;
        if (at == frames_.size())
            return false;
        for (auto &region : regions_) {
            region.touched.assign(pages(region.size), 0);
            region.synced = region.present && region.alive && region.fixed;
            if (region.synced)
                scan(region);
        }
        while (frames_.size() > at + 1) {
            const Frame &newer = frames_.back();
            for (size_t i = newer.undo.size(); i-- > 0;) {
                const Undo &undo = newer.undo[i];
                Region &region = regions_[undo.region];
                if (!region.alive)
                    continue;
                const size_t offset = size_t(undo.page) * page;
                std::memcpy(region.shadow.data() + offset, newer.pages.data() + i * page, span(region, undo.page));
                region.touched[undo.page] = 1;
            }
            frames_.pop_back();
        }
        for (auto &region : regions_) {
            if (region.alive && region.born > id) {
                drop(region);
                continue;
            }
            region.present = false;
        }
        Frame &frame = frames_.back();
        current_ = &frame;
        cursor_ = 0;
        mode_ = Mode::restore;
        StateArchive archive(static_cast<const std::vector<uint8_t> &>(frame.small));
        archive.blocks(this);
        try {
            serialize(archive);
        } catch (...) {
            mode_ = Mode::idle;
            current_ = nullptr;
            throw;
        }
        const bool complete = archive.finished() && cursor_ == frame.order.size();
        mode_ = Mode::idle;
        current_ = nullptr;
        if (!complete)
            throw std::runtime_error("Rollback state does not match the running game");
        return true;
    }

    void clear() {
        frames_.clear();
        regions_.clear();
        byPointer_.clear();
    }
    bool holds(uint64_t id) const {
        for (const auto &frame : frames_)
            if (frame.id == id)
                return true;
        return false;
    }
    size_t size() const { return frames_.size(); }
    uint64_t oldest() const { return frames_.empty() ? 0 : frames_.front().id; }
    uint64_t newest() const { return frames_.empty() ? 0 : frames_.back().id; }
    Stats stats() const { return stats_; }

    bool block(void *data, size_t size, bool loading, bool fixed) override {
        if (mode_ == Mode::capture && !loading)
            return captured(static_cast<uint8_t *>(data), size, fixed);
        if (mode_ == Mode::restore && loading)
            return restored(static_cast<uint8_t *>(data), size);
        return false;
    }

  private:
    enum class Mode { idle, capture, restore };
    static constexpr uint32_t inlineBlock = 0xffffffffu;
    struct Region {
        uint8_t *base = nullptr;
        size_t size = 0;
        uint64_t born = 0, seen = 0;
        bool alive = false, present = false, synced = false, fixed = false;
        std::vector<uint8_t> shadow, touched;
    };
    struct Undo {
        uint32_t region, page;
    };
    struct Frame {
        uint64_t id = 0;
        std::vector<uint8_t> small, pages;
        std::vector<Undo> undo;
        std::vector<uint32_t> order;
    };

    static size_t pages(size_t size) { return (size + page - 1) / page; }
    static size_t span(const Region &region, size_t index) { return std::min(page, region.size - index * page); }

    bool captured(uint8_t *data, size_t size, bool fixed) {
        struct Timer {
            double &total;
            std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
            ~Timer() { total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(); }
        } timer{compareMs_};
        uint32_t index = 0;
        const auto found = byPointer_.find(data);
        if (found != byPointer_.end() && regions_[found->second].size == size && !regions_[found->second].present &&
            regions_[found->second].fixed == fixed) {
            index = found->second;
            Region &region = regions_[index];
            const size_t count = pages(size);
            for (size_t i = 0; i < count; ++i) {
                const size_t offset = i * page, length = span(region, i);
                if (std::memcmp(data + offset, region.shadow.data() + offset, length) == 0)
                    continue;
                const size_t at = current_->pages.size();
                current_->pages.resize(at + page);
                std::memcpy(current_->pages.data() + at, region.shadow.data() + offset, length);
                current_->undo.push_back({index, uint32_t(i)});
                std::memcpy(region.shadow.data() + offset, data + offset, length);
            }
        } else {
            if (found != byPointer_.end() && regions_[found->second].present) {
                current_->order.push_back(inlineBlock);
                return false;
            }
            if (found != byPointer_.end())
                detach(regions_[found->second]);
            index = uint32_t(regions_.size());
            regions_.emplace_back();
            Region &region = regions_.back();
            region.base = data;
            region.size = size;
            region.born = current_->id;
            region.alive = true;
            region.fixed = fixed;
            region.shadow.assign(data, data + size);
            byPointer_[data] = index;
        }
        regions_[index].present = true;
        regions_[index].seen = current_->id;
        current_->order.push_back(index);
        return true;
    }

    bool restored(uint8_t *data, size_t size) {
        if (cursor_ >= current_->order.size())
            throw std::runtime_error("Rollback state has an unexpected memory block");
        const uint32_t index = current_->order[cursor_++];
        if (index == inlineBlock)
            return false;
        Region &region = regions_[index];
        if (!region.alive || region.size != size)
            throw std::runtime_error("Rollback state memory block changed size");
        if (data == region.base && region.synced) {
            for (size_t i = 0; i < region.touched.size(); ++i)
                if (region.touched[i])
                    std::memcpy(data + i * page, region.shadow.data() + i * page, span(region, i));
        } else {
            if (const auto old = byPointer_.find(region.base); old != byPointer_.end() &&
                                                                 &regions_[old->second] == &region)
                byPointer_.erase(old);
            if (const auto taken = byPointer_.find(data); taken != byPointer_.end())
                detach(regions_[taken->second]);
            std::memcpy(data, region.shadow.data(), size);
            region.base = data;
            byPointer_[data] = uint32_t(&region - regions_.data());
        }
        region.present = true;
        region.synced = false;
        region.seen = current_->id;
        std::fill(region.touched.begin(), region.touched.end(), uint8_t(0));
        return true;
    }

    void scan(Region &region) {
        for (size_t i = 0; i < region.touched.size(); ++i)
            if (std::memcmp(region.base + i * page, region.shadow.data() + i * page, span(region, i)) != 0)
                region.touched[i] = 1;
    }

    void detach(Region &region) {
        if (const auto found = byPointer_.find(region.base); found != byPointer_.end() && &regions_[found->second] == &region)
            byPointer_.erase(found);
        region.base = nullptr;
        region.present = region.synced = false;
    }

    void drop(Region &region) {
        if (const auto found = byPointer_.find(region.base); found != byPointer_.end() && &regions_[found->second] == &region)
            byPointer_.erase(found);
        region.alive = region.present = false;
        region.base = nullptr;
        region.shadow.clear();
        region.shadow.shrink_to_fit();
        region.touched.clear();
    }

    void retire(uint64_t oldestKept) {
        for (auto &region : regions_)
            if (region.alive && !region.present && region.seen < oldestKept)
                drop(region);
    }

    size_t capacity_;
    std::deque<Frame> frames_;
    std::vector<Region> regions_;
    std::unordered_map<uint8_t *, uint32_t> byPointer_;
    Frame *current_ = nullptr;
    size_t cursor_ = 0;
    Mode mode_ = Mode::idle;
    Stats stats_;
    double compareMs_ = 0;
};
}
