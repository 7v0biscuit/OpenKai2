// SPDX-License-Identifier: GPL-3.0-only
#include "lobby.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace openkai2::lobby {
namespace {
constexpr int keep_alive_seconds = 20, silence_seconds = 75;

unsigned parse_port(const std::string &text) {
    if (text.empty() || text.size() > 5 || text.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    const unsigned long value = std::strtoul(text.c_str(), nullptr, 10);
    return value >= 1 && value <= 65535 ? unsigned(value) : 0;
}
}

std::string clean(std::string text, size_t limit) {
    for (char &c : text)
        if (c == '\t' || c == '\n' || c == '\r' || static_cast<unsigned char>(c) < 0x20)
            c = ' ';
    if (text.size() > limit) {
        text.resize(limit);
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xc0) == 0x80)
            text.pop_back();
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xc0)
            text.pop_back();
    }
    const auto first = text.find_first_not_of(' ');
    const auto last = text.find_last_not_of(' ');
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (;;) {
        const auto tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos)
            return fields;
        start = tab + 1;
    }
}

std::string room_line(const Room &room) {
    return "ROOM\t" + std::to_string(room.id) + '\t' + clean(room.name, 40) + '\t' + clean(room.host, 24) + '\t' +
           (room.locked ? "1" : "0") + '\t' + clean(room.address, 64) + '\t' + std::to_string(room.port) + '\t' +
           clean(room.version, 24) + '\n';
}

std::optional<Room> parse_room(const std::string &line) {
    const auto fields = split(line);
    if (fields.size() != 8 || fields[0] != "ROOM")
        return std::nullopt;
    Room room;
    room.id = std::strtoull(fields[1].c_str(), nullptr, 10);
    room.name = clean(fields[2], 40);
    room.host = clean(fields[3], 24);
    room.locked = fields[4] == "1";
    room.address = clean(fields[5], 64);
    room.port = uint16_t(parse_port(fields[6]));
    room.version = clean(fields[7], 24);
    if (!room.id || room.address.empty() || !room.port || room.host.empty())
        return std::nullopt;
    return room;
}

bool parse_server(const std::string &text, std::string &host, uint16_t &port) {
    const std::string value = clean(text, 255);
    if (value.empty())
        return false;
    const auto colon = value.rfind(':');
    if (colon != std::string::npos && value.find(':') == colon) {
        port = uint16_t(parse_port(value.substr(colon + 1)));
        host = value.substr(0, colon);
        return port != 0 && !host.empty();
    }
    host = value;
    port = default_port;
    return true;
}

std::optional<uint64_t> Directory::add(Room room) {
    std::lock_guard lock(mutex_);
    if (rooms_.size() >= max_rooms)
        return std::nullopt;
    const auto same = std::count_if(rooms_.begin(), rooms_.end(),
                                    [&](const auto &entry) { return entry.second.address == room.address; });
    if (size_t(same) >= max_rooms_per_address)
        return std::nullopt;
    room.id = next_++;
    rooms_[room.id] = room;
    return room.id;
}

void Directory::remove(uint64_t id) {
    std::lock_guard lock(mutex_);
    rooms_.erase(id);
}

std::vector<Room> Directory::list() const {
    std::lock_guard lock(mutex_);
    std::vector<Room> rooms;
    for (const auto &[id, room] : rooms_)
        rooms.push_back(room);
    return rooms;
}

std::string handle_list(const Directory &directory, const std::vector<std::string> &fields) {
    const std::string version = fields.size() > 1 ? fields[1] : std::string();
    std::string reply;
    for (const auto &room : directory.list())
        if (version.empty() || room.version == version)
            reply += room_line(room);
    return reply + "END\n";
}

std::vector<Room> fetch_rooms(const std::string &server, std::string &error) {
    std::string host;
    uint16_t port = 0;
    if (!parse_server(server, host, port)) {
        error = "Enter a server address";
        return {};
    }
    auto socket = net::Socket::connect(host, port, error);
    if (!socket.valid())
        return {};
    if (!socket.send_text("LIST\n")) {
        error = "The server closed the connection";
        return {};
    }
    std::vector<Room> rooms;
    std::string line;
    while (socket.receive_line(line, max_line, 5000)) {
        if (line == "END")
            return rooms;
        if (auto room = parse_room(line); room && rooms.size() < max_rooms)
            rooms.push_back(*room);
    }
    error = "The server did not answer";
    return {};
}

bool Registration::open(const std::string &server, const Room &room, std::string &error) {
    std::string host;
    uint16_t port = 0;
    if (!parse_server(server, host, port)) {
        error = "no server address";
        return false;
    }
    socket_ = net::Socket::connect(host, port, error);
    if (!socket_.valid())
        return false;
    const std::string line = "REGISTER\t" + clean(room.name, 40) + '\t' + clean(room.host, 24) + '\t' +
                             (room.locked ? "1" : "0") + '\t' + std::to_string(room.port) + '\t' +
                             clean(room.version, 24) + '\n';
    std::string reply;
    if (!socket_.send_text(line) || !socket_.receive_line(reply, max_line, 5000) || reply.rfind("OK", 0) != 0) {
        error = reply.rfind("FULL", 0) == 0 ? "the server is full" : "the server did not accept the room";
        socket_.close();
        return false;
    }
    last_ = std::chrono::steady_clock::now();
    return true;
}

void Registration::keep_alive() {
    if (!socket_.valid())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_ < std::chrono::seconds(keep_alive_seconds))
        return;
    last_ = now;
    if (!socket_.send_text("PING\n"))
        socket_.close();
}

void Registration::close() {
    if (socket_.valid())
        socket_.send_text("BYE\n");
    socket_.shutdown();
    socket_.close();
}

void serve(uint16_t port, const std::atomic<bool> &stopping, const std::function<void(uint16_t)> &ready) {
    auto directory = std::make_shared<Directory>();
    auto listener = net::Socket::listen(port);
    std::cout << "[lobby] listening on port " << listener.local_port() << std::endl;
    if (ready)
        ready(listener.local_port());
    auto clients = std::make_shared<std::atomic<int>>(0);
    while (!stopping) {
        auto client = listener.accept(500);
        if (!client.valid())
            continue;
        if (clients->load() >= 2000)
            continue;
        ++*clients;
        std::thread([directory, clients, socket = std::move(client)]() mutable {
            const std::string address = socket.peer_address();
            std::string line;
            if (socket.receive_line(line, max_line, 10000)) {
                const auto fields = split(line);
                if (fields[0] == "LIST") {
                    socket.send_text(handle_list(*directory, fields));
                } else if (fields[0] == "REGISTER" && fields.size() == 6) {
                    Room room;
                    room.name = clean(fields[1], 40);
                    room.host = clean(fields[2], 24);
                    room.locked = fields[3] == "1";
                    room.port = uint16_t(parse_port(fields[4]));
                    room.version = clean(fields[5], 24);
                    room.address = address;
                    std::optional<uint64_t> id;
                    if (!room.host.empty() && room.port && !room.address.empty())
                        id = directory->add(room);
                    if (!id) {
                        socket.send_text("FULL\n");
                    } else {
                        socket.send_text("OK\t" + std::to_string(*id) + '\n');
                        std::cout << "[lobby] room " << *id << " opened by " << room.host << " at " << address
                                  << std::endl;
                        while (socket.receive_line(line, max_line, silence_seconds * 1000) && line != "BYE") {
                        }
                        directory->remove(*id);
                        std::cout << "[lobby] room " << *id << " closed" << std::endl;
                    }
                }
            }
            socket.close();
            --*clients;
        }).detach();
    }
}

}
