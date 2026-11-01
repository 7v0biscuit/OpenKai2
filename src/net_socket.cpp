// SPDX-License-Identifier: GPL-3.0-only
#include "net_socket.h"
#include <cstring>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#define OK_INVALID INVALID_SOCKET
#define OK_CLOSE closesocket
#define OK_SHUT SD_BOTH
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define OK_INVALID (-1)
#define OK_CLOSE ::close
#define OK_SHUT SHUT_RDWR
#endif

namespace openkai2::net {
namespace {
void startup() {
#ifdef _WIN32
    static const bool ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!ready)
        throw std::runtime_error("network is not available");
#endif
}

SocketHandle raw(long long handle) { return static_cast<SocketHandle>(handle); }

void no_delay(SocketHandle handle) {
    int on = 1;
    setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&on), sizeof on);
}

bool wait_readable(SocketHandle handle, int timeout_ms) {
#ifdef _WIN32
    WSAPOLLFD entry{};
    entry.fd = handle;
    entry.events = POLLRDNORM;
    return WSAPoll(&entry, 1, timeout_ms) > 0;
#else
    pollfd entry{};
    entry.fd = handle;
    entry.events = POLLIN;
    return ::poll(&entry, 1, timeout_ms) > 0;
#endif
}
}

void ensure_network() { startup(); }

Socket::~Socket() { close(); }

Socket::Socket(Socket &&other) noexcept : handle_(std::exchange(other.handle_, -1)) {}

Socket &Socket::operator=(Socket &&other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

Socket Socket::listen(uint16_t port) {
    startup();
    const SocketHandle handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == OK_INVALID)
        throw std::runtime_error("cannot open a network socket");
#ifndef _WIN32
    int on = 1;
    setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&on), sizeof on);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(handle, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 || ::listen(handle, 1) != 0) {
        OK_CLOSE(handle);
        throw std::runtime_error("port " + std::to_string(port) + " is in use or blocked");
    }
    return Socket(static_cast<long long>(handle));
}

Socket Socket::connect(const std::string &host, uint16_t port, std::string &error) {
    startup();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "cannot find " + host;
        return Socket();
    }
    for (addrinfo *entry = found; entry; entry = entry->ai_next) {
        const SocketHandle handle = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (handle == OK_INVALID)
            continue;
        if (::connect(handle, entry->ai_addr, int(entry->ai_addrlen)) == 0) {
            freeaddrinfo(found);
            no_delay(handle);
            return Socket(static_cast<long long>(handle));
        }
        OK_CLOSE(handle);
    }
    freeaddrinfo(found);
    error = "cannot reach " + host + ":" + std::to_string(port);
    return Socket();
}

Socket Socket::accept(int timeout_ms) {
    if (!valid() || !wait_readable(raw(handle_), timeout_ms))
        return Socket();
    const SocketHandle handle = ::accept(raw(handle_), nullptr, nullptr);
    if (handle == OK_INVALID)
        return Socket();
    no_delay(handle);
    return Socket(static_cast<long long>(handle));
}

bool Socket::send_all(const void *data, size_t size) {
    const char *bytes = static_cast<const char *>(data);
    while (size && valid()) {
        const int chunk = int(size > (1u << 20) ? (1u << 20) : size);
#ifdef _WIN32
        const int sent = ::send(raw(handle_), bytes, chunk, 0);
#else
        const int sent = int(::send(raw(handle_), bytes, size_t(chunk), MSG_NOSIGNAL));
#endif
        if (sent <= 0)
            return false;
        bytes += sent;
        size -= size_t(sent);
    }
    return size == 0;
}

bool Socket::receive_all(void *data, size_t size) {
    char *bytes = static_cast<char *>(data);
    while (size && valid()) {
        const int chunk = int(size > (1u << 20) ? (1u << 20) : size);
        const int got = int(::recv(raw(handle_), bytes, chunk, 0));
        if (got <= 0)
            return false;
        bytes += got;
        size -= size_t(got);
    }
    return size == 0;
}

bool Socket::readable(int timeout_ms) { return valid() && wait_readable(raw(handle_), timeout_ms); }

void Socket::shutdown() {
    if (valid())
        ::shutdown(raw(handle_), OK_SHUT);
}

void Socket::close() {
    if (valid())
        OK_CLOSE(raw(handle_));
    handle_ = -1;
}

std::string Socket::peer_address() const {
    sockaddr_storage address{};
#ifdef _WIN32
    int size = sizeof address;
#else
    socklen_t size = sizeof address;
#endif
    if (!valid() || getpeername(raw(handle_), reinterpret_cast<sockaddr *>(&address), &size) != 0)
        return {};
    char text[INET6_ADDRSTRLEN]{};
    if (address.ss_family == AF_INET)
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(&address)->sin_addr, text, sizeof text);
    else if (address.ss_family == AF_INET6) {
        const auto *v6 = reinterpret_cast<sockaddr_in6 *>(&address);
        inet_ntop(AF_INET6, &v6->sin6_addr, text, sizeof text);
        std::string value = text;
        if (value.rfind("::ffff:", 0) == 0)
            return value.substr(7);
        return value;
    }
    return text;
}

bool Socket::receive_line(std::string &line, size_t limit, int timeout_ms) {
    line.clear();
    while (line.size() < limit) {
        if (!readable(timeout_ms))
            return false;
        char c = 0;
        if (!receive_all(&c, 1))
            return false;
        if (c == '\n')
            return true;
        if (c != '\r')
            line.push_back(c);
    }
    return false;
}

uint16_t Socket::local_port() const {
    sockaddr_in address{};
#ifdef _WIN32
    int size = sizeof address;
#else
    socklen_t size = sizeof address;
#endif
    if (!valid() || getsockname(raw(handle_), reinterpret_cast<sockaddr *>(&address), &size) != 0)
        return 0;
    return ntohs(address.sin_port);
}

}
