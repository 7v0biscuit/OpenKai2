// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include "mqtt.h"
#include "net_socket.h"

namespace openkai2::netplay {

class Link {
  public:
    virtual ~Link() = default;
    virtual bool send_all(const void *data, size_t size) = 0;
    virtual bool receive_all(void *data, size_t size) = 0;
    virtual bool readable(int timeout_ms) = 0;
    virtual void shutdown() = 0;
    bool send_text(const std::string &text) { return send_all(text.data(), text.size()); }
    bool receive_line(std::string &line, size_t limit, int timeout_ms);
};

class SocketLink : public Link {
  public:
    explicit SocketLink(net::Socket &socket) : socket_(socket) {}
    bool send_all(const void *data, size_t size) override { return socket_.send_all(data, size); }
    bool receive_all(void *data, size_t size) override { return socket_.receive_all(data, size); }
    bool readable(int timeout_ms) override { return socket_.readable(timeout_ms); }
    void shutdown() override { socket_.shutdown(); }

  private:
    net::Socket &socket_;
};

std::string relay_topic(const std::string &channel, char side);
std::string knock_topic(const std::string &room_id);
bool valid_channel(const std::string &channel);

class RelayLink : public Link {
  public:
    ~RelayLink() override;
    bool open(const std::string &server, const std::string &channel, char side, std::string &error);
    bool knock(const std::string &room_id);
    bool send_all(const void *data, size_t size) override;
    bool receive_all(void *data, size_t size) override;
    bool readable(int timeout_ms) override;
    void shutdown() override;
    size_t receive_some(void *data, size_t size, int timeout_ms);
    bool alive() const;
    void close();

  private:
    struct Pending {
        std::string packet;
        std::chrono::steady_clock::time_point sent;
    };
    void received(const std::string &payload);
    void tick();
    bool deliverable() const { return !inbox_.empty() || peerClosed_ || dead_; }

    mqtt::Client client_;
    std::string out_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<uint8_t> inbox_;
    std::map<uint32_t, Pending> unacked_;
    std::map<uint32_t, std::string> early_;
    uint32_t nextSend_ = 1, expected_ = 1;
    bool peerClosed_ = false, dead_ = false, finished_ = false, ackDue_ = false;
    std::chrono::steady_clock::time_point heard_{}, lastSent_{};
    std::atomic<bool> stopping_{false};
    std::thread timer_;
};

class RelayBridge {
  public:
    ~RelayBridge();
    void start(RelayLink &link, net::Socket socket);
    bool running() const { return running_.load(); }
    void stop();

  private:
    RelayLink *link_ = nullptr;
    net::Socket socket_;
    std::thread up_, down_;
    std::atomic<bool> running_{false}, stopping_{false};
};

}
