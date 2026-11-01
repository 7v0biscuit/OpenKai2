// SPDX-License-Identifier: GPL-3.0-only
#include "lobby.h"
#include "netplay.h"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace openkai2::netplay;

namespace {
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}

void compression_round_trip() {
    std::vector<uint8_t> data(100000, 0);
    for (size_t i = 0; i < data.size(); i += 97)
        data[i] = uint8_t(i);
    for (size_t i = 50000; i < 50100; ++i)
        data[i] = uint8_t(i * 7);
    const auto packed = compress(data);
    require(packed.size() < data.size() / 4, "zero runs compress");
    std::vector<uint8_t> back;
    require(decompress(packed, back) && back == data, "compression round trip");
    auto broken = packed;
    broken.resize(broken.size() / 2);
    require(!decompress(broken, back), "truncated data is rejected");
}

void snapshot_round_trip() {
    std::vector<CardFile> files{{"mc0/BASLUS-20779DBZ2/BASLUS-20779DBZ2", {1, 2, 3}}, {"mc1/icon.sys", {}}};
    std::vector<uint8_t> state(4096, 7);
    std::vector<CardFile> outFiles;
    std::vector<uint8_t> outState;
    require(unpack_snapshot(pack_snapshot(files, state), outFiles, outState), "snapshot unpacks");
    require(outFiles.size() == 2 && outFiles[0].data == files[0].data && outState == state, "snapshot contents");
    std::vector<CardFile> evil{{"../escape.txt", {1}}};
    require(!unpack_snapshot(pack_snapshot(evil, state), outFiles, outState), "unsafe card paths are rejected");
}

struct FakeMachine {
    bool host;
    std::atomic<uint64_t> tick{0};
    uint64_t sum = 0;
    std::map<uint64_t, std::pair<Input, Input>> seen;
    std::vector<CardFile> cards;

    Machine bind() {
        Machine machine;
        machine.tick = [this] { return tick.load(); };
        machine.save = [this] {
            std::vector<uint8_t> state(16);
            const uint64_t value = tick.load();
            std::memcpy(state.data(), &value, 8);
            std::memcpy(state.data() + 8, &sum, 8);
            return state;
        };
        machine.load = [this](const std::vector<uint8_t> &state) {
            uint64_t value = 0;
            std::memcpy(&value, state.data(), 8);
            std::memcpy(&sum, state.data() + 8, 8);
            tick = value;
        };
        machine.hash = [this] { return sum; };
        machine.local_input = [this] {
            Input value = neutral_input();
            const uint64_t t = tick.load();
            if (t % (host ? 7 : 11) == 0)
                value[0] = uint8_t(host ? 0xbf : 0xdf);
            return value;
        };
        machine.read_cards = [this] { return cards; };
        machine.write_cards = [this](const std::vector<CardFile> &files) { cards = files; };
        return machine;
    }
};

