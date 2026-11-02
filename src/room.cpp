// SPDX-License-Identifier: GPL-3.0-only
#include "room.h"
#include "netplay.h"
#include "online.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <vector>

namespace openkai2::netplay {
namespace {
std::string clean(std::string text) {
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
    return text;
}

std::vector<std::string> fields(const std::string &line) {
    std::vector<std::string> out;
    std::string part;
    std::istringstream in(line);
    while (std::getline(in, part, '\t'))
        out.push_back(part);
    if (!line.empty() && line.back() == '\t')
        out.emplace_back();
    return out;
}

double milliseconds_since(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
}
}

std::string new_session_key() {
    std::random_device device;
    static constexpr char digits[] = "0123456789abcdef";
    std::string key;
    for (int i = 0; i < 32; ++i)
        key.push_back(digits[device() & 15]);
    return key;
}

Room::Room(RoomOptions options) : options_(std::move(options)) {}

Room::~Room() { stop(); }

void Room::start() { worker_ = std::thread([this] { run(); }); }

void Room::track(net::Socket *socket) {
    std::lock_guard lock(mutex_);
    active_ = socket;
}

void Room::stop() {
    stopping_ = true;
    bridge_.stop();
    if (bridger_.joinable())
        bridger_.join();
    if (gameLink_)
        gameLink_->close();
    if (roomLink_)
        roomLink_->close();
    {
        std::lock_guard lock(mutex_);
        if (active_)
            active_->shutdown();
    }
    if (worker_.joinable())
        worker_.join();
}

void Room::release_port() { mapping_.close(); }

RoomStatus Room::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void Room::set(RoomPhase phase, const std::string &message) {
    std::lock_guard lock(mutex_);
    status_.phase = phase;
    status_.message = message;
}

void Room::run() {
    try {
        if (options_.host)
            run_host();
        else
            run_join();
    } catch (const std::exception &error) {
        set(RoomPhase::failed, error.what());
    }
}

bool Room::check_guest(Link &socket, std::string &problem) {
    std::string line;
    char first = 0;
    if (!socket.readable(5000) || !socket.receive_all(&first, 1)) {
        problem = "the player did not answer";
        return false;
    }
    if (first == 1) {
        uint8_t rest[4];
        if (socket.receive_all(rest, 4) && rest[0] == 0 && rest[1] == 0 && rest[2] == 0 && rest[3] == probe_message) {
            const uint8_t reply[5] = {1, 0, 0, 0, probe_message};
            socket.send_all(reply, 5);
        }
        return false;
    }
    if (!socket.receive_line(line, 512, 5000)) {
        problem = "the player did not answer";
        return false;
    }
    line.insert(line.begin(), first);
    const auto hello = fields(line);
    if (hello.size() < 4 || hello[0] != room_greeting) {
        problem = "not an OpenKai2 player";
        return false;
    }
    const std::string &version = hello[1], &name = hello[2], &password = hello[3];
    auto refuse = [&](const std::string &reason) {
        socket.send_text("NO\t" + reason + "\n");
        problem = reason;
        return false;
    };
    if (version != options_.version)
        return refuse("This room uses OpenKai2 " + options_.version + ". Both players need the same version.");
    if (password != options_.password && (options_.invite_token.empty() || password != options_.invite_token))
        return refuse("Wrong room password");
    if (!socket.send_text("OK\t" + clean(options_.name) + "\n")) {
        problem = "connection lost";
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        status_.phase = RoomPhase::checking;
        status_.opponent = name;
        status_.message = "Checking the connection to " + name;
    }
    std::vector<double> times;
    for (int i = 0; i < 6 && !stopping_; ++i) {
        const auto sent = std::chrono::steady_clock::now();
        if (!socket.send_text("PING\t" + std::to_string(i) + "\n") || !socket.receive_line(line, 64, 3000) ||
            line != "PONG\t" + std::to_string(i)) {
            problem = "the connection to " + name + " is not stable";
            return false;
        }
        times.push_back(milliseconds_since(sent));
    }
    if (stopping_)
        return false;
    std::sort(times.begin(), times.end());
    const double rtt = times[times.size() / 2];
    const unsigned delay = options_.delay ? options_.delay : auto_delay(rtt);
    const std::string key = new_session_key();
    if (!socket.send_text("START\t" + std::to_string(delay) + "\t" + key + "\t" + std::to_string(unsigned(rtt + 0.5)) +
                          "\n") ||
        !socket.receive_line(line, 64, 5000) || line != "GO") {
        problem = name + " left before the match started";
        return false;
    }
    socket.receive_line(line, 64, 2000);
    std::lock_guard lock(mutex_);
    status_.delay = delay;
    status_.ping_ms = unsigned(rtt + 0.5);
    status_.key = key;
    return true;
}

void Room::run_host() {
    net::Socket listener;
    auto bind = [&](uint16_t port) {
        try {
            listener = net::Socket::listen(port);
        } catch (const std::exception &) {
        }
    };
    for (unsigned offset = 0; offset < 10 && !listener.valid(); ++offset)
        bind(uint16_t(options_.port + offset));
    if (!listener.valid())
        bind(0);
    if (!listener.valid()) {
        set(RoomPhase::failed, "Could not open a network port for the room.");
        return;
    }
    const uint16_t port = listener.local_port();
    {
        std::lock_guard lock(mutex_);
        status_.port = port;
        status_.message = "Opening your room";
    }
    const std::string roomName = options_.room.empty() ? options_.name + "'s room" : options_.room;
    std::string waiting = "Waiting for a player on port " + std::to_string(port);
    online::Listing listing;
    if (options_.listed) {
        std::string error, address = options_.address;
        const bool mapped = address.empty() && options_.upnp && mapping_.open(port, address, error);
        if (address.empty())
            if (auto found = nat::public_address(error))
                address = *found;
        online::Room room;
        room.id = options_.room_id.empty() ? online::new_id() : options_.room_id;
        room.name = roomName;
        room.host = options_.name;
        room.locked = !options_.password.empty();
        room.hidden = options_.hidden;
        room.port = port;
        room.version = options_.version;
        room.address = address;
        if (address.empty())
            room.address = "relay";
        if (!listing.open(options_.lobby, room, error))
            waiting += " (not listed: " + error + ")";
        else
            waiting = "Waiting for a player in " + roomName;
        (void)mapped;
    } else if (options_.upnp) {
        std::string error, address;
        mapping_.open(port, address, error);
    }
    std::mutex knockMutex;
    std::deque<std::string> knocks;
    mqtt::Client knocker;
    if (!options_.room_id.empty()) {
        std::string error;
        knocker.on_message([&](const std::string &, const std::string &channel) {
            std::lock_guard lock(knockMutex);
            if (valid_channel(channel) && knocks.size() < 8)
                knocks.push_back(channel);
        });
        if (knocker.connect(options_.lobby, "ok2k-" + new_session_key().substr(0, 16), {}, error))
            knocker.subscribe(knock_topic(options_.room_id));
    }
    set(RoomPhase::waiting, waiting);
    while (!stopping_) {
        std::string channel;
        {
            std::lock_guard lock(knockMutex);
            if (!knocks.empty()) {
                channel = knocks.front();
                knocks.pop_front();
            }
        }
        std::string problem;
        bool accepted = false;
        net::Socket socket;
        if (!channel.empty()) {
            auto link = std::make_unique<RelayLink>();
            std::string error;
            if (link->open(options_.lobby, channel, 'a', error)) {
                accepted = check_guest(*link, problem);
                if (accepted) {
                    roomLink_ = std::move(link);
                    std::lock_guard lock(mutex_);
                    status_.relayed = true;
                    status_.message = "Connected through the online service";
                }
            } else
                problem = error;
        } else {
            socket = listener.accept(250);
            if (!socket.valid())
                continue;
            track(&socket);
            SocketLink link(socket);
            accepted = check_guest(link, problem);
            track(nullptr);
        }
        if (accepted) {
            knocker.close();
            listing.close();
            listener.close();
            if (!channel.empty())
                bridger_ = std::thread([this, channel, port] { bridge_host(channel + "g", port); });
            set(RoomPhase::ready, "Connection checked. Starting the match.");
            return;
        }
        socket.close();
        if (stopping_)
            break;
        if (problem.empty())
            continue;
        std::lock_guard lock(mutex_);
        status_.phase = RoomPhase::waiting;
        status_.opponent.clear();
        status_.message = waiting + " (a player could not join: " + problem + ")";
    }
    knocker.close();
    listing.close();
}

void Room::bridge_host(std::string channel, uint16_t port) {
    auto link = std::make_unique<RelayLink>();
    std::string error;
    if (!link->open(options_.lobby, channel, 'a', error))
        return;
    while (!stopping_ && link->alive() && !link->readable(250)) {
    }
    net::Socket game;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (!stopping_ && !game.valid() && std::chrono::steady_clock::now() < deadline) {
        game = net::Socket::connect("127.0.0.1", port, error);
        if (!game.valid())
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (!game.valid() || stopping_)
        return;
    gameLink_ = std::move(link);
    bridge_.start(*gameLink_, std::move(game));
}

bool Room::bridge_guest(const std::string &channel) {
    auto link = std::make_unique<RelayLink>();
    std::string error;
    if (!link->open(options_.lobby, channel, 'b', error))
        return false;
    net::Socket listener;
    try {
        listener = net::Socket::listen(0);
    } catch (const std::exception &) {
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        status_.local_port = listener.local_port();
    }
    gameLink_ = std::move(link);
    bridger_ = std::thread([this, listener = std::move(listener)]() mutable {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (!stopping_ && std::chrono::steady_clock::now() < deadline) {
            auto game = listener.accept(250);
            if (!game.valid())
                continue;
            bridge_.start(*gameLink_, std::move(game));
            return;
        }
    });
    return true;
}

void Room::run_join() {
    const bool relayable = !options_.room_id.empty();
    const bool direct = options_.address != "relay";
    net::Socket socket;
    std::string error;
    if (direct) {
        set(RoomPhase::opening, "Connecting to " + options_.address);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(relayable ? 4 : 15);
        while (!stopping_ && !socket.valid() && std::chrono::steady_clock::now() < deadline) {
            socket = net::Socket::connect(options_.address, options_.port, error);
            if (!socket.valid())
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }
    if (stopping_)
        return;
    if (socket.valid()) {
        struct Tracked {
            Room &room;
            ~Tracked() { room.track(nullptr); }
        } tracked{*this};
        track(&socket);
        SocketLink link(socket);
        join_over(link, {});
        return;
    }
    if (!relayable) {
        set(RoomPhase::failed, "Cannot reach the room. The host may need to forward TCP port " +
                                   std::to_string(options_.port) + " on their router.");
        return;
    }
    set(RoomPhase::opening, "Connecting through the online service");
    const std::string channel = new_session_key().substr(0, 16);
    roomLink_ = std::make_unique<RelayLink>();
    if (!roomLink_->open(options_.lobby, channel, 'b', error) || !roomLink_->knock(options_.room_id)) {
        set(RoomPhase::failed, "Cannot reach the online service: " + error);
        return;
    }
    join_over(*roomLink_, channel);
}

bool Room::join_over(Link &socket, const std::string &channel) {
    if (!socket.send_text(std::string(room_greeting) + "\t" + clean(options_.version) + "\t" + clean(options_.name) + "\t" +
                          clean(options_.password) + "\n")) {
        set(RoomPhase::failed, "Connection lost");
        return false;
    }
    std::string line;
    if (!socket.receive_line(line, 512, 12000)) {
        set(RoomPhase::failed, "The room did not answer. It may be full or closed.");
        return false;
    }
    auto reply = fields(line);
    if (reply.size() >= 2 && reply[0] == "NO") {
        set(RoomPhase::failed, reply[1]);
        return false;
    }
    if (reply.size() < 2 || reply[0] != "OK") {
        set(RoomPhase::failed, "That address is not an OpenKai2 room");
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        status_.phase = RoomPhase::checking;
        status_.opponent = reply[1];
        status_.message = "Checking the connection to " + reply[1];
    }
    while (!stopping_) {
        if (!socket.receive_line(line, 128, 10000)) {
            set(RoomPhase::failed, "The connection to the host is not stable");
            return false;
        }
        auto parts = fields(line);
        if (parts.size() == 2 && parts[0] == "PING") {
            if (!socket.send_text("PONG\t" + parts[1] + "\n")) {
                set(RoomPhase::failed, "Connection lost");
                return false;
            }
            continue;
        }
        if (parts.size() == 4 && parts[0] == "START") {
            const unsigned delay = unsigned(std::strtoul(parts[1].c_str(), nullptr, 10));
            if (delay < 1 || delay > max_delay || parts[2].size() != 32) {
                set(RoomPhase::failed, "The host sent a bad start message");
                return false;
            }
            if (!channel.empty() && !bridge_guest(channel + "g")) {
                set(RoomPhase::failed, "Could not open the match connection");
                return false;
            }
            socket.send_text("GO\n");
            socket.shutdown();
            std::lock_guard lock(mutex_);
            status_.delay = delay;
            status_.key = parts[2];
            status_.ping_ms = unsigned(std::strtoul(parts[3].c_str(), nullptr, 10));
            status_.relayed = !channel.empty();
            status_.phase = RoomPhase::ready;
            status_.message = "Connection checked. Starting the match.";
            return true;
        }
        set(RoomPhase::failed, "The host sent an unknown message");
        return false;
    }
    return false;
}

}
