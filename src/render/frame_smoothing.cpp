// SPDX-License-Identifier: GPL-3.0-only
#include "frame_smoothing.h"
#include "gpu_backend.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace openkai2 {
namespace {
using Clock = std::chrono::steady_clock;
constexpr float jump_limit = 160.0f;
constexpr double cut_share = 0.3;
constexpr size_t record_limit = 1u << 21u;
constexpr size_t backlog_limit = 30u;

struct Picture {
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0, displayFbp = 0, sourceFbp = 0;
    bool usedPreferred = false;
    Clock::time_point due{};
    uint64_t order = 0;
};

struct Schedule {
    std::mutex mutex;
    std::deque<Picture> pending;
    Picture current;
    bool haveCurrent = false, enabled = false;
    uint64_t generation = 0, shownOrder = 0;
    std::atomic<double> refresh{0.0};
    std::atomic<unsigned> perFrame{0};
};
Schedule &schedule() {
    static Schedule instance;
    return instance;
}

Picture packed(const PresentationFrame &frame) {
    Picture picture;
    picture.width = frame.width;
    picture.height = frame.height;
    picture.displayFbp = frame.displayFbp;
    picture.sourceFbp = frame.sourceFbp;
    picture.usedPreferred = frame.usedPreferred;
    const size_t row = size_t(frame.width) * 4u, stride = size_t(frame.stride ? frame.stride : frame.width) * 4u;
    picture.pixels.resize(row * frame.height);
    for (uint32_t y = 0; y < frame.height; ++y) {
        if (size_t(y) * stride + row > frame.pixels.size()) {
            picture.pixels.clear();
            break;
        }
        std::memcpy(picture.pixels.data() + size_t(y) * row, frame.pixels.data() + size_t(y) * stride, row);
    }
    return picture;
}

const char *dump_folder() {
    static const char *folder = std::getenv("OPENKAI2_SMOOTHING_DUMP");
    return folder && *folder ? folder : nullptr;
}

void dump(const Picture &picture) {
    if (!dump_folder() || picture.pixels.empty())
        return;
    std::ofstream out(std::string(dump_folder()) + "/" + std::to_string(picture.order) + ".ppm", std::ios::binary);
    out << "P6\n" << picture.width << ' ' << picture.height << "\n255\n";
    for (size_t i = 0; i + 3 < picture.pixels.size(); i += 4)
        out.write(reinterpret_cast<const char *>(picture.pixels.data() + i), 3);
}

void publish(Picture &&picture) {
    if (picture.pixels.empty())
        return;
    auto &s = schedule();
    std::lock_guard lock(s.mutex);
    if (!s.enabled || picture.order <= s.shownOrder)
        return;
    const auto at = std::upper_bound(s.pending.begin(), s.pending.end(), picture.order,
                                     [](uint64_t order, const Picture &p) { return order < p.order; });
    s.pending.insert(at, std::move(picture));
}

unsigned pictures_for(double hertz) {
    if (hertz <= 0.0)
        return 0u;
    return unsigned(std::clamp(std::lround(hertz / 59.94) - 1l, 0l, 3l));
}

enum class Kind : uint8_t { submit, clut, transfer, upload, flush, textureFlush, clear, write, reset };
struct Op {
    Kind kind;
    uint32_t index;
};
struct Record {
    std::vector<Op> ops;
    std::vector<GSPrimitiveBatch> batches;
    std::vector<std::pair<GSTex0Reg, GSTexClutReg>> cluts;
    std::vector<GSTransferCommand> transfers;
    std::vector<std::vector<uint8_t>> uploads;
    std::vector<std::pair<GSContext, uint32_t>> clears;
    std::vector<std::array<uint32_t, 6>> writes;
    GSPresentationRequest request{};
    bool present = false;
    uint64_t frame = 0;
    Clock::time_point finished{};
    double period = 1.0 / 59.94;
    void clear() { *this = Record{}; }
    size_t size() const { return ops.size(); }
};

struct Job {
    bool initialize = false;
    std::vector<uint8_t> memory;
    Record record;
};

class SmoothingBackend final : public GSRasterBackend {
    using Match = smoothing::Match;