void two_players_stay_in_step() {
    FakeMachine hostMachine{true}, guestMachine{false};
    hostMachine.tick = 500;
    hostMachine.cards = {{"mc0/save.bin", {9, 9, 9}}};
    guestMachine.tick = 37;
    Options hostOptions;
    hostOptions.host = true;
    hostOptions.port = 0;
    hostOptions.delay = 3;
    hostOptions.name = "Goku";
    hostOptions.identity = "build:disc";
    Session host(hostOptions, hostMachine.bind());
    host.start();
    for (int i = 0; i < 200 && !host.listening_port(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(host.listening_port() != 0, "host listens");
    Options guestOptions = hostOptions;
    guestOptions.host = false;
    guestOptions.address = "127.0.0.1";
    guestOptions.port = host.listening_port();
    guestOptions.delay = 1;
    guestOptions.name = "Vegeta";
    Session guest(guestOptions, guestMachine.bind());
    guest.start();

    auto play = [](Session &session, FakeMachine &machine, bool &ok) {
        ok = false;
        uint64_t playedFrom = 0;
        for (int step = 0; step < 20000; ++step) {
            session.state_point();
            const auto status = session.status();
            if (status.phase == Phase::ended)
                return;
            Input port0{}, port1{};
            if (!session.inputs(port0, port1))
                return;
            if (status.phase == Phase::playing) {
                if (!playedFrom)
                    playedFrom = machine.tick;
                machine.seen[machine.tick] = {port0, port1};
                machine.sum = machine.sum * 31 + port0[0] * 7 + port1[0];
                if (machine.tick >= playedFrom + 600) {
                    ok = true;
                    return;
                }
            }
            ++machine.tick;
            if (status.phase != Phase::playing)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    bool hostOk = false, guestOk = false;
    std::thread hostThread([&] { play(host, hostMachine, hostOk); });
    std::thread guestThread([&] { play(guest, guestMachine, guestOk); });
    hostThread.join();
    guestThread.join();
    const auto hostStatus = host.status(), guestStatus = guest.status();
    require(hostOk && guestOk, "both players finish: " + hostStatus.message + " / " + guestStatus.message);
    require(hostStatus.opponent == "Vegeta" && guestStatus.opponent == "Goku", "profile names are exchanged");
    require(guestStatus.delay == 3, "the host's input delay is used");
    require(guestMachine.cards.size() == 1 && guestMachine.cards[0].data == std::vector<uint8_t>{9, 9, 9},
            "memory card is shared");
    size_t compared = 0, pressed0 = 0, pressed1 = 0;
    for (const auto &[tick, inputs] : hostMachine.seen) {
        const auto found = guestMachine.seen.find(tick);
        if (found == guestMachine.seen.end())
            continue;
        require(found->second.first == inputs.first && found->second.second == inputs.second,
                "both players see the same inputs at frame " + std::to_string(tick));
        ++compared;
        pressed0 += inputs.first[0] != 0xff;
        pressed1 += inputs.second[0] != 0xff;
    }
    require(compared >= 590, "frames compared: " + std::to_string(compared));
    require(pressed0 > 50 && pressed1 > 30, "both players' presses arrive");
    require(!hostStatus.desynced && !guestStatus.desynced, "no desync reported");
    host.stop();
    guest.stop();
}

void wrong_build_is_refused() {
    FakeMachine hostMachine{true}, guestMachine{false};
    Options hostOptions;
    hostOptions.port = 0;
    hostOptions.identity = "build-a";
    Session host(hostOptions, hostMachine.bind());
    host.start();
    for (int i = 0; i < 200 && !host.listening_port(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Options guestOptions;
    guestOptions.host = false;
    guestOptions.address = "127.0.0.1";
    guestOptions.port = host.listening_port();
    guestOptions.identity = "build-b";
    Session guest(guestOptions, guestMachine.bind());
    guest.start();
    for (int i = 0; i < 500 && guest.status().phase != Phase::ended; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto status = guest.status();
    require(status.phase == Phase::ended && status.message.find("different OpenKai2 build") != std::string::npos,
            "mismatched builds are refused: " + status.message);
}
}

void lobby_format() {
    namespace lobby = openkai2::lobby;
    lobby::Room room;
    room.id = 7;
    room.name = "Cell\tGames";
    room.host = "Gohan";
    room.locked = true;
    room.address = "203.0.113.9";
    room.port = 7350;
    room.version = "0.1.66";
    const auto back = lobby::parse_room(lobby::room_line(room).substr(0, lobby::room_line(room).size() - 1));
    require(back && back->name == "Cell Games" && back->host == "Gohan" && back->locked && back->port == 7350 &&
                back->address == "203.0.113.9" && back->version == "0.1.66",
            "room lines round trip");
    require(!lobby::parse_room("ROOM\t1\tx"), "short room lines are rejected");
    std::string host;
    uint16_t port = 0;
    require(lobby::parse_server("lobby.example.org", host, port) && host == "lobby.example.org" &&
                port == lobby::default_port,
            "server without port");
    require(lobby::parse_server("10.0.0.2:9000", host, port) && host == "10.0.0.2" && port == 9000, "server with port");
    require(!lobby::parse_server("10.0.0.2:99999", host, port), "bad server port is rejected");
    lobby::Directory directory;
    lobby::Room same;
    same.address = "198.51.100.1";
    for (size_t i = 0; i < lobby::max_rooms_per_address; ++i)
        require(directory.add(same).has_value(), "rooms are accepted");
    require(!directory.add(same), "one address cannot flood the list");
}

void browser_and_passwords() {
    namespace lobby = openkai2::lobby;
    std::atomic<bool> stopping{false};
    std::atomic<uint16_t> lobbyPort{0};
    std::thread server([&] { lobby::serve(0, stopping, [&](uint16_t port) { lobbyPort = port; }); });
    for (int i = 0; i < 300 && !lobbyPort; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(lobbyPort != 0, "lobby starts");
    const std::string address = "127.0.0.1:" + std::to_string(lobbyPort.load());

    FakeMachine hostMachine{true}, guestMachine{false};
    Options hostOptions;
    hostOptions.port = 0;
    hostOptions.identity = "same";
    hostOptions.name = "Piccolo";
    hostOptions.password = "kame";
    hostOptions.lobby = address;
    hostOptions.room = "Lookout";
    hostOptions.version = "0.1.66";
    Session host(hostOptions, hostMachine.bind());
    host.start();
    std::vector<lobby::Room> rooms;
    std::string error;
    for (int i = 0; i < 300 && rooms.empty(); ++i) {
        rooms = lobby::fetch_rooms(address, error);
        if (rooms.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(rooms.size() == 1 && rooms[0].name == "Lookout" && rooms[0].host == "Piccolo" && rooms[0].locked &&
                rooms[0].address == "127.0.0.1" && rooms[0].port == host.listening_port(),
            "the room is listed: " + error);

    Options guestOptions;
    guestOptions.host = false;
    guestOptions.address = rooms[0].address;
    guestOptions.port = rooms[0].port;
    guestOptions.identity = "same";
    guestOptions.name = "Krillin";
    guestOptions.password = "wrong";
    {
        Session guest(guestOptions, guestMachine.bind());
        guest.start();
        for (int i = 0; i < 500 && guest.status().phase != Phase::ended; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(guest.status().message == "Wrong room password", "a wrong password is refused: " + guest.status().message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    require(host.status().phase == Phase::connecting, "the host keeps waiting after a refused player");
    require(lobby::fetch_rooms(address, error).size() == 1, "the room stays listed");

    guestOptions.password = "kame";
    Session guest(guestOptions, guestMachine.bind());
    guest.start();
    for (int i = 0; i < 500 && host.status().phase == Phase::connecting; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(host.status().phase == Phase::syncing && host.status().opponent == "Krillin", "the right password joins");
    bool gone = false;
    for (int i = 0; i < 300 && !gone; ++i) {
        gone = lobby::fetch_rooms(address, error).empty();
        if (!gone)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(gone, "a full room leaves the list");
    guest.stop();
    host.stop();
    stopping = true;
    server.join();
}

int main() {
    try {
        compression_round_trip();
        snapshot_round_trip();
        two_players_stay_in_step();
        wrong_build_is_refused();
        lobby_format();
        browser_and_passwords();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "netplay tests passed\n";
    return 0;
}
