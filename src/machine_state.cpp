// SPDX-License-Identifier: GPL-3.0-only
#include "machine_state.h"
#include "disc_image.h"
#include "host_input.h"
#include "delta_states.h"
#include "frame_presenter.h"
#include "netplay.h"
#include "simulation_pacing.h"
#include "state_archive.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <cstring>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

namespace openkai2 {
namespace {
struct Request {
    bool load = false;
    std::vector<uint8_t> data;
    std::function<void(MachineStateResult)> done;
    std::chrono::steady_clock::time_point made = std::chrono::steady_clock::now();
};
std::mutex requestMutex;
std::string blockedBy;
std::optional<Request> pending;
std::atomic<bool> hasPending{false};
std::atomic<bool> manualStates{true};
std::string identity;
constexpr uint32_t magic = 0x3153424fu, version = 1u;

uint64_t checksum(const uint8_t *data, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

void put(std::vector<uint8_t> &out, const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

template <typename T> bool take(const std::vector<uint8_t> &in, size_t &at, T &value) {
    if (at + sizeof value > in.size())
        return false;
    std::memcpy(&value, in.data() + at, sizeof value);
    at += sizeof value;
    return true;
}
}

namespace {
std::vector<std::function<void(StateArchive &)>> &machineParts() {
    static std::vector<std::function<void(StateArchive &)>> parts;
    return parts;
}
}

void add_machine_state_part(std::function<void(StateArchive &)> part) { machineParts().push_back(std::move(part)); }

void serialize_machine(EeScheduler &scheduler, StateArchive &archive) {
    scheduler.serializeMachine(archive);
    archive.section(0x54524150u);
    for (auto &part : machineParts())
        part(archive);
    archive.section(0x444e4550u);
}

const uint8_t *machineMemory = nullptr;
size_t machineMemoryBytes = 0;

uint64_t hash_memory() {
    uint64_t hash = 1469598103934665603ull;
    const size_t words = machineMemoryBytes / 8;
    for (size_t i = 0; i < words; ++i) {
        uint64_t word;
        std::memcpy(&word, machineMemory + i * 8, 8);
        hash = (hash ^ word) * 0x100000001b3ull;
        hash ^= hash >> 29;
    }
    return hash;
}

namespace probe {
bool enabled = false;
int phase = 0, run = 0, runs = 3;
uint64_t startAt = 1800, frames = 300, start = 0, last = 0;
std::vector<uint8_t> base;
std::map<uint64_t, uint64_t> reference, current;
std::atomic<uint16_t> buttons{0xffff};
std::vector<uint8_t> savedMemory, sampleMemory;
uint64_t savedHash = 0;
uint64_t sampleFrame = 30;

void report_differences(const char *what, const std::vector<uint8_t> &expected) {
    size_t count = 0;
    for (size_t i = 0; i + 4 <= machineMemoryBytes; i += 4) {
        uint32_t a, b;
        std::memcpy(&a, expected.data() + i, 4);
        std::memcpy(&b, machineMemory + i, 4);
        if (a == b)
            continue;
        if (count < 24)
            std::cerr << "[determinism] " << what << " 0x" << std::hex << i << " 0x" << a << " -> 0x" << b << std::dec << '\n';
        ++count;
    }
    std::cerr << "[determinism] " << what << " differing_words=" << count << '\n';
}

uint16_t pattern(uint64_t frame) {
    static constexpr uint16_t presses[] = {0x0008, 0x4000, 0x0020, 0x0040, 0x4000, 0x2000, 0x0010, 0x0080};
    if (frame % 40 >= 4)
        return 0xffff;
    return uint16_t(~presses[(frame / 40) % 8]);
}

bool pad(int port, LatchedPadState::Packet &packet) {
    if (port != 0)
        return false;
    const uint16_t value = buttons.load();
    packet[2] = uint8_t(value);
    packet[3] = uint8_t(value >> 8);
    return true;
}

double milliseconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

void configure() {
    const char *value = std::getenv("OPENKAI2_DETERMINISM");
    if (!value || !*value)
        return;
    enabled = true;
    unsigned long long a = 0, b = 0;
    int c = 0;
    unsigned long long d = 0;
    if (std::sscanf(value, "%llu,%llu,%d,%llu", &a, &b, &c, &d) >= 1 && a)
        startAt = a;
    if (d)
        sampleFrame = d;
    if (b)
        frames = b;
    if (c > 1)
        runs = c;
}

void point(EeScheduler &scheduler) {
    if (phase == 2 || !machineMemory)
        return;
    const uint64_t tick = scheduler.currentVSyncTick();
    if (phase == 0) {
        if (tick + 600 >= startAt) {
            const uint64_t lead = tick + 600 - startAt;
            buttons = lead < 240 && lead % 120 < 4 ? uint16_t(~0x0008) : uint16_t(0xffff);
            set_pad_source(&pad);
        }
        if (tick < startAt)
            return;
        const auto began = std::chrono::steady_clock::now();
        base.clear();
        StateArchive saving(base);
        serialize_machine(scheduler, saving);
        savedMemory.assign(machineMemory, machineMemory + machineMemoryBytes);
        savedHash = hash_memory();
        std::cerr << "[determinism] saved tick=" << tick << " bytes=" << base.size() << " ms=" << milliseconds(began) << '\n';
        start = last = tick;
        buttons = pattern(0);
        set_pad_source(&pad);
        phase = 1;
        return;
    }
    if (tick == last)
        return;
    last = tick;
    const auto began = std::chrono::steady_clock::now();
    current[tick] = hash_memory();
    if (tick - start == sampleFrame) {
        if (run == 0)
            sampleMemory.assign(machineMemory, machineMemory + machineMemoryBytes);
        else
            report_differences("frame30", sampleMemory);
    }
    const double hashMs = milliseconds(began);
    buttons = pattern(tick - start);
    if (tick - start < frames)
        return;
    if (run == 0) {
        reference = current;
        std::cerr << "[determinism] run=0 frames=" << current.size() << " hash_ms=" << hashMs << '\n';
    } else {
        size_t compared = 0, differing = 0;
        uint64_t first = 0;
        for (const auto &[at, hash] : current) {
            const auto found = reference.find(at);
            if (found == reference.end())
                continue;
            ++compared;
            if (found->second != hash) {
                if (!differing)
                    first = at - start;
                ++differing;
            }
        }
        std::cerr << "[determinism] run=" << run << " compared=" << compared << " differing=" << differing;
        if (differing)
            std::cerr << " first_frame=" << first;
        std::cerr << '\n';
    }
    current.clear();
    if (++run >= runs) {
        set_pad_source(nullptr);
        phase = 2;
        std::cerr << "[determinism] done\n";
        return;
    }
    const auto loadBegan = std::chrono::steady_clock::now();
    StateArchive loading(static_cast<const std::vector<uint8_t> &>(base));
    serialize_machine(scheduler, loading);
    std::cerr << "[determinism] loaded ms=" << milliseconds(loadBegan) << '\n';
    if (hash_memory() != savedHash)
        report_differences("after_load", savedMemory);
    last = start;
    buttons = pattern(0);
}

bool rollbackEnabled = false, rollbackDone = false;
uint64_t rollbackStart = 1800, rollbackFrames = 600, rollbackDepth = 4, rollbackLast = ~0ull, rollbackHighest = 0, rollbackRolled = 0;
std::unique_ptr<DeltaStates> rollbackStates;
std::map<uint64_t, uint64_t> rollbackHashes;
struct RollbackTotals {
    uint64_t captures = 0, pages = 0, small = 0, rollbacks = 0, compared = 0, differing = 0, firstDifference = 0;
    double captureMs = 0, captureMax = 0, restoreMs = 0, restoreMax = 0, compareMs = 0, replayMs = 0;
    std::chrono::steady_clock::time_point replayBegan{};
} rollbackTotals;

void configure_rollback() {
    const char *value = std::getenv("OPENKAI2_ROLLBACK_TEST");
    if (!value || !*value)
        return;
    unsigned long long a = 0, b = 0, c = 0, picture = 0;
    const int read = std::sscanf(value, "%llu,%llu,%llu,%llu", &a, &b, &c, &picture);
    if (read >= 1 && a)
        rollbackStart = a;
    if (read >= 2 && b)
        rollbackFrames = b;
    if (read >= 3 && c)
        rollbackDepth = std::min<unsigned long long>(c, 30);
    rollbackStates = std::make_unique<DeltaStates>(size_t(rollbackDepth) + 2u, std::clamp(std::thread::hardware_concurrency(), 2u, 4u) - 1u);
    rollbackStates->pictureless(picture == 0);
    std::cerr << "[rollback] test snapshots " << (picture ? "include" : "leave out") << " the picture\n";
    rollbackEnabled = true;
}

void rollback_point(EeScheduler &scheduler) {
    if (rollbackDone || !machineMemory)
        return;
    const uint64_t tick = scheduler.currentVSyncTick();
    if (tick < rollbackStart || tick == rollbackLast)
        return;
    if (rollbackLast == ~0ull) {
        rollbackStart = tick;
        set_pad_source(&pad);
    }
    rollbackLast = tick;
    auto &totals = rollbackTotals;
    const uint64_t hash = hash_memory();
    if (const auto known = rollbackHashes.find(tick); known != rollbackHashes.end()) {
        ++totals.compared;
        if (known->second != hash && !totals.differing++)
            totals.firstDifference = tick - rollbackStart;
    } else
        rollbackHashes[tick] = hash;
    if (replay_turbo.load() && tick >= rollbackHighest) {
        replay_turbo = false;
        totals.replayMs += milliseconds(totals.replayBegan);
    }
    const auto serialize = [&](StateArchive &archive) { serialize_machine(scheduler, archive); };
    auto began = std::chrono::steady_clock::now();
    rollbackStates->capture(tick, serialize);
    double elapsed = milliseconds(began);
    if (totals.captures++) {
        totals.captureMs += elapsed;
        totals.captureMax = std::max(totals.captureMax, elapsed);
        totals.pages += rollbackStates->stats().pages;
        totals.compareMs += rollbackStates->stats().compareMs;
    } else
        std::cerr << "[rollback] first capture ms=" << elapsed << " tracked_blocks=" << rollbackStates->stats().regions
                  << " other_state_bytes=" << rollbackStates->stats().small << '\n';
    totals.small = rollbackStates->stats().small;
    const uint64_t frame = tick - rollbackStart;
    if (tick >= rollbackHighest && frame >= rollbackFrames) {
        rollbackDone = true;
        set_pad_source(nullptr);
        const double captures = double(std::max<uint64_t>(totals.captures - 1u, 1u)), rollbacks = double(std::max<uint64_t>(totals.rollbacks, 1u));
        std::cerr << "[rollback] frames=" << frame << " depth=" << rollbackDepth << " captures=" << totals.captures
                  << " capture_avg_ms=" << totals.captureMs / captures << " capture_max_ms=" << totals.captureMax
                  << " memory_compare_avg_ms=" << totals.compareMs / captures << " changed_pages_avg=" << double(totals.pages) / captures << " other_state_bytes=" << totals.small
                  << " rollbacks=" << totals.rollbacks << " restore_avg_ms=" << totals.restoreMs / rollbacks
                  << " restore_max_ms=" << totals.restoreMax << " replay_avg_ms=" << totals.replayMs / rollbacks
                  << " replay_skipped_draw_packets=" << replay_skipped_packets.load() << " resimulated_frames_compared=" << totals.compared
                  << " differing=" << totals.differing;
        if (totals.differing)
            std::cerr << " first_frame=" << totals.firstDifference;
        std::cerr << "\n[rollback] done\n";
        return;
    }
    rollbackHighest = std::max(rollbackHighest, tick);
    buttons = pattern(frame);
    if (tick > rollbackRolled && frame % (rollbackDepth * 3u) == rollbackDepth * 3u - 1u && rollbackStates->holds(tick - rollbackDepth)) {
        rollbackRolled = tick;
        began = std::chrono::steady_clock::now();
        const bool restored = rollbackStates->restore(tick - rollbackDepth, serialize);
        elapsed = milliseconds(began);
        if (!restored)
            throw std::runtime_error("Rollback test could not restore a kept frame");
        totals.restoreMs += elapsed;
        totals.restoreMax = std::max(totals.restoreMax, elapsed);
        ++totals.rollbacks;
        totals.replayBegan = std::chrono::steady_clock::now();
        replay_turbo = true;
        rollbackLast = tick - rollbackDepth;
        buttons = pattern(rollbackLast - rollbackStart);
        if (hash_memory() != rollbackHashes[rollbackLast] && !totals.differing++)
            totals.firstDifference = rollbackLast - rollbackStart;
        ++totals.compared;
    }
}
}

namespace runahead {
std::atomic<unsigned> frames{0};
constexpr uint64_t adapt_window = 120;
bool active = false, shown = false;
uint64_t base = 0, shownAt = 0, lastTick = ~0ull, passes = 0, abandoned = 0, windowFields = 0, windowLate = 0;
std::chrono::steady_clock::time_point capturedAt{};
std::unique_ptr<DeltaStates> states;

void leave() {
    if (active)
        ++abandoned;
    active = shown = false;
    replay_turbo = false;
    runahead_drawing = false;
    hide_guest_fields(false);
}

void point(EeScheduler &scheduler) {
    const uint64_t tick = scheduler.currentVSyncTick();
    if (tick == lastTick)
        return;
    lastTick = tick;
    const unsigned ahead = frames.load(std::memory_order_relaxed);
    if (!states)
        states = std::make_unique<DeltaStates>(4u, std::clamp(std::thread::hardware_concurrency(), 2u, 4u) - 1u);
    states->pictureless(true);
    const auto serialize = [&](StateArchive &archive) { serialize_machine(scheduler, archive); };
    if (!active) {
        if (states->size() && tick <= states->newest())
            states->clear();
        states->capture(tick, serialize);
        capturedAt = std::chrono::steady_clock::now();
        base = tick;
        active = true;
        shown = false;
        hide_guest_fields(true);
        runahead_drawing = true;
        replay_turbo = true;
        return;
    }
    if (!shown) {
        if (tick >= base + ahead) {
            shown = true;
            shownAt = tick;
            hide_guest_fields(false);
        }
        return;
    }
    if (tick <= shownAt)
        return;
    hide_guest_fields(true);
    wait_for_guest_fields(std::chrono::milliseconds(100));
    restore_rebase_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - capturedAt).count();
    const bool restored = states->restore(base, serialize);
    restore_rebase_ns = 0;
    if (!restored) {
        leave();
        return;
    }
    active = shown = false;
    replay_turbo = false;
    runahead_drawing = false;
    lastTick = base;
    ++passes;
    if (passes % adapt_window == 0) {
        const uint64_t fields = field_lateness.fields.load(std::memory_order_relaxed);
        const uint64_t late = field_lateness.late.load(std::memory_order_relaxed);
        const uint64_t seen = fields - windowFields, behind = late - windowLate;
        windowFields = fields;
        windowLate = late;
        const unsigned current = frames.load(std::memory_order_relaxed);
        if (passes > adapt_window && seen && behind * 4 > seen && current) {
            frames = current - 1;
            std::cout << "[runahead] " << behind << " of " << seen
                      << " recent frames were late, so this PC cannot keep up; run-ahead is now "
                      << (current - 1) << " frame(s)" << std::endl;
            if (current == 1)
                leave();
        }
    }
    if (passes % 600 == 0)
        std::cout << "[runahead] passes=" << passes << " kept_ahead_at_loading=" << abandoned << std::endl;
}
}

void configure_run_ahead(unsigned frames) {
    runahead::frames = std::min(frames, 3u);
    if (!frames)
        runahead::leave();
}

unsigned run_ahead_frames() { return runahead::frames.load(std::memory_order_relaxed); }

uint64_t machine_memory_hash() { return machineMemory ? hash_memory() : 0; }

void set_machine_memory(const uint8_t *rdram, size_t bytes) {
    machineMemory = rdram;
    machineMemoryBytes = bytes;
    probe::configure();
    probe::configure_rollback();
}

void set_machine_state_identity(std::string value) { identity = std::move(value); }
const std::string &machine_state_identity() { return identity; }

void enable_manual_machine_states(bool enabled) { manualStates = enabled; }
void request_machine_save(std::function<void(MachineStateResult)> done, MachineStatePurpose purpose) {
    if (purpose == MachineStatePurpose::manual && !manualStates.load()) {
        if (done) done({false, "Manual save states are disabled in speedrun mode", {}});
        return;
    }
    std::lock_guard lock(requestMutex);
    pending = Request{false, {}, std::move(done)};
    hasPending = true;
}

void request_machine_load(std::vector<uint8_t> data, std::function<void(MachineStateResult)> done, MachineStatePurpose purpose) {
    if (purpose == MachineStatePurpose::manual && !manualStates.load()) {
        if (done) done({false, "Manual load states are disabled in speedrun mode", {}});
        return;
    }
    std::lock_guard lock(requestMutex);
    pending = Request{true, std::move(data), std::move(done)};
    hasPending = true;
}

bool machine_state_pending() {
    if (!hasPending.load())
        return false;
    Request expired;
    std::string why;
    {
        std::lock_guard lock(requestMutex);
        if (!pending || std::chrono::steady_clock::now() - pending->made < std::chrono::seconds(3))
            return hasPending.load();
        expired = std::move(*pending);
        pending.reset();
        hasPending = false;
        why = blockedBy.empty() ? std::string("the game is busy") : blockedBy;
    }
    std::cerr << "[state] request dropped after 3 s: " << why << std::endl;
    if (expired.done)
        expired.done(MachineStateResult{false, "not available right now (" + why + "). Try again in a moment", {}});
    return false;
}

void machine_state_point(EeScheduler &scheduler) {
    const bool pendingRequest = hasPending.load(std::memory_order_acquire);
    auto *session = netplay::active();
    const bool runAhead = runahead::frames.load(std::memory_order_relaxed) && !session && !probe::enabled && !probe::rollbackEnabled;
    if (!probe::enabled && !probe::rollbackEnabled && !pendingRequest && !session && !runAhead && !runahead::active)
        return;
    std::string reason;
    const bool savable = scheduler.stateSavable(&reason);
    if (runahead::active && (!runAhead || pendingRequest || !savable))
        runahead::leave();
    if (runAhead && savable && !pendingRequest)
        runahead::point(scheduler);
    else if (!runahead::active)
        hide_guest_fields(false);
    if (probe::enabled && savable)
        probe::point(scheduler);
    if (probe::rollbackEnabled && savable)
        probe::rollback_point(scheduler);
    if (session && savable)
        session->state_point();
    if (!pendingRequest)
        return;
    if (!savable) {
        if (reason != "a movie is playing") {
            std::lock_guard lock(requestMutex);
            blockedBy = reason;
            return;
        }
        Request refused;
        {
            std::lock_guard lock(requestMutex);
            if (!pending)
                return;
            refused = std::move(*pending);
            pending.reset();
            hasPending = false;
        }
        if (refused.done)
            refused.done(MachineStateResult{false, "not available while a movie plays", {}});
        return;
    }
    Request request;
    {
        std::lock_guard lock(requestMutex);
        if (!pending)
            return;
        request = std::move(*pending);
        pending.reset();
        hasPending = false;
    }
    MachineStateResult result;
    if (!request.load) {
        try {
            StateArchive archive(result.data);
            serialize_machine(scheduler, archive);
            result.ok = true;
        } catch (const std::exception &error) {
            result.data.clear();
            result.message = error.what();
        }
    } else {
        std::vector<uint8_t> backup;
        try {
            StateArchive saving(backup);
            serialize_machine(scheduler, saving);
            StateArchive loading(static_cast<const std::vector<uint8_t> &>(request.data));
            serialize_machine(scheduler, loading);
            if (!loading.finished())
                throw std::runtime_error("Save state has extra data");
            result.ok = true;
        } catch (const std::exception &error) {
            result.message = error.what();
            if (!backup.empty()) {
                try {
                    StateArchive restore(static_cast<const std::vector<uint8_t> &>(backup));
                    serialize_machine(scheduler, restore);
                } catch (...) {
                    result.message += "; the game could not be restored and should be restarted";
                }
            }
        }
    }
    if (request.done)
        request.done(std::move(result));
}

std::vector<uint8_t> wrap_machine_state(const std::vector<uint8_t> &body) {
    std::vector<uint8_t> out;
    put(out, &magic, sizeof magic);
    put(out, &version, sizeof version);
    const uint32_t length = uint32_t(identity.size());
    put(out, &length, sizeof length);
    put(out, identity.data(), identity.size());
    const uint64_t size = body.size(), sum = checksum(body.data(), body.size());
    put(out, &size, sizeof size);
    put(out, &sum, sizeof sum);
    put(out, body.data(), body.size());
    return out;
}

bool unwrap_machine_state(const std::vector<uint8_t> &file, std::vector<uint8_t> &body, std::string &problem) {
    size_t at = 0;
    uint32_t seenMagic = 0, seenVersion = 0, length = 0;
    if (!take(file, at, seenMagic) || seenMagic != magic) {
        problem = "not an OpenKai2 save state";
        return false;
    }
    if (!take(file, at, seenVersion) || seenVersion != version) {
        problem = "made by a different save state version";
        return false;
    }
    if (!take(file, at, length) || at + length > file.size()) {
        problem = "damaged";
        return false;
    }
    const std::string seenIdentity(reinterpret_cast<const char *>(file.data() + at), length);
    at += length;
    if (seenIdentity != identity) {
        problem = "made with a different OpenKai2 build or game disc";
        return false;
    }
    uint64_t size = 0, sum = 0;
    if (!take(file, at, size) || !take(file, at, sum) || at + size != file.size()) {
        problem = "damaged";
        return false;
    }
    body.assign(file.begin() + std::ptrdiff_t(at), file.end());
    if (checksum(body.data(), body.size()) != sum) {
        problem = "damaged";
        body.clear();
        return false;
    }
    return true;
}

std::filesystem::path machine_state_file(unsigned slot) {
    return default_cache_directory().parent_path() / "states" / ("slot" + std::to_string(slot) + ".okstate");
}
}
