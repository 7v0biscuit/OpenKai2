// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "net_socket.h"

namespace openkai2::lobby {

inline constexpr uint16_t default_port = 7351;
inline constexpr size_t max_rooms = 500, max_rooms_per_address = 4, max_line = 512;

struct Room {
    uint64_t id = 0;
    std::string name, host, version, address;
    uint16_t port = 0;
    bool locked = false;
};

std::string clean(std::string text, size_t limit);
std::vector<std::string> split(const std::string &line);
std::string room_line(const Room &room);
std::optional<Room> parse_room(const std::string &line);
bool parse_server(const std::string &text, std::string &host, uint16_t &port);

class Directory {
  public:
    std::optional<uint64_t> add(Room room);
    void remove(uint64_t id);
    std::vector<Room> list() const;

  private:
    mutable std::mutex mutex_;
    std::map<uint64_t, Room> rooms_;
    uint64_t next_ = 1;
};

std::string handle_list(const Directory &directory, const std::vector<std::string> &fields);

std::vector<Room> fetch_rooms(const std::string &server, std::string &error);

class Registration {
  public:
    bool open(const std::string &server, const Room &room, std::string &error);
    void keep_alive();
    void close();
    bool active() const { return socket_.valid(); }

  private:
    net::Socket socket_;
    std::chrono::steady_clock::time_point last_{};
};

void serve(uint16_t port, const std::atomic<bool> &stopping, const std::function<void(uint16_t)> &ready = {});

}