  public:
    SmoothingBackend(RasterFactory factory, FrameSmoothing mode)
        : factory_(std::move(factory)), main_(factory_()), predict_(mode == FrameSmoothing::low_latency),
          worker_([this] { run(); }) {
        auto &s = schedule();
        std::lock_guard lock(s.mutex);
        s.enabled = true;
        s.pending.clear();
        s.haveCurrent = false;
        s.generation = 0;
        s.shownOrder = 0;
    }
    ~SmoothingBackend() override {
        {
            std::lock_guard lock(jobMutex_);
            stopping_ = true;
        }
        jobReady_.notify_all();
        if (worker_.joinable())
            worker_.join();
        auto &s = schedule();
        std::lock_guard lock(s.mutex);
        s.enabled = false;
        s.pending.clear();
        s.haveCurrent = false;
        s.generation = 0;
    }

    void Initialize(uint8_t *vram, uint32_t size) override {
        std::lock_guard lock(recordMutex_);
        main_->Initialize(vram, size);
        if (!recording())
            return;
        record_.clear();
        Job job;
        job.initialize = true;
        job.memory.assign(vram, vram + size);
        post(std::move(job));
    }
    void Reset() override {
        std::lock_guard lock(recordMutex_);
        main_->Reset();
        add(Kind::reset, 0u);
    }
    void Submit(const GSPrimitiveBatch &batch) override {
        std::lock_guard lock(recordMutex_);
        main_->Submit(batch);
        if (!recording())
            return;
        record_.ops.push_back({Kind::submit, uint32_t(record_.batches.size())});
        record_.batches.push_back(batch);
        spill();
    }
    void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override {
        std::lock_guard lock(recordMutex_);
        main_->LoadClut(tex0, texclut);
        if (!recording())
            return;
        record_.ops.push_back({Kind::clut, uint32_t(record_.cluts.size())});
        record_.cluts.emplace_back(tex0, texclut);
    }
    void BeginTransfer(const GSTransferCommand &command) override {
        std::lock_guard lock(recordMutex_);
        main_->BeginTransfer(command);
        if (!recording() || command.direction == 1u)
            return;
        record_.ops.push_back({Kind::transfer, uint32_t(record_.transfers.size())});
        record_.transfers.push_back(command);
    }
    void UploadImage(const uint8_t *data, uint32_t sizeBytes) override {
        std::lock_guard lock(recordMutex_);
        main_->UploadImage(data, sizeBytes);
        if (!recording())
            return;
        record_.ops.push_back({Kind::upload, uint32_t(record_.uploads.size())});
        record_.uploads.emplace_back(data, data + sizeBytes);
    }
    void Flush() override {
        std::lock_guard lock(recordMutex_);
        main_->Flush();
        add(Kind::flush, 0u);
    }
    void TextureFlush() override {
        std::lock_guard lock(recordMutex_);
        main_->TextureFlush();
        add(Kind::textureFlush, 0u);
    }
    void Sync(GSSyncReason reason) override { main_->Sync(reason); }
    PresentationFrame Present(const GSPresentationRequest &request) override {
        std::lock_guard lock(recordMutex_);
        PresentationFrame frame = main_->Present(request);
        const auto now = Clock::now();
        if (!recording())
            return frame;
        if (lastPresent_ != Clock::time_point{}) {
            const double seconds = std::chrono::duration<double>(now - lastPresent_).count();
            if (seconds > 1.0 / 90.0 && seconds < 1.0 / 20.0)
                period_ += (seconds - period_) * 0.1;
        }
        lastPresent_ = now;
        const unsigned extra = schedule().perFrame.load(std::memory_order_relaxed);
        ++frame_;
        if (frame && extra) {
            Picture real = packed(frame);
            real.order = predict_ ? frame_ * 8u : frame_ * 8u + extra + 1u;
            if (dump_folder()) {
                std::lock_guard ringLock(ringMutex_);
                ring_[frame_ % ring_.size()] = real;
            }
            real.due = predict_ ? now : now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(period_));
            publish(std::move(real));
        }
        record_.request = request;
        record_.present = bool(frame);
        record_.frame = frame_;
        record_.finished = now;
        record_.period = period_;
        Job job;
        job.record = std::move(record_);
        record_.clear();
        post(std::move(job));
        return frame;
    }
    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override {
        std::lock_guard lock(recordMutex_);
        const bool cleared = main_->ClearFramebuffer(context, rgba);
        if (!cleared)
            return false;
        if (recording()) {
            record_.ops.push_back({Kind::clear, uint32_t(record_.clears.size())});
            record_.clears.emplace_back(context, rgba);
        }
        return true;
    }
    uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override {
        return main_->ConsumeLocalToHostBytes(dst, maxBytes);
    }
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override {
        return main_->ReadVram(psm, base, bw, x, y);
    }
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override {
        std::lock_guard lock(recordMutex_);
        main_->WriteVram(psm, base, bw, x, y, value);
        if (!recording())
            return;
        record_.ops.push_back({Kind::write, uint32_t(record_.writes.size())});
        record_.writes.push_back({psm, base, bw, x, y, value});
    }
    void SnapshotVram(std::vector<uint8_t> &out) const override { main_->SnapshotVram(out); }
    GSTransferSnapshot GetTransferSnapshot() const override { return main_->GetTransferSnapshot(); }

