// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace openkai2::net {

void ensure_network();

class Socket {
  public:
    Socket() = default;
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&other) noexcept;
    Socket &operator=(Socket &&other) noexcept;

    static Socket listen(uint16_t port);
    static Socket connect(const std::string &host, uint16_t port, std::string &error, int timeout_ms = 5000);
    Socket accept(int timeout_ms);
    bool valid() const { return handle_ >= 0; }
    bool send_all(const void *data, size_t size);
    bool receive_all(void *data, size_t size);
    long receive_some(void *data, size_t size);
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

struct Endpoint {
    uint32_t address = 0;
    uint16_t port = 0;
    bool valid() const { return address != 0 && port != 0; }
    bool operator==(const Endpoint &other) const { return address == other.address && port == other.port; }
    bool operator!=(const Endpoint &other) const { return !(*this == other); }
    std::string text() const;
    static std::optional<Endpoint> parse(const std::string &text);
    static std::optional<Endpoint> resolve(const std::string &host, uint16_t port);
};

class Datagram {
  public:
    Datagram() = default;
    ~Datagram();
    Datagram(const Datagram &) = delete;
    Datagram &operator=(const Datagram &) = delete;
    Datagram(Datagram &&other) noexcept;
    Datagram &operator=(Datagram &&other) noexcept;

    bool open(uint16_t port = 0);
    bool valid() const { return handle_ >= 0; }
    uint16_t local_port() const;
    bool send_to(const Endpoint &to, const void *data, size_t size);
    long receive_from(void *data, size_t size, Endpoint &from, int timeout_ms);
    void close();

  private:
    long long handle_ = -1;
};

}
