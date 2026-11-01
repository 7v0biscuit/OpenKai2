// SPDX-License-Identifier: GPL-3.0-only
#include "machine_state.h"
#include "disc_image.h"
#include "state_archive.h"
#include "runtime/ee_scheduler.h"
#include <atomic>
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
    if (!hasPending.load(std::memory_order_acquire))
        return;
    std::string reason;
    if (!scheduler.stateSavable(&reason))
        return;
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