  private:
    bool recording() {
        const double refresh = schedule().refresh.load(std::memory_order_relaxed);
        if (refresh > 0.0 && !pictures_for(refresh))
            abandoned_ = true;
        return !abandoned_;
    }
    void add(Kind kind, uint32_t index) {
        if (recording())
            record_.ops.push_back({kind, index});
    }
    void spill() {
        if (record_.size() < record_limit)
            return;
        Job job;
        job.record = std::move(record_);
        record_.clear();
        post(std::move(job));
    }
    void post(Job &&job) {
        {
            std::lock_guard lock(jobMutex_);
            jobs_.push_back(std::move(job));
        }
        jobReady_.notify_one();
    }

    void replay(const Record &record, const Match *match, const std::vector<GSPrimitiveBatch> *previous, float amount) {
        for (const Op &op : record.ops) {
            switch (op.kind) {
            case Kind::submit: {
                const GSPrimitiveBatch &batch = record.batches[op.index];
                const int32_t from = match ? match->previous[op.index] : -1;
                if (from >= 0)
                    mirror_->Submit(smoothing::blend((*previous)[size_t(from)], batch, amount, predict_));
                else if (match && match->carried[op.index])
                    mirror_->Submit(smoothing::carry(batch, match->motion[op.index], amount, predict_));
                else
                    mirror_->Submit(batch);
                break;
            }
            case Kind::clut:
                mirror_->LoadClut(record.cluts[op.index].first, record.cluts[op.index].second);
                break;
            case Kind::transfer:
                mirror_->BeginTransfer(record.transfers[op.index]);
                break;
            case Kind::upload:
                mirror_->UploadImage(record.uploads[op.index].data(), uint32_t(record.uploads[op.index].size()));
                break;
            case Kind::flush:
                mirror_->Flush();
                break;
            case Kind::textureFlush:
                mirror_->TextureFlush();
                break;
            case Kind::clear:
                mirror_->ClearFramebuffer(record.clears[op.index].first, record.clears[op.index].second);
                break;
            case Kind::write: {
                const auto &w = record.writes[op.index];
                mirror_->WriteVram(w[0], w[1], w[2], w[3], w[4], w[5]);
                break;
            }
            case Kind::reset:
                mirror_->Reset();
                break;
            }
        }
    }

