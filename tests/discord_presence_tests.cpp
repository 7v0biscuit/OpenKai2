// SPDX-License-Identifier: GPL-3.0-only
#include "discord_presence.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
void require(bool b, const char *why) {
    if (!b)
        throw std::runtime_error(why);
}
struct Handle {
    HANDLE h;
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE && h)
            CloseHandle(h);
    }
};
bool io(HANDLE pipe, bool write, void *data, DWORD count) {
    Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED operation{};
    operation.hEvent = event.h;
    DWORD done = 0;
    bool good = (write ? WriteFile(pipe, data, count, &done, &operation)
                       : ReadFile(pipe, data, count, &done, &operation)) != FALSE;
    if (!good && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(event.h, 3000u) == WAIT_OBJECT_0)
            good = GetOverlappedResult(pipe, &operation, &done, FALSE) != FALSE;
        else {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &done, TRUE);
        }
    }
    return good && done == count;
}
void connected(HANDLE pipe) {
    Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED operation{};
    operation.hEvent = event.h;
    if (ConnectNamedPipe(pipe, &operation))
        return;
    if (GetLastError() == ERROR_PIPE_CONNECTED)
        return;
    require(GetLastError() == ERROR_IO_PENDING, "Test pipe could not accept a connection");
    if (WaitForSingleObject(event.h, 3000u) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &operation);
        DWORD done = 0;
        GetOverlappedResult(pipe, &operation, &done, TRUE);
        throw std::runtime_error("Discord client did not connect");
    }
}
std::vector<uint8_t> frame(unsigned op, const std::string &data) {
    std::vector<uint8_t> out(8u + data.size());
    uint32_t size = uint32_t(data.size());
    std::memcpy(out.data(), &op, 4u);
    std::memcpy(out.data() + 4u, &size, 4u);
    std::copy(data.begin(), data.end(), out.begin() + 8u);
    return out;
}
std::pair<unsigned, std::string> read_frame(HANDLE pipe) {
    uint32_t header[2]{};
    require(io(pipe, false, header, 8u), "Missing IPC header");
    require(header[1] <= 65536u, "Oversized outgoing IPC message");
    std::string body(header[1], 0);
    if (!body.empty())
        require(io(pipe, false, body.data(), header[1]), "Truncated outgoing IPC message");
    return {header[0], body};
}
void send(HANDLE pipe, unsigned op, const std::string &text) {
    auto data = frame(op, text);
    require(io(pipe, true, data.data(), DWORD(data.size())), "Cannot send IPC fixture");
}
} // namespace
int main() {
    try {
        const auto base = L"\\\\?\\pipe\\OpenKai2.DiscordTest." + std::to_wstring(GetCurrentProcessId());
        Handle server{CreateNamedPipeW(base.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 4096, 4096, 0, nullptr)};
        require(server.h != INVALID_HANDLE_VALUE, "Cannot create local Discord fixture");
        {
            std::atomic<bool> allowed{true};
            openkai2::DiscordPresence presence(base);
            presence.update("Launcher \"test\"\n", "Browsing games");
            presence.enable(true);
            presence.watch_enabled([&] { return allowed.load(); });
            connected(server.h);
            const auto hello = read_frame(server.h);
            require(hello.first == 0u &&
                        hello.second.find(openkai2::discord::application_id) != std::string::npos,
                    "Wrong Discord application handshake");
            send(server.h, 3u, std::string("ping\0probe", 10));
            const auto pong = read_frame(server.h);
            require(pong.first == 4u && pong.second == std::string("ping\0probe", 10),
                    "IPC ping payload did not survive its pong");
            auto ready = frame(1u, "{\"evt\":\"READY\",\"data\":{}}");
            require(io(server.h, true, ready.data(), 3u), "Cannot split READY header");
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            DWORD available = 0;
            PeekNamedPipe(server.h, nullptr, 0, nullptr, &available, nullptr);
            require(!available, "Activity was sent before a complete READY frame");
            require(io(server.h, true, ready.data() + 3u, DWORD(ready.size() - 3u)),
                    "Cannot finish READY frame");
            auto activity = read_frame(server.h);
            require(activity.first == 1u && activity.second.find("SET_ACTIVITY") != std::string::npos,
                    "Missing launcher activity");
            require(activity.second.find("Launcher \\\"test\\\"\\u000a") != std::string::npos,
                    "Activity text was not JSON escaped");
            require(activity.second.find("\"large_image\":\"openkai2\"") != std::string::npos,
                    "Activity does not use the executable's shared icon asset");
            presence.update("Launcher \"test\"\n", "Browsing games");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            PeekNamedPipe(server.h, nullptr, 0, nullptr, &available, nullptr);
            require(!available, "Unchanged activity was repeatedly sent");
            presence.update("Playing Budokai 2", "In game");
            activity = read_frame(server.h);
            require(activity.second.find("Playing Budokai 2") != std::string::npos,
                    "Game activity did not replace launcher state");
            allowed = false;
            activity = read_frame(server.h);
            require(activity.second.find("\"activity\":null") != std::string::npos,
                    "Saved preference change did not clear the game activity");
        }
        const auto start = std::chrono::steady_clock::now();
        {
            openkai2::DiscordPresence missing(base + L".missing");
            missing.enable(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(700),
                "Missing Discord client delayed shutdown");
        const auto blockedName = base + L".blocked";
        Handle blocked{CreateNamedPipeW(blockedName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 1, 1, 0, nullptr)};
        require(blocked.h != INVALID_HANDLE_VALUE, "Cannot create stalled Discord fixture");
        auto client = std::make_unique<openkai2::DiscordPresence>(blockedName);
        client->enable(true);
        connected(blocked.h);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto stopped = std::chrono::steady_clock::now();
        client.reset();
        require(std::chrono::steady_clock::now() - stopped < std::chrono::milliseconds(700),
                "Unresponsive Discord peer delayed shutdown");
        std::cout << "Discord local IPC checks passed: app ID, fragmented READY, ping/pong, JSON escaping, "
                     "shared icon, scene updates, coalescing, live preference changes, and bounded "
                     "missing/stalled-client shutdown.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
