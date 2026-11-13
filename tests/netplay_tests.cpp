// SPDX-License-Identifier: GPL-3.0-only
#include "mqtt.h"
#include "online.h"
#include <functional>
#include <optional>
#include "netplay.h"
#include "room.h"
#include "relay.h"
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
            if (!session.inputs(port0, port1) && session.status().phase == Phase::ended)
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

void name_rules() {
    namespace on = openkai2::online;
    require(on::name_key("J o i n t") == "joint" && on::name_key("JOINT!") == "joint" && on::name_key("[DEV] Biscuit") == "biscuit",
            "names compare without case, spaces, symbols or the developer tag");
    require(on::name_topic("Joint") == on::name_topic("joint") && on::name_topic("Joint") != on::name_topic("Jointt"),
            "name topics follow the name key");
    require(on::name_topic("!!!").empty(), "names need letters or numbers");
    const auto owner = on::make_profile(on::new_seed(), "Joint");
    const auto other = on::make_profile(on::new_seed(), "Joint");
    const auto claim = on::format_name_claim(owner.seed, owner.id, "Joint");
    const std::string topic = std::string(on::prefix) + "names/" + on::name_topic("Joint");
    require(on::allow_service_publish(topic, claim, ""), "a free name can be claimed");
    require(!on::allow_service_publish(topic, on::format_name_claim(other.seed, other.id, "Joint"), claim),
            "a held name cannot be claimed by another profile");
    require(on::allow_service_publish(topic, claim, claim), "the owner can renew the claim");
    require(!on::allow_service_publish(topic, "", claim), "a claim cannot be erased");
    require(!on::allow_service_publish(topic, on::format_name_claim(other.seed, owner.id, "Joint"), ""),
            "a claim signed by another key is refused");
}

void service_format() {
    namespace mq = openkai2::mqtt;
    require(mq::topic_matches("openkai2/v1/rooms/+", "openkai2/v1/rooms/abc"), "single-level wildcard");
    require(!mq::topic_matches("openkai2/v1/rooms/+", "openkai2/v1/rooms/abc/x"), "single level only");
    require(mq::topic_matches("openkai2/#", "openkai2/v1/players/x"), "multi-level wildcard");
    require(!mq::topic_matches("openkai2/v1/invites/aa", "openkai2/v1/invites/bb"), "exact topics");
    std::string host;
    uint16_t port = 0;
    require(mq::parse_server("", host, port) && host == "broker.emqx.io" && port == 1883, "default service");
    require(mq::parse_server("10.0.0.2:9000", host, port) && host == "10.0.0.2" && port == 9000, "custom service");
    namespace on = openkai2::online;
    on::Room room;
    room.id = "0123456789abcdef";
    room.name = "Cell\tGames";
    room.host = "Gohan";
    room.locked = true;
    room.address = "203.0.113.9";
    room.port = 7350;
    room.version = "0.1.67";
    const auto back = on::parse_room(room.id, on::format_room(room));
    require(back && back->name == "Cell Games" && back->host == "Gohan" && back->locked && back->port == 7350 &&
                back->address == "203.0.113.9" && back->version == "0.1.67",
            "room records round trip");
    room.host = "Joint [DEV]";
    const auto forged = on::parse_room(room.id, on::format_room(room));
    require(forged && forged->host == "Joint", "an unsigned [DEV] host loses the tag");
    require(!on::parse_room("../bad", on::format_room(room)), "bad room ids are refused");
    require(auto_delay(1) == 1 && auto_delay(60) <= 3 && auto_delay(150) >= 4 && auto_delay(100000) == max_delay,
            "input delay follows the ping");
}