    void process(Record &record, bool behind) {
        const unsigned extra = schedule().perFrame.load(std::memory_order_relaxed);
        largest_ = std::max<uint64_t>(largest_, record.batches.size());
        static const unsigned dumpPrimitives = [] {
            const char *value = std::getenv("OPENKAI2_SMOOTHING_DUMP_PRIMITIVES");
            return value && *value ? unsigned(std::strtoul(value, nullptr, 10)) : 1500u;
        }();
        if (dump_folder() && !dumpFrom_ && record.batches.size() >= dumpPrimitives)
            dumpFrom_ = record.frame;
        const bool dumping = dumpFrom_ && record.frame >= dumpFrom_ && record.frame < dumpFrom_ + 10u;
        auto *gpu = dynamic_cast<GpuRasterBackend *>(mirror_.get());
        if (auto *real = dynamic_cast<GpuRasterBackend *>(main_.get()); real && gpu)
            gpu->FollowScale(real->InternalScale());
        const bool draw = record.present && extra && (!behind || dumping) && havePrevious_ && !warmup_;
        if (draw) {
            Match match = smoothing::match_primitives(previous_, record.batches, jump_limit);
            const bool cut = match.matched && double(match.jumped) > cut_share * double(match.matched);
            blended_ += cut ? 0u : match.matched;
            primitives_ += record.batches.size();
            jumped_ += match.jumped;
            carried_ += cut ? 0u : match.carriedCount;
            cuts_ += cut ? 1u : 0u;
            const bool saved = !cut && gpu && gpu->Checkpoint();
            for (unsigned i = 1; i <= extra && saved; ++i) {
                const float amount = float(i) / float(extra + 1u);
                if (i > 1u)
                    gpu->Restore();
                replay(record, &match, &previous_, amount);
                mirror_->Flush();
                mirror_->Sync(GSSyncReason::Presentation);
                Picture picture = packed(mirror_->Present(record.request));
                picture.order = record.frame * 8u + i;
                const double offset = record.period * double(i) / double(extra + 1u);
                picture.due = record.finished + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(offset));
                if (Clock::now() > picture.due + std::chrono::milliseconds(2))
                    ++late_;
                if (dumping)
                    dump(picture);
                publish(std::move(picture));
                ++pictures_;
            }
            if (saved)
                gpu->Restore();
        }
        replay(record, nullptr, nullptr, 1.0f);
        if (dumping && record.present) {
            mirror_->Flush();
            mirror_->Sync(GSSyncReason::Presentation);
            Picture exact = packed(mirror_->Present(record.request));
            exact.order = record.frame * 8u + 7u;
            dump(exact);
            std::lock_guard ringLock(ringMutex_);
            Picture real = ring_[record.frame % ring_.size()];
            if (real.order / 8u == record.frame) {
                real.order = record.frame * 8u + 6u;
                dump(real);
            }
            std::cout << "[smoothing] dumped frame " << record.frame << " primitives=" << record.batches.size() << std::endl;
        }
        if (record.present) {
            previous_ = std::move(record.batches);
            havePrevious_ = true;
            warmup_ -= warmup_ ? 1u : 0u;
            ++frames_;
        }
        const auto now = Clock::now();
        if (now - reported_ > std::chrono::seconds(10)) {
            if (reported_ != Clock::time_point{})
                std::cout << "[smoothing] frames=" << frames_ << " pictures=" << pictures_ << " late=" << late_
                          << " primitives=" << primitives_ << " matched=" << blended_ << " carried=" << carried_ << " jumped=" << jumped_
                          << " cuts=" << cuts_ << " largest_frame=" << largest_ << " per_frame=" << extra
                          << (predict_ ? " mode=low_latency" : " mode=smooth") << std::endl;
            reported_ = now;
        }
    }

    void run() {
        for (;;) {
            Job job;
            bool behind = false;
            {
                std::unique_lock lock(jobMutex_);
                jobReady_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
                if (stopping_)
                    return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
                behind = std::any_of(jobs_.begin(), jobs_.end(), [](const Job &j) { return j.record.present; });
                if (jobs_.size() > backlog_limit && !dump_folder()) {
                    jobs_.clear();
                    lock.unlock();
                    give_up("the graphics card cannot keep up");
                    return;
                }
            }
            try {
                if (job.initialize) {
                    mirror_.reset();
                    memory_ = std::move(job.memory);
                    mirror_ = factory_();
                    mirror_->Initialize(memory_.data(), uint32_t(memory_.size()));
                    havePrevious_ = false;
                    warmup_ = 2u;
                    continue;
                }
                if (!mirror_)
                    continue;
                process(job.record, behind);
            } catch (const std::exception &error) {
                give_up(error.what());
                return;
            }
        }
    }

    void give_up(const std::string &why) {
        std::cout << "[smoothing] turned off: " << why << std::endl;
        abandoned_ = true;
        std::lock_guard lock(schedule().mutex);
        schedule().pending.clear();
        schedule().haveCurrent = false;
        schedule().generation = 0;
    }

    RasterFactory factory_;
    std::unique_ptr<GSRasterBackend> main_, mirror_;
    std::vector<uint8_t> memory_;
    bool predict_ = false;
    std::atomic<bool> abandoned_{false};
    std::mutex recordMutex_;
    Record record_;
    uint64_t frame_ = 0, dumpFrom_ = 0;
    std::mutex ringMutex_;
    std::array<Picture, 64> ring_;
    double period_ = 1.0 / 59.94;
    Clock::time_point lastPresent_{};
    std::mutex jobMutex_;
    std::condition_variable jobReady_;
    std::deque<Job> jobs_;
    bool stopping_ = false;
    std::vector<GSPrimitiveBatch> previous_;
    bool havePrevious_ = false;
    unsigned warmup_ = 0;
    uint64_t frames_ = 0, pictures_ = 0, late_ = 0, blended_ = 0, primitives_ = 0, jumped_ = 0, cuts_ = 0, largest_ = 0, carried_ = 0;
    Clock::time_point reported_{};
    std::thread worker_;
};
}

