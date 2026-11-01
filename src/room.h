// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include "nat.h"
#include "net_socket.h"

namespace openkai2::netplay {

inline constexpr char room_greeting[] = "OPENKAI2-ROOM 1";

struct RoomOptions {
    bool host = true;
    std::string address;
    uint16_t port = 7350;
    unsigned delay = 0;
    std::string name = "Player", password, invite_token, version;
    std::string lobby, room, room_id;
    bool listed = false, hidden = false, upnp = true;
};

enum class RoomPhase { opening, waiting, checking, ready, failed };

struct RoomStatus {
    RoomPhase phase = RoomPhase::opening;
    std::string message, opponent, key;
    unsigned delay = 0, ping_ms = 0;
    uint16_t port = 0;
};

class Room {
  public:
    explicit Room(RoomOptions options);
    ~Room();
    Room(const Room &) = delete;
    Room &operator=(const Room &) = delete;
    void start();
    void stop();
    void release_port();
    RoomStatus status() const;

  private:
    void run();
    void run_host();
    void run_join();
    bool check_guest(net::Socket &socket, std::string &problem);
    void set(RoomPhase phase, const std::string &message);

    RoomOptions options_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    nat::PortMapping mapping_;
    mutable std::mutex mutex_;
    RoomStatus status_;
    net::Socket *active_ = nullptr;
    void track(net::Socket *socket);
};

std::string new_session_key();

}
