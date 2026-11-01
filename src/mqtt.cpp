// SPDX-License-Identifier: GPL-3.0-only
#include "mqtt.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <set>

namespace openkai2::mqtt {
namespace {
constexpr size_t max_packet = 16384, max_retained = 4000, max_clients = 4000;
constexpr int keep_alive_seconds = 30;

void put_length(std::vector<uint8_t> &out, size_t length) {
    do {
        uint8_t byte = uint8_t(length % 128);
        length /= 128;
        if (length)
            byte |= 0x80;
        out.push_back(byte);
    } while (length);
}

void put_text(std::vector<uint8_t> &out, const std::string &text) {
    out.push_back(uint8_t(text.size() >> 8));
    out.push_back(uint8_t(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

std::vector<uint8_t> packet(uint8_t header, const std::vector<uint8_t> &body) {
    std::vector<uint8_t> out{header};
    put_length(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool read_packet(net::Socket &socket, uint8_t &header, std::vector<uint8_t> &body) {
    if (!socket.receive_all(&header, 1))
        return false;
    size_t length = 0, multiplier = 1;
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = 0;
        if (!socket.receive_all(&byte, 1))
            return false;
        length += size_t(byte & 0x7f) * multiplier;
        multiplier *= 128;
        if (!(byte & 0x80))
            break;
        if (i == 3)
            return false;
    }
    if (length > max_packet)
        return false;
    body.resize(length);
    return length == 0 || socket.receive_all(body.data(), length);
}

struct Cursor {
    const std::vector<uint8_t> &data;
    size_t at = 0;
    bool ok = true;
    uint16_t u16() {
        if (at + 2 > data.size())
            return ok = false, 0;
        const uint16_t value = uint16_t(data[at] << 8 | data[at + 1]);
        at += 2;
        return value;
    }
    std::string text() {
        const uint16_t length = u16();
        if (!ok || at + length > data.size())
            return ok = false, std::string();
        std::string value(data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + length));
        at += length;
        return value;
    }
    std::string rest() {
        std::string value(data.begin() + std::ptrdiff_t(std::min(at, data.size())), data.end());
        at = data.size();
        return value;
    }
};

std::vector<uint8_t> publish_packet(const std::string &topic, const std::string &payload, bool retain) {
    std::vector<uint8_t> body;
    put_text(body, topic);
    body.insert(body.end(), payload.begin(), payload.end());
    return packet(uint8_t(0x30 | (retain ? 1 : 0)), body);
}

bool valid_topic(const std::string &topic, bool filter) {
    if (topic.empty() || topic.size() > 256)
        return false;
    for (size_t i = 0; i < topic.size(); ++i) {
        const char c = topic[i];
        if (c == '\0')
            return false;
        if ((c == '#' || c == '+') && !filter)
            return false;
        if (c == '#' && i + 1 != topic.size())
            return false;
    }
    return true;
}
}

bool parse_server(const std::string &text, std::string &host, uint16_t &port) {
    std::string value = text.empty() ? std::string(default_server) : text;
    if (value.rfind("mqtt://", 0) == 0)
        value = value.substr(7);
    const auto colon = value.rfind(':');
    port = default_port;
    if (colon != std::string::npos && value.find(':') == colon) {
        const unsigned long number = std::strtoul(value.c_str() + colon + 1, nullptr, 10);
        if (number < 1 || number > 65535)
            return false;
        port = uint16_t(number);
        value.resize(colon);
    }
    host = value;
    return !host.empty();
}

bool topic_matches(const std::string &filter, const std::string &topic) {
    size_t f = 0, t = 0;
    while (f < filter.size()) {
        if (filter[f] == '#')
            return true;
        const size_t fEnd = filter.find('/', f), tEnd = topic.find('/', t);
        const std::string level = filter.substr(f, fEnd == std::string::npos ? std::string::npos : fEnd - f);
        if (t > topic.size())
            return false;
        const std::string part = topic.substr(t, tEnd == std::string::npos ? std::string::npos : tEnd - t);
        if (level != "+" && level != part)
            return false;
        if (fEnd == std::string::npos)
            return tEnd == std::string::npos;
        if (tEnd == std::string::npos)
            return filter.compare(fEnd + 1, std::string::npos, "#") == 0;
        f = fEnd + 1;
        t = tEnd + 1;
    }
    return t >= topic.size();
}

Client::~Client() { close(); }

bool Client::send_packet(uint8_t header, const std::vector<uint8_t> &body) {
    const auto bytes = packet(header, body);
    std::lock_guard lock(send_mutex_);
    return socket_.send_all(bytes.data(), bytes.size());
}

bool Client::connect(const std::string &server, const std::string &client_id, const Will &will, std::string &error) {
    close();
    std::string host;
    uint16_t port = 0;
    if (!parse_server(server, host, port)) {
        error = "bad online service address";
        return false;
    }
    socket_ = net::Socket::connect(host, port, error);
    if (!socket_.valid())
        return false;
    std::vector<uint8_t> body;
    put_text(body, "MQTT");
    uint8_t flags = 0x02;
    if (!will.topic.empty())
        flags |= 0x04 | (will.retain ? 0x20 : 0);
    body.insert(body.end(), {4, flags, 0, keep_alive_seconds});
    put_text(body, client_id);
    if (!will.topic.empty()) {
        put_text(body, will.topic);
        put_text(body, will.payload);
    }
    uint8_t header = 0;
    std::vector<uint8_t> reply;
    if (!send_packet(0x10, body) || !socket_.readable(8000) || !read_packet(socket_, header, reply) ||
        header != 0x20 || reply.size() < 2 || reply[1] != 0) {
        error = "the online service refused the connection";
        socket_.close();
        return false;
    }
    stopping_ = false;
    connected_ = true;
    reader_ = std::thread([this] { loop(); });
    return true;
}

bool Client::subscribe(const std::string &filter) {
    std::vector<uint8_t> body;
    const uint16_t id = next_id_++;
    body.push_back(uint8_t(id >> 8));
    body.push_back(uint8_t(id));
    put_text(body, filter);
    body.push_back(0);
    return connected_ && send_packet(0x82, body);
}

bool Client::publish(const std::string &topic, const std::string &payload, bool retain) {
    if (!connected_)
        return false;
    const auto bytes = publish_packet(topic, payload, retain);
    std::lock_guard lock(send_mutex_);
    return socket_.send_all(bytes.data(), bytes.size());
}

void Client::on_message(Handler handler) {
    std::lock_guard lock(handler_mutex_);
    handler_ = std::move(handler);
}

void Client::loop() {
    auto lastSent = std::chrono::steady_clock::now();
    while (!stopping_) {
        if (std::chrono::steady_clock::now() - lastSent > std::chrono::seconds(keep_alive_seconds / 2)) {
            if (!send_packet(0xc0, {}))
                break;
            lastSent = std::chrono::steady_clock::now();
        }
        if (!socket_.readable(250))
            continue;
        uint8_t header = 0;
        std::vector<uint8_t> body;
        if (!read_packet(socket_, header, body))
            break;
        if ((header & 0xf0) == 0x30) {
            Cursor in{body};
            const std::string topic = in.text();
            if ((header & 0x06) != 0)
                in.u16();
            const std::string payload = in.rest();
            if (!in.ok)
                continue;
            Handler handler;
            {
                std::lock_guard lock(handler_mutex_);
                handler = handler_;
            }
            if (handler)
                handler(topic, payload);
        }
    }
    connected_ = false;
}

void Client::close() {
    stopping_ = true;
    if (connected_)
        send_packet(0xe0, {});
    socket_.shutdown();
    if (reader_.joinable())
        reader_.join();
    socket_.close();
    connected_ = false;
}

void serve(uint16_t port, const std::atomic<bool> &stopping, const std::function<void(uint16_t)> &ready) {
    struct Session {
        net::Socket socket;
        std::mutex send;
        std::vector<std::string> filters;
        Will will;
        bool alive = true;
    };
    struct Shared {
        std::mutex mutex;
        std::set<std::shared_ptr<Session>> sessions;
        std::map<std::string, std::string> retained;
    };
    auto shared = std::make_shared<Shared>();
    auto deliver = [shared](const std::string &topic, const std::string &payload, bool retain) {
        std::vector<std::shared_ptr<Session>> targets;
        {
            std::lock_guard lock(shared->mutex);
            if (retain) {
                if (payload.empty())
                    shared->retained.erase(topic);
                else if (shared->retained.size() < max_retained || shared->retained.count(topic))
                    shared->retained[topic] = payload;
            }
            for (const auto &session : shared->sessions)
                for (const auto &filter : session->filters)
                    if (topic_matches(filter, topic)) {
                        targets.push_back(session);
                        break;
                    }
        }
        const auto bytes = publish_packet(topic, payload, false);
        for (const auto &session : targets) {
            std::lock_guard lock(session->send);
            session->socket.send_all(bytes.data(), bytes.size());
        }
    };
    auto listener = net::Socket::listen(port);
    std::cout << "[lobby] listening on port " << listener.local_port() << std::endl;
    if (ready)
        ready(listener.local_port());
    while (!stopping) {
        auto client = listener.accept(500);
        if (!client.valid())
            continue;
        auto session = std::make_shared<Session>();
        session->socket = std::move(client);
        {
            std::lock_guard lock(shared->mutex);
            if (shared->sessions.size() >= max_clients)
                continue;
            shared->sessions.insert(session);
        }
        std::thread([shared, session, deliver]() {
            uint8_t header = 0;
            std::vector<uint8_t> body;
            bool clean = false;
            int keepAlive = 60;
            if (session->socket.readable(10000) && read_packet(session->socket, header, body) && header == 0x10) {
                Cursor in{body};
                in.text();
                if (in.at < body.size())
                    ++in.at;
                const uint8_t flags = in.at < body.size() ? body[in.at++] : 0;
                keepAlive = std::max<int>(in.u16(), 5);
                in.text();
                if (flags & 0x04) {
                    session->will.topic = in.text();
                    session->will.payload = in.text();
                    session->will.retain = (flags & 0x20) != 0;
                    if (!valid_topic(session->will.topic, false))
                        session->will.topic.clear();
                }
                const auto ack = packet(0x20, {0, in.ok ? uint8_t(0) : uint8_t(2)});
                {
                    std::lock_guard lock(session->send);
                    session->socket.send_all(ack.data(), ack.size());
                }
                while (in.ok && session->socket.readable(keepAlive * 1500) &&
                       read_packet(session->socket, header, body)) {
                    const uint8_t type = header & 0xf0;
                    if (type == 0xe0) {
                        clean = true;
                        break;
                    }
                    if (type == 0xc0) {
                        const auto pong = packet(0xd0, {});
                        std::lock_guard lock(session->send);
                        session->socket.send_all(pong.data(), pong.size());
                    } else if (type == 0x80) {
                        Cursor sub{body};
                        const uint16_t id = sub.u16();
                        std::vector<uint8_t> ackBody{uint8_t(id >> 8), uint8_t(id)};
                        std::vector<std::string> added;
                        while (sub.ok && sub.at < body.size()) {
                            const std::string filter = sub.text();
                            if (sub.at < body.size())
                                ++sub.at;
                            const bool good = sub.ok && valid_topic(filter, true);
                            ackBody.push_back(good ? 0 : 0x80);
                            if (good)
                                added.push_back(filter);
                        }
                        std::vector<std::pair<std::string, std::string>> matches;
                        {
                            std::lock_guard lock(shared->mutex);
                            for (const auto &filter : added) {
                                if (session->filters.size() < 32)
                                    session->filters.push_back(filter);
                                for (const auto &[topic, payload] : shared->retained)
                                    if (topic_matches(filter, topic))
                                        matches.emplace_back(topic, payload);
                            }
                        }
                        std::lock_guard lock(session->send);
                        const auto ack = packet(0x90, ackBody);
                        session->socket.send_all(ack.data(), ack.size());
                        for (const auto &[topic, payload] : matches) {
                            const auto bytes = publish_packet(topic, payload, true);
                            session->socket.send_all(bytes.data(), bytes.size());
                        }
                    } else if (type == 0x30) {
                        Cursor pub{body};
                        const std::string topic = pub.text();
                        if (header & 0x06)
                            pub.u16();
                        const std::string payload = pub.rest();
                        if (pub.ok && valid_topic(topic, false))
                            deliver(topic, payload, (header & 1) != 0);
                    }
                }
            }
            {
                std::lock_guard lock(shared->mutex);
                shared->sessions.erase(session);
            }
            session->socket.close();
            if (!clean && !session->will.topic.empty())
                deliver(session->will.topic, session->will.payload, session->will.retain);
        }).detach();
    }
}

}