std::unique_ptr<GSRasterBackend> make_smoothing_backend(RasterFactory factory, FrameSmoothing mode) {
    if (mode == FrameSmoothing::off)
        return factory();
    return std::make_unique<SmoothingBackend>(std::move(factory), mode);
}

void set_display_refresh(double hertz) {
    if (const char *forced = std::getenv("OPENKAI2_DISPLAY_HZ"); forced && *forced)
        hertz = std::strtod(forced, nullptr);
    auto &s = schedule();
    s.refresh.store(hertz, std::memory_order_relaxed);
    s.perFrame.store(pictures_for(hertz), std::memory_order_relaxed);
}

unsigned smoothing_pictures_per_frame() { return schedule().perFrame.load(std::memory_order_relaxed); }

uint64_t smoothed_generation() {
    auto &s = schedule();
    std::lock_guard lock(s.mutex);
    if (!s.enabled)
        return 0u;
    const auto now = Clock::now();
    bool advanced = false;
    while (!s.pending.empty() && s.pending.front().due <= now) {
        if (s.pending.front().order > s.shownOrder) {
            s.current = std::move(s.pending.front());
            s.shownOrder = s.current.order;
            s.haveCurrent = advanced = true;
        }
        s.pending.pop_front();
    }
    if (advanced)
        ++s.generation;
    return s.generation;
}

bool copy_smoothed_picture(std::vector<uint8_t> &pixels, uint32_t &width, uint32_t &height, uint32_t *displayFbp,
                           uint32_t *sourceFbp, bool *usedPreferred) {
    auto &s = schedule();
    std::lock_guard lock(s.mutex);
    if (!s.enabled || !s.haveCurrent || !s.generation)
        return false;
    pixels = s.current.pixels;
    width = s.current.width;
    height = s.current.height;
    if (displayFbp)
        *displayFbp = s.current.displayFbp;
    if (sourceFbp)
        *sourceFbp = s.current.sourceFbp;
    if (usedPreferred)
        *usedPreferred = s.current.usedPreferred;
    return true;
}

namespace smoothing {
namespace {
uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
    return h;
}
int32_t coordinate(float value) { return int32_t(std::lround(double(value) * 1024.0)); }
int32_t cell_of(float value) { return int32_t(std::floor(double(value) / 16.0)); }
uint64_t cell_key(int32_t x, int32_t y) { return (uint64_t(uint32_t(x)) << 32u) | uint64_t(uint32_t(y)); }
}

uint64_t primitive_key(const GSPrimitiveBatch &batch) {
    const auto &state = batch.state;
    const auto &tex = state.context.tex0;
    uint64_t h = mix(batch.vertexCount, uint64_t(state.prim.tme) | (uint64_t(state.prim.fst) << 1u) |
                                            (uint64_t(state.prim.abe) << 2u) | (uint64_t(state.prim.ctxt) << 3u));
    h = mix(h, uint64_t(state.context.frame.psm));
    h = mix(h, uint64_t(tex.tbp0) | (uint64_t(tex.psm) << 32u) | (uint64_t(tex.cbp) << 40u));
    for (unsigned i = 0; i < batch.vertexCount && i < batch.vertices.size(); ++i) {
        const GSVertex &v = batch.vertices[i];
        if (state.prim.fst)
            h = mix(h, uint64_t(v.u) | (uint64_t(v.v) << 16u));
        else {
            const float q = v.q != 0.0f ? v.q : 1.0f;
            h = mix(h, uint64_t(uint32_t(coordinate(v.s / q))) | (uint64_t(uint32_t(coordinate(v.t / q))) << 32u));
        }
    }
    return h;
}

