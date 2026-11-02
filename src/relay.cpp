// SPDX-License-Identifier: GPL-3.0-only
#include "relay.h"
#include "netplay.h"
#include "online.h"
#include "room.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace openkai2::netplay {
namespace {
using clock = std::chrono::steady_clock;
constexpr size_t chunk_bytes = 8000, window = 96, early_limit = 1024;
constexpr std::chrono::milliseconds resend_after{400}, give_up{20000};

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

bool RelayLink::knock(const std::string &room_id) {
    const auto channel = out_.substr(std::string(online::prefix).size() + 6);
    return client_.publish(knock_topic(room_id), channel.substr(0, channel.find('/')), false);
}

void RelayLink::received(const std::string &payload) {
    if (payload.size() < 5)
        return;
    const char type = payload[0];
    const uint32_t seq = get32(payload, 1);
    std::lock_guard lock(mutex_);
    heard_ = clock::now();
    if (type == 'A') {
        for (auto it = unacked_.begin(); it != unacked_.end() && it->first <= seq;)
            it = unacked_.erase(it);
        changed_.notify_all();
        return;
    }
    if (type != 'D' && type != 'F')
        return;
    ackDue_ = true;
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
        for (auto &[seq, pending] : unacked_)
            if (now - pending.sent > resend_after && resent < 32) {
                pending.sent = now;
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
        client_.publish(out_, packet, false);
}

bool RelayLink::send_all(const void *data, size_t size) {
    const auto *bytes = static_cast<const char *>(data);
    while (size) {
        const size_t count = std::min(size, chunk_bytes);
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
        if (!client_.publish(out_, packet, false))
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
    client_.publish(out_, packet, false);
}

bool RelayLink::alive() const {
    std::lock_guard lock(mutex_);
    return !dead_ && !peerClosed_;
}

void RelayLink::close() {
    stopping_ = true;
    if (timer_.joinable())
        timer_.join();
    client_.close();
    std::lock_guard lock(mutex_);
    dead_ = true;
    changed_.notify_all();
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
