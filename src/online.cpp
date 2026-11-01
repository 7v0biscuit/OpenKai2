// SPDX-License-Identifier: GPL-3.0-only
#include "online.h"
#include "dev_identity.h"
#include <cstdlib>
#include <random>

namespace openkai2::online {
namespace {
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
    return text;
}

std::vector<std::string> fields(const std::string &payload) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const auto tab = payload.find('\t', start);
        out.push_back(payload.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos)
            return out;
        start = tab + 1;
    }
}

std::string developer_proof(const std::string &name, const std::string &context) {
    if (!identity::claims_developer(name))
        return {};
    const auto seed = identity::developer_seed();
    if (!seed)
        return {};
    const auto signature = identity::sign_claim(*seed, name, context);
    return crypto::to_hex(signature.data(), signature.size());
}

std::string own_name(const std::string &name) {
    return identity::allowed_name(name, identity::developer_seed().has_value());
}

std::string signed_by(const Profile &profile, const std::string &message) {
    const auto signature =
        crypto::sign(profile.seed, reinterpret_cast<const uint8_t *>(message.data()), message.size());
    return crypto::to_hex(signature.data(), signature.size());
}

bool signed_ok(const std::string &id, const std::string &message, const std::string &hex) {
    crypto::PublicKey key{};
    crypto::Signature signature{};
    return id.size() == 64 && crypto::from_hex(id, key.data(), key.size()) &&
           crypto::from_hex(hex, signature.data(), signature.size()) &&
           crypto::verify(key, reinterpret_cast<const uint8_t *>(message.data()), message.size(), signature);
}

unsigned port_number(const std::string &text) {
    if (text.empty() || text.size() > 5 || text.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    const unsigned long value = std::strtoul(text.c_str(), nullptr, 10);
    return value <= 65535 ? unsigned(value) : 0;
}

const char *kind_name(FriendKind kind) {
    return kind == FriendKind::accept ? "accept" : kind == FriendKind::remove ? "remove" : "request";
}
}

