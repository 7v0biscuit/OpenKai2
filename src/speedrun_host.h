// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace openkai2::speedrun {

bool debugger_attached();
std::vector<std::string> running_processes();
std::filesystem::path executable_path();
bool relaunch(int argc, char **argv);
double wall_seconds();

class Watchdog {
  public:
    Watchdog();
    ~Watchdog();
    Watchdog(const Watchdog &) = delete;
    Watchdog &operator=(const Watchdog &) = delete;
    void flag(const std::string &reason);
    std::vector<std::string> flags() const;

  private:
    void run();
    mutable std::mutex mutex_;
    std::vector<std::string> flags_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

class LiveSplit {
  public:
    LiveSplit();
    ~LiveSplit();
    LiveSplit(const LiveSplit &) = delete;
    LiveSplit &operator=(const LiveSplit &) = delete;
    void send(const std::string &command);
    bool connected() const { return connected_.load(); }

  private:
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::string> queue_;
    std::atomic<bool> connected_{false};
    bool stop_ = false;
    std::thread thread_;
};

}
