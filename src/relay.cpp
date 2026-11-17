// SPDX-License-Identifier: GPL-3.0-only
#include "relay.h"
#include "netplay.h"
#include "online.h"
#include "room.h"
#include "nat.h"
#include <random>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace openkai2::netplay {
namespace {
using clock = std::chrono::steady_clock;
constexpr size_t chunk_bytes = 8000, window = 96, early_limit = 1024;
constexpr std::chrono::milliseconds give_up{20000};

void put32(std::string &out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(char(uint8_t(value >> shift)));
}
uint32_t get32(const std::string &in, size_t at) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
        value = (value << 8) | uint8_t(in[at + i]);
    return value;
}
}

bool Link::receive_line(std::string &line, size_t limit, int timeout_ms) {
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

std::string relay_topic(const std::string &channel, char side) {
    return std::string(online::prefix) + "relay/" + channel + "/" + side;
}
std::string knock_topic(const std::string &room_id) { return std::string(online::prefix) + "relay/" + room_id + "/knock"; }
bool valid_channel(const std::string &channel) {
    return channel.size() >= 8 && channel.size() <= 40 &&
           std::all_of(channel.begin(), channel.end(), [](char c) { return std::isxdigit(uint8_t(c)) || c == 'g'; });
}

RelayLink::~RelayLink() { close(); }

bool RelayLink::open(const std::string &server, const std::string &channel, char side, std::string &error) {
    if (!valid_channel(channel)) {
        error = "bad relay channel";
        return false;
    }
    out_ = relay_topic(channel, side);
    client_.on_message([this](const std::string &, const std::string &payload) { received(payload); });
    if (!client_.connect(server, "ok2r-" + new_session_key().substr(0, 16), {}, error))
        return false;
    if (!client_.subscribe(relay_topic(channel, side == 'a' ? 'b' : 'a'))) {
        error = "the online service refused the relay";
        client_.close();
        return false;
    }
    heard_ = lastSent_ = clock::now();
    stopping_ = false;
    timer_ = std::thread([this] {
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            tick();
        }
    });
    return true;
}

bool RelayLink::open_direct(net::Datagram socket, net::Endpoint peer) {
    if (!socket.valid() || !peer.valid())
        return false;
    udp_ = std::move(socket);
    peer_ = peer;
    chunk_ = 1200;
    heard_ = lastSent_ = clock::now();
    stopping_ = false;
    receiver_ = std::thread([this] {
        std::vector<char> buffer(2048);
        while (!stopping_) {
            net::Endpoint from;
            const long got = udp_.receive_from(buffer.data(), buffer.size(), from, 50);
            if (got > 0 && from == peer_)
                received(std::string(buffer.data(), size_t(got)));
        }
    });
    timer_ = std::thread([this] {
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            tick();
        }
    });
    return true;
}

bool RelayLink::transmit(const std::string &packet) {
    if (udp_.valid())
        return udp_.send_to(peer_, packet.data(), packet.size());
    return client_.publish(out_, packet, false);
}

bool RelayLink::knock(const std::string &room_id) {
    const auto channel = out_.substr(std::string(online::prefix).size() + 6);
    return client_.publish(knock_topic(room_id), channel.substr(0, channel.find('/')), false);
}

std::chrono::microseconds RelayLink::resend_after() const {
    const auto floor = udp_.valid() ? std::chrono::microseconds(30000) : std::chrono::microseconds(100000);
    if (smoothed_.count() == 0)
        return std::chrono::microseconds(400000);
    return std::clamp(smoothed_ * 3 / 2 + std::chrono::microseconds(15000), floor, std::chrono::microseconds(400000));
}

void RelayLink::received(const std::string &payload) {
    if (payload.size() < 5)
        return;
    const char type = payload[0];
    const uint32_t seq = get32(payload, 1);
    std::string ack;
    {
        std::lock_guard lock(mutex_);
        const auto now = clock::now();
        heard_ = now;
        if (type == 'A') {
            for (auto it = unacked_.begin(); it != unacked_.end() && it->first <= seq;) {
                if (!it->second.resent) {
                    const auto sample = std::chrono::duration_cast<std::chrono::microseconds>(now - it->second.sent);
                    smoothed_ = smoothed_.count() ? (smoothed_ * 7 + sample) / 8 : sample;
                }
                it = unacked_.erase(it);
            }
            changed_.notify_all();
            return;
        }
        if (type != 'D' && type != 'F')
            return;
        ackDue_ = true;
        receive_data(seq, payload);
        if (udp_.valid()) {
            ack.assign(1, 'A');
            put32(ack, expected_ - 1);
            ackDue_ = false;
            lastSent_ = now;
        }
    }
    if (!ack.empty())
        transmit(ack);
}