Match match_primitives(const std::vector<GSPrimitiveBatch> &previous, const std::vector<GSPrimitiveBatch> &current,
                       float jumpLimit) {
    Match result;
    result.previous.assign(current.size(), -1);
    std::unordered_map<uint64_t, std::vector<int32_t>> byKey;
    byKey.reserve(previous.size());
    for (size_t i = previous.size(); i-- > 0;)
        if (previous[i].state.prim.tme)
            byKey[primitive_key(previous[i])].push_back(int32_t(i));
    for (size_t i = 0; i < current.size(); ++i) {
        if (!current[i].state.prim.tme)
            continue;
        const auto found = byKey.find(primitive_key(current[i]));
        if (found == byKey.end() || found->second.empty())
            continue;
        const int32_t from = found->second.back();
        found->second.pop_back();
        const GSPrimitiveBatch &a = previous[size_t(from)], &b = current[i];
        float moved = 0.0f;
        for (unsigned v = 0; v < b.vertexCount && v < b.vertices.size(); ++v)
            moved = std::max({moved, std::fabs(b.vertices[v].x - a.vertices[v].x), std::fabs(b.vertices[v].y - a.vertices[v].y)});
        ++result.matched;
        if (moved > jumpLimit) {
            ++result.jumped;
            continue;
        }
        result.previous[i] = from;
    }
    struct Sample {
        double z;
        float dx, dy;
    };
    std::unordered_map<uint64_t, std::vector<Sample>> field;
    field.reserve(current.size());
    for (size_t i = 0; i < current.size(); ++i) {
        const int32_t from = result.previous[i];
        if (from < 0)
            continue;
        const GSPrimitiveBatch &a = previous[size_t(from)], &b = current[i];
        for (unsigned v = 0; v < b.vertexCount && v < b.vertices.size(); ++v) {
            auto &cell = field[cell_key(cell_of(b.vertices[v].x), cell_of(b.vertices[v].y))];
            if (cell.size() < 12u)
                cell.push_back({b.vertices[v].z, b.vertices[v].x - a.vertices[v].x, b.vertices[v].y - a.vertices[v].y});
        }
    }
    result.motion.assign(current.size(), {});
    result.carried.assign(current.size(), 0u);
    if (field.empty())
        return result;
    for (size_t i = 0; i < current.size(); ++i) {
        if (result.previous[i] >= 0)
            continue;
        const GSPrimitiveBatch &b = current[i];
        std::array<bool, 3> found{};
        float sumX = 0.0f, sumY = 0.0f;
        unsigned count = 0;
        for (unsigned v = 0; v < b.vertexCount && v < b.vertices.size(); ++v) {
            const GSVertex &vertex = b.vertices[v];
            const int32_t cx = cell_of(vertex.x), cy = cell_of(vertex.y);
            const double tolerance = std::max(256.0, std::fabs(vertex.z) * 0.05);
            double best = tolerance;
            for (int32_t oy = -1; oy <= 1; ++oy)
                for (int32_t ox = -1; ox <= 1; ++ox) {
                    const auto cell = field.find(cell_key(cx + ox, cy + oy));
                    if (cell == field.end())
                        continue;
                    for (const Sample &sample : cell->second) {
                        const double gap = std::fabs(sample.z - vertex.z);
                        if (gap <= best) {
                            best = gap;
                            result.motion[i][v] = {sample.dx, sample.dy};
                            found[v] = true;
                        }
                    }
                }
            if (found[v]) {
                sumX += result.motion[i][v][0];
                sumY += result.motion[i][v][1];
                ++count;
            }
        }
        if (!count)
            continue;
        for (unsigned v = 0; v < b.vertexCount && v < b.vertices.size(); ++v)
            if (!found[v])
                result.motion[i][v] = {sumX / float(count), sumY / float(count)};
        result.carried[i] = 1u;
        ++result.carriedCount;
    }
    return result;
}

