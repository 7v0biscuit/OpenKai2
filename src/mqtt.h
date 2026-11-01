// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "net_socket.h"

namespace openkai2::mqtt {

inline constexpr char default_server[] = "broker.emqx.io:1883";
inline constexpr uint16_t default_port = 1883;

bool parse_server(const std::string &text, std::string &host, uint16_t &port);
bool topic_matches(const std::string &filter, const std::string &topic);

struct Will {
    std::string topic, payload;
    bool retain = true;
};

class Client {
  public:
    using Handler = std::function<void(const std::string &topic, const std::string &payload)>;
    ~Client();
    bool connect(const std::string &server, const std::string &client_id, const Will &will, std::string &error);
    bool subscribe(const std::string &filter);
    bool publish(const std::string &topic, const std::string &payload, bool retain);
    void on_message(Handler handler);
    void close();
    bool connected() const { return connected_.load(); }

  private:
    void loop();
    bool send_packet(uint8_t header, const std::vector<uint8_t> &body);

    net::Socket socket_;
    std::thread reader_;
    std::mutex send_mutex_, handler_mutex_;
    Handler handler_;
    std::atomic<bool> connected_{false}, stopping_{false};
    uint16_t next_id_ = 1;
};

using PublishFilter = std::function<bool(const std::string &topic, const std::string &payload, const std::string &existing)>;
void serve(uint16_t port, const std::atomic<bool> &stopping, const std::function<void(uint16_t)> &ready = {},
           PublishFilter filter = {});

}
