// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "net_socket.h"

namespace openkai2::netplay {

inline constexpr uint32_t protocol_version = 3;
inline constexpr uint16_t default_port = 7350;
inline constexpr unsigned max_delay = 8;
inline constexpr uint64_t hash_interval = 120;

using Input = std::array<uint8_t, 6>;
Input neutral_input();
unsigned auto_delay(double rtt_ms);
std::optional<unsigned> probe_room(const std::string &address, uint16_t port, int timeout_ms);

std::vector<uint8_t> compress(const std::vector<uint8_t> &data);
bool decompress(const std::vector<uint8_t> &data, std::vector<uint8_t> &out);

struct CardFile {
    std::string path;
    std::vector<uint8_t> data;
};
std::vector<CardFile> read_card_files(const std::filesystem::path &root);
void write_card_files(const std::filesystem::path &root, const std::vector<CardFile> &files);
std::vector<uint8_t> pack_snapshot(const std::vector<CardFile> &files, const std::vector<uint8_t> &state);
bool unpack_snapshot(const std::vector<uint8_t> &packed, std::vector<CardFile> &files, std::vector<uint8_t> &state);

struct Options {
    bool host = true;
    std::string address;
    uint16_t port = default_port;
    unsigned delay = 0;
    std::string name = "Player";
    std::string identity;
    std::string password;
    std::string lobby, room, room_id, version;
    bool listed = false, hidden = false, upnp = true;
    std::string invite_token;
};

struct Machine {
    std::function<uint64_t()> tick;
    std::function<std::vector<uint8_t>()> save;
    std::function<void(const std::vector<uint8_t> &)> load;
    std::function<uint64_t()> hash;
    std::function<Input()> local_input;
    std::function<std::vector<CardFile>()> read_cards;
    std::function<void(const std::vector<CardFile> &)> write_cards;
    std::function<bool()> stopping;
};

enum class Phase { connecting, syncing, playing, ended };

struct Status {
    Phase phase = Phase::connecting;
    std::string opponent, message;
    unsigned delay = 0, ping_ms = 0;
    bool waiting = false, desynced = false;
    uint64_t desync_tick = 0;
};

class Session {
  public:
    Session(Options options, Machine machine);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    void start();
    void stop();
    bool inputs(Input &port0, Input &port1);
    void state_point();
    Status status() const;
    uint16_t listening_port() const { return listeningPort_.load(); }

  private:
    void run();
    int handshake();
    void read_loop();
    void write_loop();
    void send(uint8_t type, std::vector<uint8_t> payload);
    void end(const std::string &message);
    bool wait_remote(uint64_t tick, Input &out);

    Options options_;
    Machine machine_;
    net::Socket listener_, socket_;
    std::thread worker_, writer_;
    std::atomic<bool> stopping_{false};
    std::atomic<uint16_t> listeningPort_{0};

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    Status status_;
    std::deque<std::vector<uint8_t>> outgoing_;
    std::condition_variable outgoingReady_;
    std::map<uint64_t, Input> remote_, local_;
    std::map<uint64_t, uint64_t> remoteHashes_, localHashes_;
    std::vector<uint8_t> snapshot_;
    bool snapshotReady_ = false, wantSnapshot_ = false, locked_ = false;
    uint64_t startTick_ = 0, lastSent_ = 0, cachedTick_ = ~0ull;
    Input cached0_{}, cached1_{};
    std::chrono::steady_clock::time_point pingSent_{};
};

Session *active();
void set_active(Session *session);

}
