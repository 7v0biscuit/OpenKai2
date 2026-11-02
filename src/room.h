// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include "nat.h"
#include "net_socket.h"
#include "relay.h"
#include <memory>

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
    uint16_t port = 0, local_port = 0;
    bool relayed = false;
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
    bool check_guest(Link &link, std::string &problem);
    bool join_over(Link &link, const std::string &channel);
    void bridge_host(std::string channel, uint16_t port);
    bool bridge_guest(const std::string &channel);
    void set(RoomPhase phase, const std::string &message);

    RoomOptions options_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    nat::PortMapping mapping_;
    mutable std::mutex mutex_;
    RoomStatus status_;
    net::Socket *active_ = nullptr;
    std::unique_ptr<RelayLink> roomLink_, gameLink_;
    RelayBridge bridge_;
    std::thread bridger_;
    void track(net::Socket *socket);
};

std::string new_session_key();

}