void RelayLink::receive_data(uint32_t seq, const std::string &payload) {
    if (seq < expected_ || early_.size() >= early_limit)
        return;
    early_.emplace(seq, payload);
    bool delivered = false;
    for (auto it = early_.find(expected_); it != early_.end(); it = early_.find(expected_)) {
        if (it->second[0] == 'F')
            peerClosed_ = true;
        else
            inbox_.insert(inbox_.end(), it->second.begin() + 5, it->second.end());
        early_.erase(it);
        ++expected_;
        delivered = true;
    }
    if (delivered)
        changed_.notify_all();
}

void RelayLink::tick() {
    std::vector<std::string> outgoing;
    {
        std::lock_guard lock(mutex_);
        const auto now = clock::now();
        if (!dead_ && now - heard_ > give_up) {
            dead_ = true;
            changed_.notify_all();
        }
        unsigned resent = 0;
        const auto wait = resend_after();
        for (auto &[seq, pending] : unacked_)
            if (now - pending.sent > wait && resent < 32) {
                pending.sent = now;
                pending.resent = true;
                outgoing.push_back(pending.packet);
                ++resent;
            }
        if (ackDue_ || now - lastSent_ > std::chrono::seconds(1)) {
            std::string ack(1, 'A');
            put32(ack, expected_ - 1);
            outgoing.push_back(ack);
            ackDue_ = false;
        }
        if (!outgoing.empty())
            lastSent_ = now;
    }
    for (const auto &packet : outgoing)
        transmit(packet);
}

bool RelayLink::send_all(const void *data, size_t size) {
    const auto *bytes = static_cast<const char *>(data);
    while (size) {
        const size_t count = std::min(size, chunk_);
        std::string packet(1, 'D');
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait_for(lock, give_up, [&] { return unacked_.size() < window || dead_ || finished_; }) ||
                dead_ || finished_)
                return false;
            put32(packet, nextSend_);
            packet.append(bytes, count);
            unacked_[nextSend_++] = Pending{packet, clock::now()};
            lastSent_ = clock::now();
        }
        if (!transmit(packet))
            return false;
        bytes += count;
        size -= count;
    }
    return true;
}

bool RelayLink::readable(int timeout_ms) {
    std::unique_lock lock(mutex_);
    changed_.wait_for(lock, std::chrono::milliseconds(std::max(timeout_ms, 0)), [&] { return deliverable(); });
    return !inbox_.empty();
}

size_t RelayLink::receive_some(void *data, size_t size, int timeout_ms) {
    std::unique_lock lock(mutex_);
    changed_.wait_for(lock, std::chrono::milliseconds(std::max(timeout_ms, 0)), [&] { return deliverable(); });
    const size_t count = std::min(size, inbox_.size());
    std::copy_n(inbox_.begin(), count, static_cast<uint8_t *>(data));
    inbox_.erase(inbox_.begin(), inbox_.begin() + std::ptrdiff_t(count));
    return count;
}

bool RelayLink::receive_all(void *data, size_t size) {
    auto *bytes = static_cast<uint8_t *>(data);
    std::unique_lock lock(mutex_);
    while (size) {
        changed_.wait(lock, [&] { return deliverable(); });
        if (inbox_.empty())
            return false;
        const size_t count = std::min(size, inbox_.size());
        std::copy_n(inbox_.begin(), count, bytes);
        inbox_.erase(inbox_.begin(), inbox_.begin() + std::ptrdiff_t(count));
        bytes += count;
        size -= count;
    }
    return true;
}

void RelayLink::shutdown() {
    std::string packet(1, 'F');
    {
        std::lock_guard lock(mutex_);
        if (finished_ || dead_)
            return;
        finished_ = true;
        put32(packet, nextSend_);
        unacked_[nextSend_++] = Pending{packet, clock::now()};
    }
    transmit(packet);
}

bool RelayLink::alive() const {
    std::lock_guard lock(mutex_);
    return !dead_ && !peerClosed_;
}

void RelayLink::close() {
    stopping_ = true;
    if (timer_.joinable())
        timer_.join();
    if (receiver_.joinable())
        receiver_.join();
    client_.close();
    udp_.close();
    std::lock_guard lock(mutex_);
    dead_ = true;
    changed_.notify_all();
}

namespace {
std::optional<net::Endpoint> public_endpoint(net::Datagram &socket) {
    std::random_device device;
    for (const auto &[host, port] : {std::pair<const char *, uint16_t>{"stun.l.google.com", 19302},
                                     {"stun1.l.google.com", 19302}, {"stun.cloudflare.com", 3478}}) {
        const auto server = net::Endpoint::resolve(host, port);
        if (!server)
            continue;
        uint8_t transaction[12];
        for (auto &byte : transaction)
            byte = uint8_t(device());
        const auto request = nat::stun_request(transaction);
        for (int attempt = 0; attempt < 2; ++attempt) {
            socket.send_to(*server, request.data(), request.size());
            const auto until = clock::now() + std::chrono::milliseconds(700);
            while (clock::now() < until) {
                uint8_t buffer[576];
                net::Endpoint from;
                const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(until - clock::now()).count());
                const long got = socket.receive_from(buffer, sizeof buffer, from, std::max(left, 1));
                if (got <= 0)
                    break;
                if (from != *server)
                    continue;
                const std::vector<uint8_t> reply(buffer, buffer + got);
                if (const auto mapped = nat::stun_mapped_endpoint(reply, transaction))
                    return net::Endpoint::parse(*mapped);
            }
        }
    }
    return std::nullopt;
}