void service_rooms_invites_passwords() {
    namespace on = openkai2::online;
    std::atomic<bool> stopping{false};
    std::atomic<uint16_t> servicePort{0};
    std::thread server([&] {
        openkai2::mqtt::serve(0, stopping, [&](uint16_t port) { servicePort = port; }, &on::allow_service_publish);
    });
    for (int i = 0; i < 300 && !servicePort; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(servicePort != 0, "service starts");
    const std::string service = "127.0.0.1:" + std::to_string(servicePort.load());
    auto wait_for = [](const std::function<bool()> &condition) {
        for (int i = 0; i < 500; ++i) {
            if (condition())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };

    on::Directory piccolo, krillin;
    std::string error;
    const auto piccoloProfile = on::make_profile(on::new_seed(), "Piccolo");
    const auto krillinProfile = on::make_profile(on::new_seed(), "Krillin");
    require(piccolo.open(service, piccoloProfile, "0.1.67", error), "first launcher connects: " + error);
    require(krillin.open(service, krillinProfile, "0.1.67", error), "second launcher connects: " + error);
    require(piccolo.id() == piccoloProfile.id && piccolo.id().size() == 64, "players are known by their profile key");
    require(wait_for([&] { return piccolo.players().size() == 1 && krillin.players().size() == 1; }),
            "launchers see each other");
    require(krillin.players()[0].name == "Piccolo", "player names are shown");
    for(const std::string status:{"away","do not disturb","hosting","in a match","online"}) {
        piccolo.set_status(status);piccolo.set_status(status);
        require(wait_for([&]{const auto player=krillin.player(piccolo.id());return player && player->status==status;}),
                "signed profile availability reaches another launcher");
    }

    require(on::lookup_name(service, "P i c c o l o", krillinProfile.id, error) == on::NameState::taken,
            "a claimed name is taken for everyone else");
    require(on::lookup_name(service, "piccolo", piccoloProfile.id, error) == on::NameState::mine, "the owner keeps the name");
    require(on::lookup_name(service, "Freeza", krillinProfile.id, error) == on::NameState::free, "unclaimed names are free");
    require(on::lookup_name(service, "biscuit", krillinProfile.id, error) == on::NameState::taken,
            "the developer's name is reserved");
    {
        on::Directory copycat;
        const auto copy = on::make_profile(on::new_seed(), "PICCOLO");
        require(copycat.open(service, copy, "0.1.67", error), "a second profile with a taken name connects");
        require(copycat.name_taken(), "the second profile is told its name is taken");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        require(krillin.players().size() == 1, "a profile with a taken name does not appear online");
        openkai2::mqtt::Client thief;
        require(thief.connect(service, "thief", {}, error), "raw client connects");
        thief.publish(std::string(on::prefix) + "names/" + on::name_topic("Piccolo"),
                      on::format_name_claim(copy.seed, copy.id, "Piccolo"), true);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        require(on::lookup_name(service, "Piccolo", piccoloProfile.id, error) == on::NameState::mine,
                "the service refuses to hand a name to someone else");
        const auto fake = on::make_profile(on::new_seed(), "Goku");
        on::Player pretend{fake.id, "Piccolo", "online", "0.1.67"};
        thief.publish(std::string(on::prefix) + "players/" + fake.id, on::format_player(fake, pretend), true);
        require(wait_for([&] { return krillin.players().size() == 2; }), "the impostor is listed");
        bool flagged = false;
        for (const auto &player : krillin.players())
            if (player.id == fake.id)
                flagged = player.impostor;
        require(flagged, "a player using someone else's name is flagged");
        thief.publish(std::string(on::prefix) + "players/" + fake.id, "", true);
        thief.close();
        require(wait_for([&] { return krillin.players().size() == 1; }), "the impostor leaves");
    }
    std::mutex inviteMutex;
    std::optional<on::Invite> received;
    krillin.on_invite([&](const on::Invite &invite) {
        std::lock_guard lock(inviteMutex);
        received = invite;
    });

    FakeMachine hostMachine{true}, guestMachine{false};
    Options hostOptions;
    hostOptions.port = 0;
    hostOptions.identity = "same";
    hostOptions.name = "Piccolo";
    hostOptions.password = "kame";
    hostOptions.lobby = service;
    hostOptions.listed = true;
    hostOptions.upnp = false;
    hostOptions.address = "127.0.0.1";
    hostOptions.room = "Lookout";
    hostOptions.room_id = on::new_id();
    hostOptions.invite_token = "f00dfeedcafe1234";
    hostOptions.version = "0.1.67";
    Session host(hostOptions, hostMachine.bind());
    host.start();
    require(wait_for([&] { return krillin.room(hostOptions.room_id).has_value(); }), "the room is listed");
    const auto room = *krillin.room(hostOptions.room_id);
    require(room.name == "Lookout" && room.host == "Piccolo" && room.locked && room.port == host.listening_port(),
            "room details are listed");
    require(piccolo.invite(krillin.id(), room.id, "f00dfeedcafe1234"), "invite sent");
    require(wait_for([&] {
                std::lock_guard lock(inviteMutex);
                return received.has_value();
            }),
            "the invite arrives");
    {
        std::lock_guard lock(inviteMutex);
        require(received->from_name == "Piccolo" && received->room_id == room.id && received->from_id == piccolo.id() &&
                    received->token == "f00dfeedcafe1234",
                "the invite names the sender, room and token");
    }
    const auto ping = probe_room(room.address, room.port, 2000);
    require(ping.has_value(), "the room answers a ping");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(host.status().phase == Phase::connecting && host.status().message.find("could not join") == std::string::npos,
            "a ping does not disturb the host");

    Options guestOptions;
    guestOptions.host = false;
    guestOptions.address = room.address;
    guestOptions.port = room.port;
    guestOptions.identity = "same";
    guestOptions.name = "Krillin";
    guestOptions.password = "wrong";
    {
        Session guest(guestOptions, guestMachine.bind());
        guest.start();
        require(wait_for([&] { return guest.status().phase == Phase::ended; }), "a wrong password ends the attempt");
        require(guest.status().message == "Wrong room password", "wrong password message: " + guest.status().message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    require(host.status().phase == Phase::connecting, "the host keeps waiting after a refused player");
    require(krillin.room(room.id).has_value(), "the room stays listed");

    guestOptions.password = "f00dfeedcafe1234";
    Session guest(guestOptions, guestMachine.bind());
    guest.start();
    require(wait_for([&] { return host.status().phase != Phase::connecting; }), "the right password joins");
    require(host.status().opponent == "Krillin" && guest.status().opponent == "Piccolo", "names are exchanged");
    require(host.status().delay >= 1 && host.status().delay == guest.status().delay, "both use the same delay");
    require(wait_for([&] { return !krillin.room(room.id).has_value(); }), "a full room leaves the list");
    guest.stop();
    host.stop();
    std::mutex friendMutex;
    std::vector<on::FriendMessage> toKrillin, toPiccolo;
    krillin.on_friend([&](const on::FriendMessage &message) {
        std::lock_guard lock(friendMutex);
        toKrillin.push_back(message);
    });
    piccolo.on_friend([&](const on::FriendMessage &message) {
        std::lock_guard lock(friendMutex);
        toPiccolo.push_back(message);
    });
    require(piccolo.send_friend(krillin.id(), on::FriendKind::request), "friend request sent");
    require(wait_for([&] {
                std::lock_guard lock(friendMutex);
                return !toKrillin.empty();
            }),
            "the friend request arrives");
    {
        std::lock_guard lock(friendMutex);
        require(toKrillin[0].kind == on::FriendKind::request && toKrillin[0].from_id == piccolo.id() &&
                    toKrillin[0].from_name == "Piccolo",
                "the request names its sender");
    }
    krillin.clear_friend(piccolo.id());
    require(krillin.send_friend(piccolo.id(), on::FriendKind::accept), "friend request accepted");
    require(wait_for([&] {
                std::lock_guard lock(friendMutex);
                return !toPiccolo.empty() && toPiccolo.back().kind == on::FriendKind::accept;
            }),
            "the acceptance arrives");

    openkai2::mqtt::Client forger;
    require(forger.connect(service, "forger", {}, error), "raw client connects");
    const auto fake = on::make_profile(on::new_seed(), "Goku");
    on::Player impostor{piccolo.id(), "Piccolo", "online", "0.1.67"};
    forger.publish(std::string(on::prefix) + "players/" + krillin.id(), on::format_player(fake, impostor), true);
    on::FriendMessage stolen;
    stolen.from_id = piccolo.id();
    stolen.from_name = "Piccolo";
    stolen.nonce = on::new_id();
    forger.publish(std::string(on::prefix) + "friends/" + krillin.id() + "/" + piccolo.id(),
                   on::format_friend(fake, stolen, krillin.id()), true);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard lock(friendMutex);
        require(toKrillin.size() == 1, "a forged friend request is ignored");
    }
    require(krillin.players().size() == 1 && krillin.players()[0].name == "Piccolo", "forged presence is ignored");
    forger.close();

    krillin.close();
    require(piccolo.send_friend(krillinProfile.id, on::FriendKind::request), "request to an offline player");
    on::Directory later;
    std::vector<on::FriendMessage> waiting;
    std::mutex laterMutex;
    later.on_friend([&](const on::FriendMessage &message) {
        std::lock_guard lock(laterMutex);
        waiting.push_back(message);
    });
    require(later.open(service, krillinProfile, "0.1.67", error), "the player comes back online");
    require(wait_for([&] {
                std::lock_guard lock(laterMutex);
                return !waiting.empty();
            }),
            "a request sent while offline is delivered later");
    later.close();
    piccolo.close();
    stopping = true;
    server.join();
}

struct RollbackMachine : FakeMachine {
    std::map<uint64_t, std::pair<uint64_t, uint64_t>> snapshots;
    std::atomic<bool> restored{false};
    uint64_t restores = 0;

    Machine bindRollback() {
        Machine machine = bind();
        machine.capture = [this](uint64_t at) {
            snapshots[at] = {tick.load(), sum};
            while (snapshots.size() > max_prediction + 4u)
                snapshots.erase(snapshots.begin());
        };
        machine.restore = [this](uint64_t at) {
            const auto found = snapshots.find(at);
            if (found == snapshots.end())
                return false;
            tick = found->second.first;
            sum = found->second.second;
            snapshots.erase(snapshots.upper_bound(at), snapshots.end());
            restored = true;
            ++restores;
            return true;
        };
        machine.replaying = [](bool) {};
        return machine;
    }
};

void rollback_corrects_late_inputs() {
    RollbackMachine hostMachine, guestMachine;
    hostMachine.host = true;
    guestMachine.host = false;
    hostMachine.tick = 300;
    guestMachine.tick = 12;
    Options hostOptions;
    hostOptions.host = true;
    hostOptions.port = 0;
    hostOptions.name = "Goku";
    hostOptions.identity = "build:disc";
    hostOptions.lag_ms = 40;
    Session host(hostOptions, hostMachine.bindRollback());
    host.start();
    for (int i = 0; i < 200 && !host.listening_port(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(host.listening_port() != 0, "rollback host listens");
    Options guestOptions = hostOptions;
    guestOptions.host = false;
    guestOptions.address = "127.0.0.1";
    guestOptions.port = host.listening_port();
    guestOptions.name = "Vegeta";
    Session guest(guestOptions, guestMachine.bindRollback());
    guest.start();

    static constexpr uint64_t frames = 720;
    auto play = [](Session &session, RollbackMachine &machine, bool &ok, uint64_t &began) {
        ok = false;
        auto next = std::chrono::steady_clock::now();
        for (int step = 0; step < 60000; ++step) {
            const auto status = session.status();
            if (status.phase == Phase::ended)
                return;
            Input port0{}, port1{};
            if (!session.inputs(port0, port1) && session.status().phase == Phase::ended)
                return;
            if (status.phase == Phase::playing) {
                if (!began)
                    began = machine.tick;
                machine.seen[machine.tick] = {port0, port1};
                machine.sum = machine.sum * 31 + port0[0] * 7 + port1[0];
            }
            session.state_point();
            if (status.phase == Phase::playing && machine.tick >= began + frames && !session.rolling_back()) {
                ok = true;
                return;
            }
            ++machine.tick;
            if (status.phase != Phase::playing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                next = std::chrono::steady_clock::now();
            } else if (!session.rolling_back()) {
                next += std::chrono::microseconds(16667);
                std::this_thread::sleep_until(next);
            }
        }
    };
    bool hostOk = false, guestOk = false;
    uint64_t hostBegan = 0, guestBegan = 0;
    std::thread hostThread([&] { play(host, hostMachine, hostOk, hostBegan); });
    std::thread guestThread([&] { play(guest, guestMachine, guestOk, guestBegan); });
    hostThread.join();
    guestThread.join();
    const auto hostStatus = host.status(), guestStatus = guest.status();
    require(hostOk && guestOk, "both rollback players finish: " + hostStatus.message + " / " + guestStatus.message);
    require(hostStatus.delay <= rollback_delay && guestStatus.delay == hostStatus.delay,
            "rollback uses a short input delay: " + std::to_string(hostStatus.delay));
    const auto hostStats = host.rollback_stats(), guestStats = guest.rollback_stats();
    std::cout << "rollback host predicted=" << hostStats.predicted << " mispredicted=" << hostStats.mispredicted
              << " rollbacks=" << hostStats.rollbacks << " replayed=" << hostStats.replayed << " longest=" << hostStats.longest
              << " stalls=" << hostStats.stalls << "; guest predicted=" << guestStats.predicted
              << " mispredicted=" << guestStats.mispredicted << " rollbacks=" << guestStats.rollbacks
              << " longest=" << guestStats.longest << " stalls=" << guestStats.stalls << '\n';
    require(hostStats.mispredicted > 10 && guestStats.mispredicted > 10 && hostStats.rollbacks > 10 && guestStats.rollbacks > 10,
            "late inputs were predicted wrongly and corrected");
    require(hostStats.longest <= max_prediction + 1u && guestStats.longest <= max_prediction + 1u, "rollbacks stay within the window");
    const uint64_t settled = hostBegan + frames - 30;
    size_t compared = 0, pressed0 = 0, pressed1 = 0;
    for (const auto &[tick, inputs] : hostMachine.seen) {
        if (tick > settled)
            continue;
        const auto found = guestMachine.seen.find(tick);
        require(found != guestMachine.seen.end(), "both players played frame " + std::to_string(tick));
        require(found->second.first == inputs.first && found->second.second == inputs.second,
                "after rollback both players used the same inputs at frame " + std::to_string(tick));
        ++compared;
        pressed0 += inputs.first[0] != 0xff;
        pressed1 += inputs.second[0] != 0xff;
    }
    require(compared >= frames - 40, "rollback frames compared: " + std::to_string(compared));
    require(pressed0 > 50 && pressed1 > 30, "both players' presses survive rollback");
    require(!hostStatus.desynced && !guestStatus.desynced, "rollback reports no desync");
    host.stop();
    guest.stop();
}

void room_checks_before_launch() {
    auto wait = [](Room &room) {
        for (int i = 0; i < 400; ++i) {
            const auto status = room.status();
            if (status.phase == RoomPhase::ready || status.phase == RoomPhase::failed)
                return status;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return room.status();
    };
    RoomOptions hostOptions;
    hostOptions.port = 7450;
    hostOptions.name = "Goku";
    hostOptions.password = "kame";
    hostOptions.version = "test";
    hostOptions.upnp = false;
    openkai2::net::Socket blocker = openkai2::net::Socket::listen(7450);
    Room host(hostOptions);
    host.start();
    uint16_t port = 0;
    for (int i = 0; i < 200 && !(port = host.status().port); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(port != 0 && port != 7450, "a busy port is skipped");
    auto join = [&](const std::string &password, const std::string &version) {
        RoomOptions options;
        options.host = false;
        options.address = "127.0.0.1";
        options.port = port;
        options.name = "Vegeta";
        options.password = password;
        options.version = version;
        auto room = std::make_unique<Room>(options);
        room->start();
        return room;
    };
    require(probe_room("127.0.0.1", port, 2000).has_value(), "an open room answers ping probes");
    auto wrong = join("nope", "test");
    require(wait(*wrong).message == "Wrong room password", "a wrong password is refused: " + wrong->status().message);
    auto old = join("kame", "old");
    require(wait(*old).message.find("same version") != std::string::npos, "another version is refused");
    {
        RoomOptions open = hostOptions;
        open.password.clear();
        open.port = 7460;
        Room openRoom(open);
        openRoom.start();
        uint16_t openPort = 0;
        for (int i = 0; i < 200 && !(openPort = openRoom.status().port); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        RoomOptions visitor;
        visitor.host = false;
        visitor.address = "127.0.0.1";
        visitor.port = openPort;
        visitor.name = "Krillin";
        visitor.version = "test";
        Room joiner(visitor);
        joiner.start();
        require(wait(joiner).phase == RoomPhase::ready, "a room without a password lets players in: " + joiner.status().message);
    }
    auto guest = join("kame", "test");
    const auto joined = wait(*guest), hosted = wait(host);
    require(joined.phase == RoomPhase::ready && hosted.phase == RoomPhase::ready,
            "both players are checked: " + joined.message + " / " + hosted.message);
    require(joined.key == hosted.key && joined.key.size() == 32, "both get the same session key");
    require(joined.delay == hosted.delay && joined.delay >= 1, "both use the same delay");
    require(joined.opponent == "Goku" && hosted.opponent == "Vegeta", "both see each other");
    host.stop();
    openkai2::net::Socket again = openkai2::net::Socket::listen(port);
    require(again.valid(), "the game can reuse the room's port");
}

void relay_through_service() {
    namespace on = openkai2::online;
    std::atomic<bool> stopping{false};
    std::atomic<uint16_t> servicePort{0};
    std::thread server([&] {
        openkai2::mqtt::serve(0, stopping, [&](uint16_t port) { servicePort = port; }, &on::allow_service_publish);
    });
    for (int i = 0; i < 300 && !servicePort; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    struct Stop {
        std::atomic<bool> &flag;
        std::thread &thread;
        ~Stop() {
            flag = true;
            if (thread.joinable())
                thread.join();
        }
    } stopServer{stopping, server};
    require(servicePort != 0, "relay service starts");
    const std::string service = "127.0.0.1:" + std::to_string(servicePort.load());
    {
        RelayLink a, b;
        std::string error;
        require(a.open(service, "0123456789abcdef", 'a', error) && b.open(service, "0123456789abcdef", 'b', error),
                "relay links open: " + error);
        std::vector<uint8_t> sent(3u << 20u), got(sent.size());
        for (size_t i = 0; i < sent.size(); ++i)
            sent[i] = uint8_t((i * 2654435761u) >> 13u);
        bool wrote = false;
        std::thread writer([&] { wrote = a.send_all(sent.data(), sent.size()); });
        const bool read = b.receive_all(got.data(), got.size());
        writer.join();
        require(wrote && read, "relay sends and receives");
        require(got == sent, "relay delivers every byte in order");
        b.send_text("back\n");
        std::string line;
        require(a.receive_line(line, 16, 5000) && line == "back", "relay works both ways");
    }
    RoomOptions hostOptions;
    hostOptions.port = 7470;
    hostOptions.name = "Goku";
    hostOptions.version = "test";
    hostOptions.upnp = false;
    hostOptions.listed = true;
    hostOptions.address = "relay";
    hostOptions.lobby = service;
    hostOptions.room_id = on::new_id();
    Room host(hostOptions);
    host.start();
    for (int i = 0; i < 400 && host.status().phase != RoomPhase::waiting; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(host.status().phase == RoomPhase::waiting, "relay host waits: " + host.status().message);
    RoomOptions joinOptions;
    joinOptions.host = false;
    joinOptions.address = "relay";
    joinOptions.name = "Vegeta";
    joinOptions.version = "test";
    joinOptions.lobby = service;
    joinOptions.room_id = hostOptions.room_id;
    Room guest(joinOptions);
    guest.start();
    for (int i = 0; i < 1500; ++i) {
        const auto guestPhase = guest.status().phase, hostPhase = host.status().phase;
        if (guestPhase == RoomPhase::failed || hostPhase == RoomPhase::failed ||
            (guestPhase == RoomPhase::ready && hostPhase == RoomPhase::ready))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto joined = guest.status(), hosted = host.status();
    require(joined.phase == RoomPhase::ready && joined.relayed && joined.local_port != 0,
            "the guest joins through the relay: " + joined.message + " / host: " + hosted.message);
    require(hosted.phase == RoomPhase::ready && hosted.relayed && joined.key == hosted.key,
            "the host accepts the relayed guest: " + hosted.message);
    auto hostGame = openkai2::net::Socket::listen(hosted.port);
    std::string error;
    auto guestGame = openkai2::net::Socket::connect("127.0.0.1", joined.local_port, error);
    require(guestGame.valid(), "the guest game reaches its local bridge");
    openkai2::net::Socket hostSide;
    for (int i = 0; i < 400 && !hostSide.valid(); ++i)
        hostSide = hostGame.accept(25);
    std::vector<uint8_t> payload(200000);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = uint8_t(i * 7u);
    require(guestGame.send_all(payload.data(), payload.size()), "the guest game sends");
    for (int i = 0; i < 400 && !hostSide.valid(); ++i)
        hostSide = hostGame.accept(25);
    require(hostSide.valid(), "the host bridge reaches the host game");
    std::vector<uint8_t> arrived(payload.size());
    require(hostSide.readable(10000) && hostSide.receive_all(arrived.data(), arrived.size()) && arrived == payload,
            "match data crosses the relay to the host");
    require(hostSide.send_all(payload.data(), 1000) && guestGame.readable(10000) &&
                guestGame.receive_all(arrived.data(), 1000) && std::equal(arrived.begin(), arrived.begin() + 1000, payload.begin()),
            "match data crosses the relay to the guest");
    guestGame.close();
    hostSide.close();
    guest.stop();
    host.stop();
}

int main() {
    try {
        compression_round_trip();
        snapshot_round_trip();
        two_players_stay_in_step();
        rollback_corrects_late_inputs();
        room_checks_before_launch();
        relay_through_service();
        wrong_build_is_refused();
        name_rules();
        service_format();
        service_rooms_invites_passwords();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "netplay tests passed\n";
    return 0;
}
