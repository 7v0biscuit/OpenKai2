// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace openkai2::net {

class Socket {
  public:
    Socket() = default;
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&other) noexcept;
    Socket &operator=(Socket &&other) noexcept;

    static Socket listen(uint16_t port);
    static Socket connect(const std::string &host, uint16_t port, std::string &error);
    Socket accept(int timeout_ms);
    bool valid() const { return handle_ >= 0; }
    bool send_all(const void *data, size_t size);
    bool receive_all(void *data, size_t size);
    bool readable(int timeout_ms);
    void shutdown();
    void close();
    uint16_t local_port() const;
    std::string peer_address() const;
    bool receive_line(std::string &line, size_t limit, int timeout_ms);
    bool send_text(const std::string &text) { return send_all(text.data(), text.size()); }

  private:
    explicit Socket(long long handle) : handle_(handle) {}
    long long handle_ = -1;
};

}