bool valid_id(const std::string &id) {
    if (id.size() < 16 || id.size() > 64)
        return false;
    for (char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

crypto::Seed new_seed() {
    std::random_device device;
    crypto::Seed seed{};
    for (auto &byte : seed)
        byte = uint8_t(device());
    return seed;
}

Profile make_profile(const crypto::Seed &seed, const std::string &name) {
    Profile profile;
    profile.seed = seed;
    const auto key = crypto::public_key(seed);
    profile.id = crypto::to_hex(key.data(), key.size());
    profile.name = own_name(name);
    return profile;
}

std::string new_id() { return identity::random_nonce(); }

std::string format_room(const Room &room) {
    const std::string name = own_name(room.host);
    const std::string context = "room:" + room.id + ":" + room.address + ":" + std::to_string(room.port);
    return "1\t" + clean(room.name, 40) + '\t' + clean(name, 24) + '\t' + (room.locked ? "1" : "0") + '\t' +
           clean(room.address, 64) + '\t' + std::to_string(room.port) + '\t' + clean(room.version, 24) + '\t' +
           developer_proof(name, context);
}

std::optional<Room> parse_room(const std::string &id, const std::string &payload) {
    const auto f = fields(payload);
    if (!valid_id(id) || f.size() != 8 || f[0] != "1")
        return std::nullopt;
    Room room;
    room.id = id;
    room.name = clean(f[1], 40);
    room.address = clean(f[4], 64);
    room.port = uint16_t(port_number(f[5]));
    room.version = clean(f[6], 24);
    room.locked = f[3] == "1";
    room.host = identity::verified_name(clean(f[2], 24),
                                        "room:" + room.id + ":" + room.address + ":" + std::to_string(room.port), f[7]);
    if (room.host.empty() || room.address.empty() || !room.port)
        return std::nullopt;
    return room;
}

std::string format_player(const Profile &profile, const Player &player) {
    const std::string name = clean(own_name(player.name), 24), status = clean(player.status, 24),
                      version = clean(player.version, 24);
    const std::string body = name + '\t' + status + '\t' + version;
    return "2\t" + body + '\t' + developer_proof(name, "player:" + profile.id) + '\t' +
           signed_by(profile, "presence\t" + body);
}

std::optional<Player> parse_player(const std::string &id, const std::string &payload) {
    const auto f = fields(payload);
    if (!valid_id(id) || f.size() != 6 || f[0] != "2")
        return std::nullopt;
    if (!signed_ok(id, "presence\t" + f[1] + '\t' + f[2] + '\t' + f[3], f[5]))
        return std::nullopt;
    Player player;
    player.id = id;
    player.name = identity::verified_name(clean(f[1], 24), "player:" + id, f[4]);
    player.status = clean(f[2], 24);
    player.version = clean(f[3], 24);
    if (player.name.empty())
        return std::nullopt;
    return player;
}

std::string format_invite(const Profile &profile, const Invite &invite, const std::string &target) {
    const std::string name = clean(own_name(invite.from_name), 24);
    const std::string body = target + '\t' + invite.room_id + '\t' + invite.nonce + '\t' + clean(invite.token, 64);
    return "2\t" + profile.id + '\t' + name + '\t' + invite.room_id + '\t' + invite.nonce + '\t' +
           clean(invite.token, 64) + '\t' +
           developer_proof(name, "invite:" + target + ":" + invite.room_id + ":" + invite.nonce) + '\t' +
           signed_by(profile, "invite\t" + name + '\t' + body);
}

std::optional<Invite> parse_invite(const std::string &target, const std::string &payload) {
    const auto f = fields(payload);
    if (f.size() != 8 || f[0] != "2" || !valid_id(f[1]) || !valid_id(f[3]) || !valid_id(f[4]))
        return std::nullopt;
    if (!signed_ok(f[1], "invite\t" + f[2] + '\t' + target + '\t' + f[3] + '\t' + f[4] + '\t' + f[5], f[7]))
        return std::nullopt;
    Invite invite;
    invite.from_id = f[1];
    invite.room_id = f[3];
    invite.nonce = f[4];
    invite.token = clean(f[5], 64);
    invite.from_name =
        identity::verified_name(clean(f[2], 24), "invite:" + target + ":" + invite.room_id + ":" + invite.nonce, f[6]);
    return invite;
}

std::string format_friend(const Profile &profile, const FriendMessage &message, const std::string &target) {
    const std::string name = clean(own_name(message.from_name), 24);
    const std::string kind = kind_name(message.kind);
    return "1\t" + kind + '\t' + name + '\t' + message.nonce + '\t' +
           developer_proof(name, "friend:" + target + ":" + message.nonce) + '\t' +
           signed_by(profile, "friend\t" + kind + '\t' + name + '\t' + target + '\t' + message.nonce);
}

std::optional<FriendMessage> parse_friend(const std::string &target, const std::string &from, const std::string &payload) {
    const auto f = fields(payload);
    if (!valid_id(from) || from.size() != 64 || f.size() != 6 || f[0] != "1" || !valid_id(f[3]))
        return std::nullopt;
    if (!signed_ok(from, "friend\t" + f[1] + '\t' + f[2] + '\t' + target + '\t' + f[3], f[5]))
        return std::nullopt;
    FriendMessage message;
    message.from_id = from;
    message.nonce = f[3];
    message.from_name = identity::verified_name(clean(f[2], 24), "friend:" + target + ":" + f[3], f[4]);
    if (f[1] == "request")
        message.kind = FriendKind::request;
    else if (f[1] == "accept")
        message.kind = FriendKind::accept;
    else if (f[1] == "remove")
        message.kind = FriendKind::remove;
    else
        return std::nullopt;
    return message;
}

Directory::~Directory() { close(); }

void Directory::publish_presence() {
    Player self;
    {
        std::lock_guard lock(mutex_);
        self = {profile_.id, profile_.name, status_, version_};
    }
    client_.publish(std::string(prefix) + "players/" + profile_.id, format_player(profile_, self), true);
}

bool Directory::open(const std::string &server, const Profile &profile, const std::string &version, std::string &error) {
    close();
    profile_ = profile;
    version_ = version;
    client_.on_message([this](const std::string &topic, const std::string &payload) { received(topic, payload); });
    const std::string presence = std::string(prefix) + "players/" + profile_.id;
    if (!client_.connect(server, "ok2-" + new_id(), {presence, "", true}, error))
        return false;
    client_.subscribe(std::string(prefix) + "rooms/+");
    client_.subscribe(std::string(prefix) + "hidden/+");
    client_.subscribe(std::string(prefix) + "players/+");
    client_.subscribe(std::string(prefix) + "invites/" + profile_.id);
    client_.subscribe(std::string(prefix) + "friends/" + profile_.id + "/+");
    publish_presence();
    return true;
}

void Directory::close() {
    if (client_.connected())
        client_.publish(std::string(prefix) + "players/" + profile_.id, "", true);
    client_.close();
    std::lock_guard lock(mutex_);
    rooms_.clear();
    players_.clear();
}

void Directory::set_status(const std::string &status) {
    {
        std::lock_guard lock(mutex_);
        status_ = status;
    }
    publish_presence();
}

bool Directory::invite(const std::string &player_id, const std::string &room_id, const std::string &token) {
    Invite invite;
    invite.from_id = profile_.id;
    invite.from_name = profile_.name;
    invite.room_id = room_id;
    invite.nonce = new_id();
    invite.token = token;
    return client_.publish(std::string(prefix) + "invites/" + player_id, format_invite(profile_, invite, player_id),
                           false);
}

bool Directory::send_friend(const std::string &player_id, FriendKind kind) {
    if (!valid_id(player_id) || player_id == profile_.id)
        return false;
    FriendMessage message;
    message.from_id = profile_.id;
    message.from_name = profile_.name;
    message.kind = kind;
    message.nonce = new_id();
    return client_.publish(std::string(prefix) + "friends/" + player_id + "/" + profile_.id,
                           format_friend(profile_, message, player_id), true);
}

void Directory::clear_friend(const std::string &from_id) {
    if (valid_id(from_id))
        client_.publish(std::string(prefix) + "friends/" + profile_.id + "/" + from_id, "", true);
}

std::vector<Room> Directory::rooms() const {
    std::lock_guard lock(mutex_);
    std::vector<Room> out;
    for (const auto &[id, room] : rooms_)
        if (!room.hidden)
            out.push_back(room);
    return out;
}

std::vector<Player> Directory::players() const {
    std::lock_guard lock(mutex_);
    std::vector<Player> out;
    for (const auto &[id, player] : players_)
        if (id != profile_.id)
            out.push_back(player);
    return out;
}

std::optional<Room> Directory::room(const std::string &id) const {
    std::lock_guard lock(mutex_);
    const auto found = rooms_.find(id);
    return found == rooms_.end() ? std::nullopt : std::optional<Room>(found->second);
}

std::optional<Player> Directory::player(const std::string &id) const {
    std::lock_guard lock(mutex_);
    const auto found = players_.find(id);
    return found == players_.end() ? std::nullopt : std::optional<Player>(found->second);
}

void Directory::on_change(Notify notify) {
    std::lock_guard lock(mutex_);
    notify_ = std::move(notify);
}

void Directory::on_invite(InviteHandler handler) {
    std::lock_guard lock(mutex_);
    invite_ = std::move(handler);
}

void Directory::on_friend(FriendHandler handler) {
    std::lock_guard lock(mutex_);
    friend_ = std::move(handler);
}

void Directory::received(const std::string &topic, const std::string &payload) {
    const std::string base = prefix;
    if (topic.compare(0, base.size(), base) != 0)
        return;
    const std::string rest = topic.substr(base.size());
    Notify notify;
    InviteHandler inviteHandler;
    FriendHandler friendHandler;
    std::optional<Invite> invite;
    std::optional<FriendMessage> friendMessage;
    {
        std::lock_guard lock(mutex_);
        if (rest.rfind("rooms/", 0) == 0 || rest.rfind("hidden/", 0) == 0) {
            const bool hidden = rest[0] == 'h';
            const std::string id = rest.substr(hidden ? 7 : 6);
            if (payload.empty())
                rooms_.erase(id);
            else if (auto room = parse_room(id, payload); room && rooms_.size() < 1000) {
                room->hidden = hidden;
                rooms_[id] = *room;
            } else
                return;
        } else if (rest.rfind("players/", 0) == 0) {
            const std::string id = rest.substr(8);
            if (payload.empty())
                players_.erase(id);
            else if (auto player = parse_player(id, payload); player && players_.size() < 2000)
                players_[id] = *player;
            else
                return;
        } else if (rest == "invites/" + profile_.id) {
            invite = parse_invite(profile_.id, payload);
            if (!invite)
                return;
            inviteHandler = invite_;
        } else if (rest.rfind("friends/" + profile_.id + "/", 0) == 0) {
            if (payload.empty())
                return;
            const std::string from = rest.substr(8 + profile_.id.size() + 1);
            friendMessage = parse_friend(profile_.id, from, payload);
            if (!friendMessage)
                return;
            friendHandler = friend_;
        }
        notify = notify_;
    }
    if (invite && inviteHandler)
        inviteHandler(*invite);
    if (friendMessage && friendHandler)
        friendHandler(*friendMessage);
    if (notify)
        notify();
}

Listing::~Listing() { close(); }

bool Listing::open(const std::string &server, const Room &room, std::string &error) {
    close();
    topic_ = std::string(prefix) + (room.hidden ? "hidden/" : "rooms/") + room.id;
    if (!client_.connect(server, "ok2r-" + room.id, {topic_, "", true}, error))
        return false;
    return client_.publish(topic_, format_room(room), true);
}

void Listing::close() {
    if (client_.connected() && !topic_.empty())
        client_.publish(topic_, "", true);
    client_.close();
}

}