GSPrimitiveBatch carry(const GSPrimitiveBatch &current, const std::array<std::array<float, 2>, 3> &motion, float amount,
                       bool predict) {
    GSPrimitiveBatch out = current;
    const float share = predict ? amount : amount - 1.0f;
    for (unsigned i = 0; i < current.vertexCount && i < current.vertices.size(); ++i) {
        out.vertices[i].x += motion[i][0] * share;
        out.vertices[i].y += motion[i][1] * share;
    }
    return out;
}

GSPrimitiveBatch blend(const GSPrimitiveBatch &previous, const GSPrimitiveBatch &current, float amount, bool predict) {
    GSPrimitiveBatch out = current;
    for (unsigned i = 0; i < current.vertexCount && i < current.vertices.size(); ++i) {
        const GSVertex &a = previous.vertices[i], &b = current.vertices[i];
        GSVertex &v = out.vertices[i];
        if (predict) {
            v.x = b.x + (b.x - a.x) * amount;
            v.y = b.y + (b.y - a.y) * amount;
            v.z = b.z + (b.z - a.z) * double(amount);
        } else {
            v.x = a.x + (b.x - a.x) * amount;
            v.y = a.y + (b.y - a.y) * amount;
            v.z = a.z + (b.z - a.z) * double(amount);
        }
        if (v.z < 0.0)
            v.z = 0.0;
    }
    return out;
}

void verify() {
    const auto require = [](bool ok, const char *why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto triangle = [](float x, float s, uint32_t tbp) {
        GSPrimitiveBatch batch;
        batch.vertexCount = 3;
        batch.state.prim.tme = true;
        batch.state.context.tex0.tbp0 = tbp;
        for (unsigned i = 0; i < 3; ++i) {
            batch.vertices[i].x = x + float(i) * 8.0f;
            batch.vertices[i].y = 100.0f + float(i);
            batch.vertices[i].z = 1000.0;
            batch.vertices[i].s = s + float(i) * 0.125f;
            batch.vertices[i].t = 0.5f;
            batch.vertices[i].q = 1.0f;
        }
        return batch;
    };
    const std::vector<GSPrimitiveBatch> before = {triangle(10, 0.0f, 1), triangle(50, 0.25f, 1), triangle(90, 0.5f, 2)};
    std::vector<GSPrimitiveBatch> after = {triangle(98, 0.5f, 2), triangle(20, 0.0f, 1)};
    Match match = match_primitives(before, after, jump_limit);
    require(match.previous.size() == 2 && match.previous[0] == 2 && match.previous[1] == 0,
            "Smoothing matched triangles by order instead of by texture coordinates");
    const auto half = blend(before[0], after[1], 0.5f, false);
    require(std::fabs(half.vertices[0].x - 15.0f) < 0.001f, "Smoothing did not blend halfway");
    const auto ahead = blend(before[0], after[1], 0.5f, true);
    require(std::fabs(ahead.vertices[0].x - 25.0f) < 0.001f, "Low latency smoothing did not project forward");
    after[1] = triangle(400, 0.0f, 1);
    match = match_primitives(before, after, jump_limit);
    require(match.previous[1] == -1 && match.jumped == 1, "Smoothing blended a triangle across a camera cut");
    auto outlineBefore = triangle(10, 0.0f, 1), outlineAfter = triangle(20, 0.0f, 1);
    outlineBefore.state.prim.tme = outlineAfter.state.prim.tme = false;
    const std::vector<GSPrimitiveBatch> shapes = {triangle(10, 0.0f, 1), outlineBefore};
    const std::vector<GSPrimitiveBatch> moved = {triangle(20, 0.0f, 1), outlineAfter};
    match = match_primitives(shapes, moved, jump_limit);
    require(match.previous[0] == 0 && match.previous[1] == -1 && match.carried[1],
            "An untextured outline was not carried by the surface around it");
    const auto carried = carry(moved[1], match.motion[1], 0.5f, false);
    require(std::fabs(carried.vertices[0].x - 15.0f) < 0.001f, "An untextured outline did not move halfway with its surface");
    require(pictures_for(240.0) == 3u && pictures_for(144.0) == 1u && pictures_for(120.0) == 1u &&
                pictures_for(60.0) == 0u && pictures_for(165.0) == 2u,
            "Smoothing picked the wrong number of pictures per frame");
}
}

}
