// SPDX-License-Identifier: GPL-3.0-only
#include "speedrun_host.h"
#include "speedrun.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace openkai2::speedrun {
namespace {
double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#ifdef _WIN32
std::string narrow(const wchar_t *text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(size_t(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}
using Socket = SOCKET;
constexpr Socket no_socket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket no_socket = -1;
void close_socket(Socket s) { ::close(s); }
#endif
#ifdef MSG_NOSIGNAL
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif
}

double wall_seconds() {
#ifdef _WIN32
    FILETIME time{};
    GetSystemTimePreciseAsFileTime(&time);
    const uint64_t ticks = (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    return double(ticks) / 1e7;
#else
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
}

bool debugger_attached() {
#ifdef _WIN32
    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    return IsDebuggerPresent() || remote;
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
        if (line.rfind("TracerPid:", 0) == 0)
            return std::stol(line.substr(10)) != 0;
    return false;
#endif
}

std::vector<std::string> running_processes() {
    std::vector<std::string> names;
#ifdef _WIN32
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return names;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        names.push_back(narrow(entry.szExeFile));
    CloseHandle(snapshot);
#else
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator("/proc", error)) {
        std::ifstream comm(entry.path() / "comm");
        std::string name;
        if (comm && std::getline(comm, name))
            names.push_back(name);
    }
#endif
    return names;
}

std::filesystem::path executable_path() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    path.resize(size);
    return path;
#else
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error);
#endif
}

bool relaunch(int argc, char **argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::wstring command = GetCommandLineW();
    const auto exe = executable_path();
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    if (argc < 1)
        return false;
    std::vector<char *> arguments(argv, argv + argc);
    arguments.push_back(nullptr);
    const auto exe = executable_path();
    execv(exe.empty() ? argv[0] : exe.c_str(), arguments.data());
    return false;
#endif
}

Watchdog::Watchdog() : thread_([this] { run(); }) {}

Watchdog::~Watchdog() {
    stop_ = true;
    if (thread_.joinable())
        thread_.join();
}

void Watchdog::flag(const std::string &reason) {
    std::lock_guard lock(mutex_);
    if (std::find(flags_.begin(), flags_.end(), reason) == flags_.end())
        flags_.push_back(reason);
}

std::vector<std::string> Watchdog::flags() const {
    std::lock_guard lock(mutex_);
    return flags_;
}

void Watchdog::run() {
    ClockCheck clock;
    unsigned tick = 0;
    while (!stop_) {
        if (debugger_attached())
            flag("a debugger was attached");
        if (clock.sample(steady_seconds(), wall_seconds()))
            flag("the system clock and game clock ran at different speeds");
        if (tick % 10 == 0)
            for (const auto &name : running_processes())
                if (suspicious_process(name))
                    flag(name + " was running");
        ++tick;
        for (int i = 0; i < 10 && !stop_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

LiveSplit::LiveSplit() : thread_([this] { run(); }) {}

LiveSplit::~LiveSplit() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void LiveSplit::send(const std::string &command) {
    {
        std::lock_guard lock(mutex_);
        if (queue_.size() < 64)
            queue_.push_back(command);
    }
    wake_.notify_all();
}

void LiveSplit::run() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return;
#endif
    Socket socket = no_socket;
    auto next_attempt = std::chrono::steady_clock::now();
    while (true) {
        std::vector<std::string> commands;
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_ || !queue_.empty(); });
            if (stop_)
                break;
            commands.swap(queue_);
        }
        if (socket == no_socket && std::chrono::steady_clock::now() >= next_attempt) {
            next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(16834);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (socket != no_socket &&
                ::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
                close_socket(socket);
                socket = no_socket;
            }
            connected_ = socket != no_socket;
        }
        if (socket == no_socket)
            continue;
        for (const auto &command : commands) {
            const std::string line = command + "\r\n";
            if (::send(socket, line.data(), int(line.size()), send_flags) != int(line.size())) {
                close_socket(socket);
                socket = no_socket;
                connected_ = false;
                break;
            }
        }
    }
    if (socket != no_socket)
        close_socket(socket);
#ifdef _WIN32
    WSACleanup();
#endif
}

}