std::string endpoint_text(const std::optional<net::Endpoint> &endpoint) { return endpoint && endpoint->valid() ? endpoint->text() : "-"; }
}

std::unique_ptr<RelayLink> direct_link(Link &control, std::string &route) {
    net::Datagram socket;
    std::optional<net::Endpoint> outside, inside, loopback;
    std::random_device device;
    const std::string token = std::to_string(device() % 900000000u + 100000000u);
    if (socket.open(0)) {
        const uint16_t port = socket.local_port();
        outside = public_endpoint(socket);
        inside = net::Endpoint::parse(nat::local_address() + ":" + std::to_string(port));
        loopback = net::Endpoint::parse("127.0.0.1:" + std::to_string(port));
    }
    if (!control.send_text("P " + token + " " + endpoint_text(outside) + " " + endpoint_text(inside) + " " +
                           endpoint_text(loopback) + "\n")) {
        route = "the relay closed";
        return nullptr;
    }
    std::string line;
    if (!control.receive_line(line, 256, 15000) || line.rfind("P ", 0) != 0) {
        route = "the other player did not offer a direct connection";
        return nullptr;
    }
    std::istringstream words(line.substr(2));
    std::string theirToken, word;
    words >> theirToken;
    std::vector<net::Endpoint> targets;
    while (words >> word)
        if (const auto endpoint = net::Endpoint::parse(word); endpoint && endpoint->valid())
            targets.push_back(*endpoint);
    bool heard = false;
    net::Endpoint chosen;
    if (socket.valid() && !targets.empty() && !theirToken.empty()) {
        const std::string hello = "H" + theirToken, answer = "K" + theirToken;
        const auto began = clock::now();
        auto lastSend = began - std::chrono::seconds(1);
        clock::time_point heardAt{};
        while (clock::now() - began < std::chrono::seconds(4) && !(heard && clock::now() - heardAt > std::chrono::milliseconds(600))) {
            if (clock::now() - lastSend >= std::chrono::milliseconds(100)) {
                for (const auto &target : targets)
                    socket.send_to(target, hello.data(), hello.size());
                if (heard)
                    socket.send_to(chosen, answer.data(), answer.size());
                lastSend = clock::now();
            }
            char buffer[64];
            net::Endpoint from;
            const long got = socket.receive_from(buffer, sizeof buffer, from, 20);
            if (got <= 1 || (buffer[0] != 'H' && buffer[0] != 'K') || std::string(buffer + 1, size_t(got - 1)) != token)
                continue;
            if (!heard) {
                heard = true;
                heardAt = clock::now();
                chosen = from;
            }
            if (buffer[0] == 'H')
                socket.send_to(from, answer.data(), answer.size());
        }
    }
    if (!control.send_text(heard ? "U\n" : "R\n") || !control.receive_line(line, 16, 15000)) {
        route = "the relay closed";
        return nullptr;
    }
    if (!heard || line != "U") {
        route = !socket.valid() ? "no UDP socket" : targets.empty() ? "no address to try" : "the routers did not let a direct connection through";
        return nullptr;
    }
    auto link = std::make_unique<RelayLink>();
    if (!link->open_direct(std::move(socket), chosen)) {
        route = "the direct connection could not start";
        return nullptr;
    }
    route = chosen.text();
    return link;
}

RelayBridge::~RelayBridge() { stop(); }

void RelayBridge::start(RelayLink &link, net::Socket socket) {
    stop();
    link_ = &link;
    socket_ = std::move(socket);
    stopping_ = false;
    running_ = true;
    up_ = std::thread([this] {
        std::vector<uint8_t> buffer(chunk_bytes);
        while (!stopping_) {
            if (!socket_.readable(50))
                continue;
            const long got = socket_.receive_some(buffer.data(), buffer.size());
            if (got <= 0 || !link_->send_all(buffer.data(), size_t(got)))
                break;
        }
        link_->shutdown();
        running_ = false;
    });
    down_ = std::thread([this] {
        std::vector<uint8_t> buffer(chunk_bytes * 4);
        while (!stopping_) {
            const size_t got = link_->receive_some(buffer.data(), buffer.size(), 50);
            if (!got) {
                if (!link_->alive())
                    break;
                continue;
            }
            if (!socket_.send_all(buffer.data(), got))
                break;
        }
        socket_.shutdown();
        running_ = false;
    });
}

void RelayBridge::stop() {
    stopping_ = true;
    socket_.shutdown();
    if (up_.joinable())
        up_.join();
    if (down_.joinable())
        down_.join();
    socket_.close();
    running_ = false;
}

}
