// SPDX-License-Identifier: GPL-3.0-only
#include "discord_presence.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openkai2 {
namespace {
std::string quoted(const std::string &text) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += char(c);
        } else if (c < 32u) {
            result += "\\u00";
            result += hex[c >> 4u];
            result += hex[c & 15u];
        } else
            result += char(c);
    }
    return result + '"';
}
} // namespace
std::string discord::activity_payload(unsigned pid, const std::string &details, const std::string &state,
                                      long long started, unsigned long long nonce, bool clear) {
    std::string activity = "null";
    if (!clear)
        activity = "{\"details\":" + quoted(details) + ",\"state\":" + quoted(state) +
                   ",\"timestamps\":{\"start\":" + std::to_string(started) +
                   "},\"assets\":{\"large_image\":" + quoted(image_asset) +
                   ",\"large_text\":\"OpenKai2\"},\"instance\":false}";
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) +
           ",\"activity\":" + activity + "},\"nonce\":" + quoted(std::to_string(nonce)) + "}";
}
struct DiscordPresence::Impl {
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::atomic<PresenceStatus> connection{PresenceStatus::off};
    bool enabled = false, stopping = false;
    uint64_t revision = 0;
    std::string details = "In launcher", state = "Browsing games";
    std::wstring testPipe;
    std::function<bool()> enabledReader;
    const long long started =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    explicit Impl(std::wstring pipe) : testPipe(std::move(pipe)) {
#ifdef _WIN32
        worker = std::thread([this] { run(); });
#endif
    }
    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            changed.notify_all();
        }
        if (worker.joinable())
            worker.join();
    }
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::vector<uint8_t> incoming;
    bool ready = false;
    uint64_t sentRevision = ~uint64_t(0), nonce = 0;
    bool write_bytes(const std::vector<uint8_t> &bytes) {
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent)
            return false;
        DWORD written = 0;
        bool good = WriteFile(pipe, bytes.data(), DWORD(bytes.size()), &written, &operation) != FALSE;
        if (!good && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(operation.hEvent, 150u) == WAIT_OBJECT_0)
                good = GetOverlappedResult(pipe, &operation, &written, FALSE) != FALSE;
            else {
                CancelIoEx(pipe, &operation);
                GetOverlappedResult(pipe, &operation, &written, TRUE);
            }
        }
        CloseHandle(operation.hEvent);
        return good && written == bytes.size();
    }
    bool send(unsigned opcode, const std::string &body) {
        if (body.size() > 65536u)
            return false;
        std::vector<uint8_t> bytes(8u + body.size());
        const uint32_t fields[] = {opcode, uint32_t(body.size())};
        for (unsigned f = 0; f < 2u; ++f)
            for (unsigned i = 0; i < 4u; ++i)
                bytes[f * 4u + i] = uint8_t(fields[f] >> (i * 8u));
        std::copy(body.begin(), body.end(), bytes.begin() + 8u);
        return write_bytes(bytes);
    }
    void disconnect() {
        if (ready)
            std::cerr << "[discord] disconnected\n";
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
        }
        incoming.clear();
        ready = false;
        sentRevision = ~uint64_t(0);
    }
    bool connect() {
        for (unsigned i = 0; i < (testPipe.empty() ? 10u : 1u); ++i) {
            const auto name = testPipe.empty() ? L"\\\\?\\pipe\\discord-ipc-" + std::to_wstring(i) : testPipe;
            pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
                break;
        }
        if (pipe == INVALID_HANDLE_VALUE)
            return false;
        return send(0u, std::string("{\"v\":1,\"client_id\":\"") + discord::application_id + "\"}");
    }
    bool receive() {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
            return false;
        if (available) {
            if (incoming.size() + available > 131072u)
                return false;
            const auto old = incoming.size();
            incoming.resize(old + available);
            OVERLAPPED operation{};
            operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!operation.hEvent)
                return false;
            DWORD read = 0;
            bool good = ReadFile(pipe, incoming.data() + old, available, &read, &operation) != FALSE;
            if (!good && GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(operation.hEvent, 100u) == WAIT_OBJECT_0)
                    good = GetOverlappedResult(pipe, &operation, &read, FALSE) != FALSE;
                else {
                    CancelIoEx(pipe, &operation);
                    GetOverlappedResult(pipe, &operation, &read, TRUE);
                }
            }
            CloseHandle(operation.hEvent);
            if (!good)
                return false;
            incoming.resize(old + read);
        }
        while (incoming.size() >= 8u) {
            const auto word = [&](unsigned at) {
                return uint32_t(incoming[at]) | (uint32_t(incoming[at + 1u]) << 8u) |
                       (uint32_t(incoming[at + 2u]) << 16u) | (uint32_t(incoming[at + 3u]) << 24u);
            };
            const auto opcode = word(0), length = word(4);
            if (length > 65536u || opcode > 4u)
                return false;
            if (incoming.size() < 8u + length)
                break;
            const std::string body(incoming.begin() + 8u, incoming.begin() + 8u + length);
            if (opcode == 2u)
                return false;
            if (opcode == 3u && !send(4u, body))
                return false;
            if (opcode == 1u) {
                // Discord emits READY before accepting activity commands.
                if (body.find("\"READY\"") != std::string::npos) {
                    ready = true;
                    std::cerr << "[discord] connected\n";
                }
                if (body.find("\"ERROR\"") != std::string::npos) {
                    std::cerr
                        << "[discord] activity rejected; check application and art asset configuration\n";
                    return false;
                }
            }
            incoming.erase(incoming.begin(), incoming.begin() + 8u + length);
        }
        return true;
    }
    void run() {
        auto retry = std::chrono::steady_clock::time_point{};
        auto opened = retry;
        auto readAt = retry;
        for (;;) {
            const auto clock = std::chrono::steady_clock::now();
            if (clock >= readAt) {
                std::function<bool()> reader;
                {
                    std::lock_guard lock(mutex);
                    reader = enabledReader;
                }
                if (reader)
                    try {
                        const auto value = reader();
                        std::lock_guard lock(mutex);
                        enabled = value;
                    } catch (...) {
                    }
                readAt = clock + std::chrono::seconds(1);
            }
            bool active, stop;
            uint64_t current;
            std::string detail, text;
            {
                std::lock_guard lock(mutex);
                active = enabled;
                stop = stopping;
                current = revision;
                detail = details;
                text = state;
            }
            if (stop || !active) {
                if (ready)
                    send(1u,
                         discord::activity_payload(GetCurrentProcessId(), "", "", started, ++nonce, true));
                disconnect();
                connection.store(PresenceStatus::off);
                if (stop)
                    break;
            } else {
                connection.store(ready ? PresenceStatus::connected : PresenceStatus::waiting);
                const auto now = std::chrono::steady_clock::now();
                if (pipe == INVALID_HANDLE_VALUE && now >= retry) {
                    retry = now + std::chrono::seconds(5);
                    opened = now;
                    if (!connect())
                        disconnect();
                }
                if (pipe != INVALID_HANDLE_VALUE) {
                    if (!receive() || (!ready && now - opened > std::chrono::seconds(5)))
                        disconnect();
                    else if (ready && current != sentRevision) {
                        if (send(1u, discord::activity_payload(GetCurrentProcessId(), detail, text, started,
                                                               ++nonce)))
                            sentRevision = current;
                        else
                            disconnect();
                    }
                }
            }
            std::unique_lock lock(mutex);
            changed.wait_for(lock, std::chrono::milliseconds(100),
                             [&] { return stopping || enabled != active || revision != current; });
        }
    }
#endif
};
DiscordPresence::DiscordPresence(std::wstring pipe) : m(std::make_unique<Impl>(std::move(pipe))) {}
DiscordPresence::~DiscordPresence() = default;
void DiscordPresence::enable(bool value) {
    std::lock_guard lock(m->mutex);
    m->enabled = value;
    m->changed.notify_all();
}
void DiscordPresence::watch_enabled(std::function<bool()> reader) {
    std::lock_guard lock(m->mutex);
    m->enabledReader = std::move(reader);
    m->changed.notify_all();
}
void DiscordPresence::update(std::string details, std::string state) {
    if (details.size() > 128u)
        details.resize(128u);
    if (state.size() > 128u)
        state.resize(128u);
    std::lock_guard lock(m->mutex);
    if (m->details != details || m->state != state) {
        m->details = std::move(details);
        m->state = std::move(state);
        ++m->revision;
        m->changed.notify_all();
    }
}
PresenceStatus DiscordPresence::status() const noexcept { return m->connection.load(); }
} // namespace openkai2
