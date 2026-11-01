// SPDX-License-Identifier: GPL-3.0-only
#include "machine_state.h"
#include "disc_image.h"
#include "host_input.h"
#include "netplay.h"
#include "state_archive.h"
#include "runtime/ee_scheduler.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <cstring>
#include <exception>
#include <mutex>
#include <optional>

namespace openkai2 {
namespace {
struct Request {
    bool load = false;
    std::vector<uint8_t> data;
    std::function<void(MachineStateResult)> done;
};
std::mutex requestMutex;
std::optional<Request> pending;
std::atomic<bool> hasPending{false};
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
constexpr uint64_t sampleFrame = 30;

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
    if (std::sscanf(value, "%llu,%llu,%d", &a, &b, &c) >= 1 && a)
        startAt = a;
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
        scheduler.serializeMachine(saving);
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
    scheduler.serializeMachine(loading);
    std::cerr << "[determinism] loaded ms=" << milliseconds(loadBegan) << '\n';
    if (hash_memory() != savedHash)
        report_differences("after_load", savedMemory);
    last = start;
    buttons = pattern(0);
}
}

uint64_t machine_memory_hash() { return machineMemory ? hash_memory() : 0; }

void set_machine_memory(const uint8_t *rdram, size_t bytes) {
    machineMemory = rdram;
    machineMemoryBytes = bytes;
    probe::configure();
}

void set_machine_state_identity(std::string value) { identity = std::move(value); }
const std::string &machine_state_identity() { return identity; }

void request_machine_save(std::function<void(MachineStateResult)> done) {
    std::lock_guard lock(requestMutex);
    pending = Request{false, {}, std::move(done)};
    hasPending = true;
}

void request_machine_load(std::vector<uint8_t> data, std::function<void(MachineStateResult)> done) {
    std::lock_guard lock(requestMutex);
    pending = Request{true, std::move(data), std::move(done)};
    hasPending = true;
}

bool machine_state_pending() { return hasPending.load(); }

void machine_state_point(EeScheduler &scheduler) {
    const bool pendingRequest = hasPending.load(std::memory_order_acquire);
    auto *session = netplay::active();
    if (!probe::enabled && !pendingRequest && !session)
        return;
    std::string reason;
    const bool savable = scheduler.stateSavable(&reason);
    if (probe::enabled && savable)
        probe::point(scheduler);
    if (session && savable)
        session->state_point();
    if (!pendingRequest)
        return;
    if (!savable) {
        if (reason != "a movie is playing")
            return;
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
            scheduler.serializeMachine(archive);
            result.ok = true;
        } catch (const std::exception &error) {
            result.data.clear();
            result.message = error.what();
        }
    } else {
        std::vector<uint8_t> backup;
        try {
            StateArchive saving(backup);
            scheduler.serializeMachine(saving);
            StateArchive loading(static_cast<const std::vector<uint8_t> &>(request.data));
            scheduler.serializeMachine(loading);
            if (!loading.finished())
                throw std::runtime_error("Save state has extra data");
            result.ok = true;
        } catch (const std::exception &error) {
            result.message = error.what();
            if (!backup.empty()) {
                try {
                    StateArchive restore(static_cast<const std::vector<uint8_t> &>(backup));
                    scheduler.serializeMachine(restore);
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
