// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "ed25519.h"
#include "mqtt.h"

namespace openkai2::online {

inline constexpr char prefix[] = "openkai2/v1/";

struct Room {
    std::string id, name, host, address, version;
    uint16_t port = 0;
    bool locked = false, hidden = false;
};

struct Player {
    std::string id, name, status, version;
    bool impostor = false;
};

std::string name_key(const std::string &name);
std::string name_topic(const std::string &name);
std::string format_name_claim(const crypto::Seed &seed, const std::string &owner, const std::string &name);
std::optional<std::pair<std::string, std::string>> parse_name_claim(const std::string &topic_hash, const std::string &payload);
bool allow_service_publish(const std::string &topic, const std::string &payload, const std::string &existing);
enum class NameState { free, mine, taken };
std::optional<NameState> lookup_name(const std::string &server, const std::string &name, const std::string &owner,
                                     std::string &error);

struct Invite {
    std::string from_id, from_name, room_id, nonce, token;
};

enum class FriendKind { request, accept, remove };
struct FriendMessage {
    std::string from_id, from_name, nonce;
    FriendKind kind = FriendKind::request;
};

struct Profile {
    crypto::Seed seed{};
    std::string id, name;
};
Profile make_profile(const crypto::Seed &seed, const std::string &name);
crypto::Seed new_seed();

std::string new_id();
bool valid_id(const std::string &id);
std::string format_room(const Room &room);
std::optional<Room> parse_room(const std::string &id, const std::string &payload);
std::string format_player(const Profile &profile, const Player &player);
std::optional<Player> parse_player(const std::string &id, const std::string &payload);
std::string format_invite(const Profile &profile, const Invite &invite, const std::string &target);
std::optional<Invite> parse_invite(const std::string &target, const std::string &payload);
std::string format_friend(const Profile &profile, const FriendMessage &message, const std::string &target);
std::optional<FriendMessage> parse_friend(const std::string &target, const std::string &from, const std::string &payload);

class Directory {
  public:
    using Notify = std::function<void()>;
    using InviteHandler = std::function<void(const Invite &)>;
    using FriendHandler = std::function<void(const FriendMessage &)>;
    ~Directory();
    bool open(const std::string &server, const Profile &profile, const std::string &version, std::string &error);
    void close();
    bool connected() const { return client_.connected(); }
    void set_status(const std::string &status);
    bool invite(const std::string &player_id, const std::string &room_id, const std::string &token);
    bool send_friend(const std::string &player_id, FriendKind kind);
    void clear_friend(const std::string &from_id);
    std::vector<Room> rooms() const;
    std::vector<Player> players() const;
    std::optional<Room> room(const std::string &id) const;
    std::optional<Player> player(const std::string &id) const;
    NameState name_state(const std::string &name, const std::string &owner) const;
    bool name_taken() const { return nameTaken_; }
    void on_change(Notify notify);
    void on_invite(InviteHandler handler);
    void on_friend(FriendHandler handler);
    const std::string &id() const { return profile_.id; }

  private:
    void received(const std::string &topic, const std::string &payload);
    void publish_presence();
    mqtt::Client client_;
    Profile profile_;
    std::string version_, status_ = "online";
    mutable std::mutex mutex_;
    std::map<std::string, Room> rooms_;
    std::map<std::string, Player> players_;
    std::map<std::string, std::pair<std::string, std::string>> names_;
    bool nameTaken_ = false;
    Notify notify_;
    InviteHandler invite_;
    FriendHandler friend_;
};

class Listing {
  public:
    ~Listing();
    bool open(const std::string &server, const Room &room, std::string &error);
    void close();

  private:
    mqtt::Client client_;
    std::string topic_;
};

}
