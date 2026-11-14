// SPDX-License-Identifier: GPL-3.0-only
#include "netplay.h"
#include "dev_identity.h"
#include "nat.h"
#include "online.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <optional>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

namespace openkai2::netplay {
namespace {
enum Message : uint8_t { hello = 1, welcome, reject, snapshot, input, hash, ping, pong, bye, profile, confirm, probe, advantage, resync };
static_assert(probe == probe_message);
constexpr uint32_t max_message = 96u << 20;
constexpr size_t max_snapshot = 160u << 20;
constexpr auto remote_timeout = std::chrono::seconds(20);
std::atomic<Session *> current{nullptr};

void put32(std::vector<uint8_t> &out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(value >> (8 * i)));
}
void put64(std::vector<uint8_t> &out, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        out.push_back(uint8_t(value >> (8 * i)));
}
void put_text(std::vector<uint8_t> &out, const std::string &text) {
    put32(out, uint32_t(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}
void put_varint(std::vector<uint8_t> &out, uint64_t value) {
    while (value >= 0x80) {
        out.push_back(uint8_t(value | 0x80));
        value >>= 7;
    }
    out.push_back(uint8_t(value));
}

struct Reader {
    const uint8_t *data;
    size_t size, at = 0;
    bool ok = true;
    bool need(size_t count) {
        if (!ok || at + count > size || at + count < at)
            ok = false;
        return ok;
    }
    uint32_t u32() {
        if (!need(4))
            return 0;
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
            value |= uint32_t(data[at++]) << (8 * i);
        return value;
    }
    uint64_t u64() {
        if (!need(8))
            return 0;
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value |= uint64_t(data[at++]) << (8 * i);
        return value;
    }
    std::string text(size_t limit = 4096) {
        const uint32_t length = u32();
        if (length > limit || !need(length))
            return ok = false, std::string();
        std::string value(reinterpret_cast<const char *>(data + at), length);
        at += length;
        return value;
    }
    std::vector<uint8_t> bytes(uint64_t length) {
        if (length > max_snapshot || !need(size_t(length)))
            return ok = false, std::vector<uint8_t>();
        std::vector<uint8_t> value(data + at, data + at + length);
        at += size_t(length);
        return value;
    }
    uint64_t varint() {
        uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (!need(1))
                return 0;
            const uint8_t byte = data[at++];
            value |= uint64_t(byte & 0x7f) << shift;
            if (!(byte & 0x80))
                return value;
        }
        ok = false;
        return 0;
    }
};

bool safe_relative(const std::string &path) {
    if (path.empty() || path.size() > 512)
        return false;
    const std::filesystem::path value(path);
    if (value.is_absolute() || value.has_root_name() || value.has_root_directory())
        return false;
    for (const auto &part : value)
        if (part == ".." || part == ".")
            return false;
    return path.find('\\') == std::string::npos && path.find(':') == std::string::npos;
}
}

unsigned auto_delay(double rtt_ms) {
    const double frames = (rtt_ms / 2.0 + 4.0) / (1000.0 / 60.0);
    return std::clamp(unsigned(std::ceil(frames)), 1u, max_delay);
}

std::optional<unsigned> probe_room(const std::string &address, uint16_t port, int timeout_ms) {
    std::string error;
    const auto began = std::chrono::steady_clock::now();
    auto socket = net::Socket::connect(address, port, error);
    if (!socket.valid())
        return std::nullopt;
    const uint8_t frame[5] = {1, 0, 0, 0, probe};
    uint8_t reply[5];
    if (!socket.send_all(frame, 5) || !socket.readable(timeout_ms) || !socket.receive_all(reply, 5) || reply[4] != probe)
        return std::nullopt;
    return unsigned(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count() / 2 + 0.5);
}

Input neutral_input() { return {0xff, 0xff, 128, 128, 128, 128}; }

std::vector<uint8_t> compress(const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out;
    put64(out, data.size());
    size_t i = 0, literal = 0;
    auto flush = [&](size_t end) {
        if (end > literal) {
            put_varint(out, uint64_t(end - literal) << 1);
            out.insert(out.end(), data.begin() + std::ptrdiff_t(literal), data.begin() + std::ptrdiff_t(end));
        }
    };
    while (i < data.size()) {
        size_t run = 1;
        while (i + run < data.size() && data[i + run] == data[i])
            ++run;
        if (run >= 8) {
            flush(i);
            put_varint(out, (uint64_t(run) << 1) | 1u);
            out.push_back(data[i]);
            i += run;
            literal = i;
        } else {
            i += run;
        }
    }
    flush(data.size());
    return out;
}

bool decompress(const std::vector<uint8_t> &data, std::vector<uint8_t> &out) {
    Reader in{data.data(), data.size()};
    const uint64_t size = in.u64();
    if (!in.ok || size > max_snapshot)
        return false;
    out.clear();
    out.reserve(size_t(size));
    while (in.ok && in.at < in.size) {
        const uint64_t token = in.varint();
        const uint64_t count = token >> 1;
        if (!in.ok || out.size() + count > size)
            return false;
        if (token & 1u) {
            if (!in.need(1))
                return false;
            out.insert(out.end(), size_t(count), data[in.at++]);
        } else {
            if (!in.need(size_t(count)))
                return false;
            out.insert(out.end(), data.begin() + std::ptrdiff_t(in.at), data.begin() + std::ptrdiff_t(in.at + count));
            in.at += size_t(count);
        }
    }
    return in.ok && out.size() == size;
}

std::vector<CardFile> read_card_files(const std::filesystem::path &root) {
    std::vector<CardFile> files;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
        return files;
    for (auto it = std::filesystem::recursive_directory_iterator(root, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error))
            continue;
        std::ifstream in(it->path(), std::ios::binary);
        CardFile file{std::filesystem::relative(it->path(), root).generic_string(),
                      std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {})};
        files.push_back(std::move(file));
    }
    std::sort(files.begin(), files.end(), [](const CardFile &a, const CardFile &b) { return a.path < b.path; });
    return files;
}

void write_card_files(const std::filesystem::path &root, const std::vector<CardFile> &files) {
    for (const auto &file : files)
        if (!safe_relative(file.path))
            throw std::runtime_error("the other player sent an unsafe memory card path");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    for (const auto &file : files) {
        const auto target = root / std::filesystem::path(file.path);
        std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(file.data.data()), std::streamsize(file.data.size()));
        if (!out)
            throw std::runtime_error("cannot write the shared memory card");
    }
}

std::vector<uint8_t> pack_snapshot(const std::vector<CardFile> &files, const std::vector<uint8_t> &state) {
    std::vector<uint8_t> raw;
    put32(raw, uint32_t(files.size()));
    for (const auto &file : files) {
        put_text(raw, file.path);
        put64(raw, file.data.size());
        raw.insert(raw.end(), file.data.begin(), file.data.end());
    }
    put64(raw, state.size());
    raw.insert(raw.end(), state.begin(), state.end());
    return compress(raw);
}

bool unpack_snapshot(const std::vector<uint8_t> &packed, std::vector<CardFile> &files, std::vector<uint8_t> &state) {
    std::vector<uint8_t> raw;
    if (!decompress(packed, raw))
        return false;
    Reader in{raw.data(), raw.size()};
    const uint32_t count = in.u32();
    if (count > 4096)
        return false;
    files.clear();
    for (uint32_t i = 0; i < count && in.ok; ++i) {
        CardFile file;
        file.path = in.text(512);
        file.data = in.bytes(in.u64());
        if (!safe_relative(file.path))
            return false;
        files.push_back(std::move(file));
    }
    state = in.bytes(in.u64());
    return in.ok && in.at == in.size;
}

Session::Session(Options options, Machine machine) : options_(std::move(options)), machine_(std::move(machine)) {
    options_.name = identity::allowed_name(options_.name, identity::developer_seed().has_value());
    if (options_.delay)
        options_.delay = std::clamp(options_.delay, 1u, max_delay);
    status_.delay = options_.delay;
}

Session::~Session() { stop(); }

void Session::start() { worker_ = std::thread([this] { run(); }); }

void Session::stop() {
    if (stopping_.exchange(true))
        return;
    {
        std::lock_guard lock(mutex_);
        std::lock_guard socketLock(socketMutex_);
        if (status_.phase != Phase::ended && socket_.valid()) {
            std::vector<uint8_t> frame;
            put32(frame, 1);
            frame.push_back(bye);
            outgoing_.push_back(std::move(frame));
        }
    }
    outgoingReady_.notify_all();
    if (writer_.joinable())
        writer_.join();
    {
        std::lock_guard lock(socketMutex_);
        socket_.shutdown();
        listener_.shutdown();
    }
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
    socket_.close();
    listener_.close();
}

Status Session::status() const {
    std::lock_guard lock(mutex_);
    Status current = status_;
    current.rollback_frames = recentRollbackAt_ + rollback_display_frames >= frontier_ ? recentRollback_ : 0;
    return current;
}

void Session::end(const std::string &message) {
    {
        std::lock_guard lock(mutex_);
        if (status_.phase == Phase::ended)
            return;
        status_.phase = Phase::ended;
        status_.message = message;
        status_.waiting = false;
        locked_ = false;
    }
    std::cerr << "[netplay] " << message << '\n';
    changed_.notify_all();
    outgoingReady_.notify_all();
    std::lock_guard lock(socketMutex_);
    socket_.shutdown();
}

void Session::send(uint8_t type, std::vector<uint8_t> payload) {
    std::vector<uint8_t> frame;
    frame.reserve(payload.size() + 5);
    put32(frame, uint32_t(payload.size() + 1));
    frame.push_back(type);
    frame.insert(frame.end(), payload.begin(), payload.end());
    {
        std::lock_guard lock(mutex_);
        if (status_.phase == Phase::ended)
            return;
        outgoing_.push_back(std::move(frame));
    }
    outgoingReady_.notify_one();
}

void Session::write_loop() {
    for (;;) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock lock(mutex_);
            outgoingReady_.wait(lock, [&] { return !outgoing_.empty() || stopping_ || status_.phase == Phase::ended; });
            if (outgoing_.empty())
                return;
            frame = std::move(outgoing_.front());
            outgoing_.pop_front();
        }
        if (!socket_.send_all(frame.data(), frame.size())) {
            end("Connection lost");
            return;
        }
    }
}

int Session::handshake() {
    auto read_frame = [&](uint8_t &type, std::vector<uint8_t> &payload) {
        if (!socket_.readable(15000))
            return false;
        uint8_t header[5];
        if (!socket_.receive_all(header, 5))
            return false;
        const uint32_t length = uint32_t(header[0]) | uint32_t(header[1]) << 8 | uint32_t(header[2]) << 16 |
                                uint32_t(header[3]) << 24;
        if (length < 1 || length > 65536)
            return false;
        type = header[4];
        payload.resize(length - 1);
        return payload.empty() || socket_.receive_all(payload.data(), payload.size());
    };
    auto write_frame = [&](uint8_t type, const std::vector<uint8_t> &payload) {
        std::vector<uint8_t> frame;
        put32(frame, uint32_t(payload.size() + 1));
        frame.push_back(type);
        frame.insert(frame.end(), payload.begin(), payload.end());
        return socket_.send_all(frame.data(), frame.size());
    };
    auto proof = [&](const std::string &context) {
        if (!identity::claims_developer(options_.name))
            return std::string();
        const auto seed = identity::developer_seed();
        if (!seed)
            return std::string();
        const auto signature = identity::sign_claim(*seed, options_.name, context);
        return crypto::to_hex(signature.data(), signature.size());
    };
    uint8_t type = 0;
    std::vector<uint8_t> payload;
    if (options_.host) {
        if (!read_frame(type, payload)) {
            end("The other player did not answer");
            return 0;
        }
        if (type == probe) {
            write_frame(probe, {});
            return -1;
        }
        if (type != hello) {
            end("The other player sent an unexpected message");
            return 0;
        }
        Reader in{payload.data(), payload.size()};
        const uint32_t version = in.u32();
        const std::string identity = in.text(), name = in.text(64), password = in.text(128), theirs = in.text(64);
        std::string problem;
        if (!in.ok || version != protocol_version)
            problem = "The other player has a different OpenKai2 version";
        else if (identity != options_.identity)
            problem = "The other player has a different OpenKai2 build or game disc";
        else if (password != options_.password && (options_.invite_token.empty() || password != options_.invite_token))
            problem = "Wrong room password";
        if (!problem.empty()) {
            std::vector<uint8_t> out;
            put_text(out, problem);
            write_frame(reject, out);
            end(problem);
            return 0;
        }
        const std::string ours = identity::random_nonce();
        std::vector<uint8_t> out;
        put_text(out, options_.name);
        out.push_back(uint8_t(options_.delay));
        put_text(out, ours);
        put_text(out, proof("session:" + theirs));
        const auto sent = std::chrono::steady_clock::now();
        if (!write_frame(welcome, out) || !read_frame(type, payload) || type != profile) {
            end("The other player did not finish joining");
            return 0;
        }
        const auto rtt = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sent).count();
        Reader reply{payload.data(), payload.size()};
        const std::string signature = reply.text(256);
        const std::string verified = identity::verified_name(name, "session:" + ours, signature);
        const unsigned delay = options_.delay ? options_.delay
                               : options_.rollback ? std::min(auto_delay(rtt), rollback_delay)
                                                   : auto_delay(rtt);
        if (!write_frame(confirm, {uint8_t(delay)})) {
            end("Connection lost");
            return 0;
        }
        std::lock_guard lock(mutex_);
        options_.delay = delay;
        status_.delay = delay;
        status_.ping_ms = unsigned(rtt + 0.5);
        status_.opponent = verified.empty() ? "Player 2" : verified;
    } else {
        const std::string ours = identity::random_nonce();
        std::vector<uint8_t> out;
        put32(out, protocol_version);
        put_text(out, options_.identity);
        put_text(out, options_.name);
        put_text(out, options_.password);
        put_text(out, ours);
        if (!write_frame(hello, out) || !read_frame(type, payload)) {
            end("The host did not answer");
            return 0;
        }
        Reader in{payload.data(), payload.size()};
        if (type == reject) {
            end(in.text());
            return 0;
        }
        const std::string name = in.text(64);
        if (type != welcome || !in.need(1)) {
            end("The host sent an unexpected reply");
            return 0;
        }
        ++in.at;
        const std::string theirs = in.text(64), signature = in.text(256);
        std::vector<uint8_t> answer;
        put_text(answer, proof("session:" + theirs));
        if (!write_frame(profile, answer) || !read_frame(type, payload) || type != confirm || payload.empty()) {
            end("The host did not finish the connection");
            return 0;
        }
        const unsigned delay = std::clamp(unsigned(payload[0]), 1u, max_delay);
        const std::string verified = identity::verified_name(name, "session:" + ours, signature);
        std::lock_guard lock(mutex_);
        options_.delay = delay;
        status_.delay = delay;
        status_.opponent = verified.empty() ? "Player 1" : verified;
    }
    std::lock_guard lock(mutex_);
    status_.phase = Phase::syncing;
    status_.message = "Syncing with " + status_.opponent;
    wantSnapshot_ = options_.host;
    return 1;
}

void Session::run() {
    try {
        if (options_.host) {
            for (int attempt = 0; !listener_.valid(); ++attempt) {
                try {
                    net::Socket listening = net::Socket::listen(options_.port);
                    std::lock_guard lock(socketMutex_);
                    listener_ = std::move(listening);
                } catch (const std::exception &) {
                    if (attempt >= 40 || stopping_)
                        throw;
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
            }
            listeningPort_ = listener_.local_port();
            online::Listing listing;
            nat::PortMapping mapping;
            const std::string roomName = options_.room.empty() ? options_.name + "'s room" : options_.room;
            std::string waiting = "Waiting for a player on port " + std::to_string(listeningPort_.load());
            if (options_.listed) {
                {
                    std::lock_guard lock(mutex_);
                    status_.message = "Opening room " + roomName;
                }
                std::string error, address = options_.address;
                const bool mapped = address.empty() && options_.upnp && mapping.open(listeningPort_.load(), address, error);
                if (address.empty())
                    if (auto found = nat::public_address(error))
                        address = *found;
                online::Room room;
                room.id = options_.room_id.empty() ? online::new_id() : options_.room_id;
                room.name = roomName;
                room.host = options_.name;
                room.locked = !options_.password.empty();
                room.hidden = options_.hidden;
                room.port = listeningPort_.load();
                room.version = options_.version;
                room.address = address;
                if (address.empty())
                    waiting += " (not listed: " + error + ")";
                else if (!listing.open(options_.lobby, room, error))
                    waiting += " (not listed: " + error + ")";
                else
                    waiting = "Waiting for a player in " + roomName +
                              (mapped ? "" : "  (if nobody can join, forward TCP port " +
                                                 std::to_string(listeningPort_.load()) + " on your router)");
            }
            {
                std::lock_guard lock(mutex_);
                status_.message = waiting;
            }
            while (!stopping_) {
                net::Socket accepted = listener_.accept(250);
                if (!accepted.valid())
                    continue;
                {
                    std::lock_guard lock(socketMutex_);
                    socket_ = std::move(accepted);
                }
                const int result = handshake();
                if (result > 0)
                    break;
                {
                    std::lock_guard lock(socketMutex_);
                    socket_.close();
                }
                std::lock_guard lock(mutex_);
                const std::string reason = status_.message;
                status_.phase = Phase::connecting;
                status_.message = result < 0 ? waiting : waiting + " (a player could not join: " + reason + ")";
            }
            listing.close();
            {
                std::lock_guard lock(socketMutex_);
                listener_.close();
            }
            {
                std::lock_guard lock(mutex_);
                if (stopping_ || status_.phase != Phase::syncing)
                    return;
            }
            writer_ = std::thread([this] { write_loop(); });
            read_loop();
            return;
        } else {
            {
                std::lock_guard lock(mutex_);
                status_.message = "Connecting to " + options_.address;
            }
            std::string error;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (!stopping_ && !socket_.valid() && std::chrono::steady_clock::now() < deadline) {
                net::Socket connected = net::Socket::connect(options_.address, options_.port, error);
                {
                    std::lock_guard lock(socketMutex_);
                    socket_ = std::move(connected);
                }
                if (!socket_.valid())
                    std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!socket_.valid() && !stopping_) {
                end("Cannot reach the host. They may need to forward TCP port " + std::to_string(options_.port) +
                    " on their router.");
                return;
            }
        }
        if (stopping_ || handshake() <= 0)
            return;
        writer_ = std::thread([this] { write_loop(); });
        read_loop();
    } catch (const std::exception &error) {
        end(error.what());
    }
}

void Session::read_loop() {
    auto lastPing = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    while (!stopping_) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPing >= std::chrono::seconds(1)) {
            lastPing = now;
            {
                std::lock_guard lock(mutex_);
                pingSent_ = now;
            }
            send(ping, {});
        }
        if (!socket_.readable(200))
            continue;
        uint8_t header[5];
        if (!socket_.receive_all(header, 5))
            break;
        const uint32_t length = uint32_t(header[0]) | uint32_t(header[1]) << 8 | uint32_t(header[2]) << 16 |
                                uint32_t(header[3]) << 24;
        if (length < 1 || length > max_message) {
            end("The other player sent bad data");
            return;
        }
        std::vector<uint8_t> payload(length - 1);
        if (!payload.empty() && !socket_.receive_all(payload.data(), payload.size()))
            break;
        Reader in{payload.data(), payload.size()};
        switch (header[4]) {
        case input: {
            const uint64_t tick = in.u64();
            Input value{};
            if (!in.need(value.size()))
                break;
            std::memcpy(value.data(), payload.data() + in.at, value.size());
            {
                std::lock_guard lock(mutex_);
                if (options_.lag_ms)
                    delayed_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(options_.lag_ms), tick, value});
                else
                    accept_remote(tick, value);
            }
            changed_.notify_all();
            break;
        }
        case advantage: {
            const auto value = int64_t(int32_t(in.u32()));
            std::lock_guard lock(mutex_);
            remoteAdvantage_ = value;
            remoteAdvantageFresh_ = true;
            break;
        }
        case hash: {
            const uint64_t tick = in.u64(), value = in.u64();
            const uint32_t generation = in.u32();
            std::lock_guard lock(mutex_);
            if (generation != generation_)
                break;
            remoteHashes_[tick] = value;
            if (auto found = localHashes_.find(tick); found != localHashes_.end() && found->second != value)
                mark_desync(tick, startTick_);
            break;
        }
        case resync:
            if (!options_.host) {
                const uint32_t generation = in.u32();
                if (!in.ok)
                    break;
                std::lock_guard lock(mutex_);
                resyncGeneration_ = generation;
                resyncData_.assign(payload.begin() + std::ptrdiff_t(in.at), payload.end());
                resyncReady_ = true;
            }
            break;
        case snapshot:
            if (!options_.host) {
                std::lock_guard lock(mutex_);
                snapshot_ = std::move(payload);
                snapshotReady_ = true;
            }
            break;
        case ping:
            send(pong, {});
            break;
        case pong: {
            std::lock_guard lock(mutex_);
            status_.ping_ms = unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - pingSent_)
                                           .count());
            break;
        }
        case bye:
            end("The other player left");
            return;
        default:
            break;
        }
    }
    if (!stopping_)
        end("Connection lost");
}

void Session::accept_remote(uint64_t tick, const Input &value) {
    remote_[tick] = value;
    newestRemote_ = std::max(newestRemote_, tick);
    if (const auto guess = predicted_.find(tick); guess != predicted_.end()) {
        if (guess->second != value) {
            rollbackTo_ = std::min(rollbackTo_, tick);
            ++rollbackStats_.mispredicted;
        }
        predicted_.erase(guess);
    }
    while (remote_.count(confirmed_ + 1))
        ++confirmed_;
}

void Session::deliver_delayed() {
    const auto now = std::chrono::steady_clock::now();
    while (!delayed_.empty() && delayed_.front().due <= now) {
        accept_remote(delayed_.front().tick, delayed_.front().value);
        delayed_.pop_front();
    }
}

bool Session::remote_or_predict(uint64_t tick, Input &out) {
    {
        std::lock_guard lock(mutex_);
        deliver_delayed();
        if (auto found = remote_.find(tick); found != remote_.end()) {
            out = found->second;
            return true;
        }
        if (captured_ + 1 == tick && tick <= confirmed_ + max_prediction) {
            const auto last = remote_.find(confirmed_);
            out = last != remote_.end() ? last->second : neutral_input();
            predicted_[tick] = out;
            ++rollbackStats_.predicted;
            return true;
        }
        ++rollbackStats_.stalls;
    }
    return wait_remote(tick, out);
}

void Session::rollback_point() {
    const uint64_t tick = machine_.tick();
    uint64_t target = ~0ull, start = 0;
    bool finished = false;
    {
        std::lock_guard lock(mutex_);
        deliver_delayed();
        start = startTick_;
        if (replaying_ && tick >= frontier_) {
            replaying_ = false;
            finished = true;
        }
        frontier_ = std::max(frontier_, tick);
        if (rollbackTo_ <= tick)
            target = rollbackTo_;
    }
    if (finished && machine_.replaying)
        machine_.replaying(false);
    if (resync_point(tick))
        return;
    if (target != ~0ull) {
        const uint64_t from = target > start ? target - 1 : start;
        if (!machine_.restore(from)) {
            end("The game fell too far behind the other player to correct itself");
            return;
        }
        if (machine_.replaying)
            machine_.replaying(true);
        std::lock_guard lock(mutex_);
        rollbackTo_ = ~0ull;
        cachedTick_ = ~0ull;
        captured_ = from;
        replaying_ = true;
        for (auto it = predicted_.lower_bound(from); it != predicted_.end();)
            it = predicted_.erase(it);
        ++rollbackStats_.rollbacks;
        rollbackStats_.replayed += tick - from;
        rollbackStats_.longest = std::max(rollbackStats_.longest, tick - from);
        if (recentRollbackAt_ + rollback_display_frames < tick)
            recentRollback_ = 0;
        recentRollback_ = std::max<uint64_t>(recentRollback_, tick - from);
        recentRollbackAt_ = tick;
        return;
    }
    machine_.capture(tick);
    std::lock_guard lock(mutex_);
    captured_ = tick;
}

void Session::mark_desync(uint64_t tick, uint64_t start) {
    if (status_.desynced)
        return;
    status_.desynced = true;
    status_.desync_tick = tick;
    std::cerr << "[netplay] out of sync at frame " << tick - start << '\n';
    if (options_.host && options_.rollback && rollbackStats_.resyncs < max_resyncs)
        resyncWanted_ = true;
}

bool Session::resync_point(uint64_t tick) {
    if (options_.host) {
        {
            std::lock_guard lock(mutex_);
            if (!resyncWanted_ || replaying_ || rollbackTo_ <= tick)
                return false;
        }
        Input confirmed{};
        if (!wait_remote(tick, confirmed))
            return false;
        {
            std::lock_guard lock(mutex_);
            if (confirmed_ < tick || rollbackTo_ <= tick)
                return false;
            resyncWanted_ = false;
            ++generation_;
            ++rollbackStats_.resyncs;
            localHashes_.clear();
            remoteHashes_.clear();
            status_.desynced = false;
        }
        std::vector<uint8_t> payload;
        {
            std::lock_guard lock(mutex_);
            put32(payload, generation_);
        }
        const auto packed = pack_snapshot(machine_.read_cards(), machine_.save());
        payload.insert(payload.end(), packed.begin(), packed.end());
        send(resync, std::move(payload));
        std::cerr << "[netplay] sent a fresh game state at frame " << tick << " to recover from going out of sync\n";
        return false;
    }
    std::vector<uint8_t> packed;
    uint32_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        if (!resyncReady_)
            return false;
        resyncReady_ = false;
        packed = std::move(resyncData_);
        generation = resyncGeneration_;
    }
    std::vector<CardFile> files;
    std::vector<uint8_t> state;
    if (!unpack_snapshot(packed, files, state))
        throw std::runtime_error("The host sent a damaged game state");
    machine_.write_cards(files);
    machine_.load(state);
    if (machine_.forget)
        machine_.forget();
    const uint64_t loaded = machine_.tick();
    machine_.capture(loaded);
    bool replay = false;
    {
        std::lock_guard lock(mutex_);
        generation_ = generation;
        localHashes_.clear();
        remoteHashes_.clear();
        status_.desynced = false;
        ++rollbackStats_.resyncs;
        rollbackTo_ = ~0ull;
        cachedTick_ = ~0ull;
        captured_ = loaded;
        for (auto it = predicted_.upper_bound(loaded); it != predicted_.end();)
            it = predicted_.erase(it);
        frontier_ = std::max(frontier_, tick);
        replay = tick > loaded;
        if (replay)
            replaying_ = true;
    }
    if (replay && machine_.replaying)
        machine_.replaying(true);
    std::cerr << "[netplay] loaded the host's game state at frame " << loaded << " to recover from going out of sync\n";
    return true;
}

std::chrono::microseconds Session::time_sync(uint64_t tick, uint64_t start, unsigned delay) {
    if (!options_.rollback || !machine_.capture || !machine_.restore || tick <= start + delay ||
        (tick - start) % sync_interval != 0)
        return {};
    int64_t local = 0;
    unsigned frames = 0;
    {
        std::lock_guard lock(mutex_);
        if (replaying_ || status_.phase != Phase::playing)
            return {};
        local = int64_t(tick) - int64_t(newestRemote_);
        if (remoteAdvantageFresh_ && local - remoteAdvantage_ >= 3) {
            frames = unsigned(std::min<int64_t>((local - remoteAdvantage_) / 2, max_sync_wait));
            remoteAdvantageFresh_ = false;
            ++rollbackStats_.waits;
            rollbackStats_.waited += frames;
        }
    }
    std::vector<uint8_t> payload;
    put32(payload, uint32_t(int32_t(std::clamp<int64_t>(local, INT32_MIN, INT32_MAX))));
    send(advantage, std::move(payload));
    return frames ? std::chrono::microseconds(frames * 16667u + 1000u) : std::chrono::microseconds{};
}

RollbackStats Session::rollback_stats() const {
    std::lock_guard lock(mutex_);
    return rollbackStats_;
}

bool Session::rolling_back() const {
    std::lock_guard lock(mutex_);
    return replaying_;
}

void Session::state_point() {
    bool rollback = false;
    {
        std::lock_guard lock(mutex_);
        rollback = status_.phase == Phase::playing && options_.rollback && machine_.capture && machine_.restore;
    }
    if (rollback) {
        try {
            rollback_point();
        } catch (const std::exception &error) {
            end(error.what());
        }
        return;
    }
    bool doSave = false, doLoad = false;
    std::vector<uint8_t> packed;
    {
        std::lock_guard lock(mutex_);
        if (status_.phase != Phase::syncing)
            return;
        if (options_.host && wantSnapshot_) {
            wantSnapshot_ = false;
            doSave = true;
        } else if (!options_.host && snapshotReady_) {
            snapshotReady_ = false;
            packed = std::move(snapshot_);
            doLoad = true;
        }
    }
    try {
        if (doSave) {
            auto payload = pack_snapshot(machine_.read_cards(), machine_.save());
            const uint64_t tick = machine_.tick();
            send(snapshot, std::move(payload));
            std::lock_guard lock(mutex_);
            startTick_ = tick;
            lastSent_ = startTick_ + options_.delay - 1;
            confirmed_ = startTick_ + options_.delay - 1;
            frontier_ = tick;
            cachedTick_ = ~0ull;
            locked_ = true;
            status_.phase = Phase::playing;
            status_.message.clear();
        } else if (doLoad) {
            std::vector<CardFile> files;
            std::vector<uint8_t> state;
            if (!unpack_snapshot(packed, files, state))
                throw std::runtime_error("The host sent a damaged game state");
            machine_.write_cards(files);
            machine_.load(state);
            const uint64_t tick = machine_.tick();
            std::lock_guard lock(mutex_);
            startTick_ = tick;
            lastSent_ = startTick_ + options_.delay - 1;
            confirmed_ = startTick_ + options_.delay - 1;
            frontier_ = tick;
            cachedTick_ = ~0ull;
            locked_ = true;
            status_.phase = Phase::playing;
            status_.message.clear();
        }
    } catch (const std::exception &error) {
        end(error.what());
    }
}

bool Session::wait_remote(uint64_t tick, Input &out) {
    std::unique_lock lock(mutex_);
    const auto began = std::chrono::steady_clock::now();
    while (true) {
        deliver_delayed();
        if (auto found = remote_.find(tick); found != remote_.end()) {
            out = found->second;
            status_.waiting = false;
            return true;
        }
        if (status_.phase == Phase::ended || stopping_ || (machine_.stopping && machine_.stopping()))
            return false;
        const auto waited = std::chrono::steady_clock::now() - began;
        if (waited > remote_timeout) {
            lock.unlock();
            end("The other player stopped responding");
            return false;
        }
        if (waited > std::chrono::milliseconds(50))
            status_.waiting = true;
        changed_.wait_for(lock, std::chrono::milliseconds(options_.lag_ms ? 2 : 50));
    }
}

bool Session::inputs(Input &port0, Input &port1) {
    uint64_t tick = 0, start = 0, from = 0, to = 0;
    unsigned delay = 0;
    {
        std::lock_guard lock(mutex_);
        if (status_.phase == Phase::ended)
            return false;
        if (!locked_)
            return false;
        tick = machine_.tick();
        if (tick == cachedTick_) {
            port0 = cached0_;
            port1 = cached1_;
            return true;
        }
        start = startTick_;
        delay = options_.delay;
        from = std::max(lastSent_ + 1, start + delay);
        to = tick + delay;
        if (to > lastSent_)
            lastSent_ = to;
    }
    if (from <= to) {
        const Input sample = machine_.local_input();
        for (uint64_t t = from; t <= to; ++t) {
            {
                std::lock_guard lock(mutex_);
                local_[t] = sample;
            }
            std::vector<uint8_t> payload;
            put64(payload, t);
            payload.insert(payload.end(), sample.begin(), sample.end());
            send(input, std::move(payload));
        }
    }
    Input local = neutral_input(), remote = neutral_input();
    if (tick >= start + delay) {
        {
            std::lock_guard lock(mutex_);
            if (auto found = local_.find(tick); found != local_.end())
                local = found->second;
        }
        const bool predicting = options_.rollback && machine_.capture && machine_.restore;
        if (!(predicting ? remote_or_predict(tick, remote) : wait_remote(tick, remote)))
            return false;
    }
    bool settled = true;
    {
        std::lock_guard lock(mutex_);
        settled = rollbackTo_ >= tick && (predicted_.empty() || predicted_.begin()->first >= tick);
    }
    if (settled && tick > start && (tick - start) % hash_interval == 0 && machine_.hash) {
        const uint64_t value = machine_.hash();
        {
            std::lock_guard lock(mutex_);
            localHashes_[tick] = value;
            if (auto found = remoteHashes_.find(tick); found != remoteHashes_.end() && found->second != value)
                mark_desync(tick, start);
        }
        std::vector<uint8_t> payload;
        put64(payload, tick);
        put64(payload, value);
        {
            std::lock_guard lock(mutex_);
            put32(payload, generation_);
        }
        send(hash, std::move(payload));
    }
    if (const auto pause = time_sync(tick, start, delay); pause.count() > 0)
        std::this_thread::sleep_for(pause);
    std::lock_guard lock(mutex_);
    port0 = options_.host ? local : remote;
    port1 = options_.host ? remote : local;
    cachedTick_ = tick;
    cached0_ = port0;
    cached1_ = port1;
    while (!remote_.empty() && remote_.begin()->first + 120 < tick)
        remote_.erase(remote_.begin());
    while (!local_.empty() && local_.begin()->first + 120 < tick)
        local_.erase(local_.begin());
    while (!localHashes_.empty() && localHashes_.begin()->first + 3600 < tick)
        localHashes_.erase(localHashes_.begin());
    while (!remoteHashes_.empty() && remoteHashes_.begin()->first + 3600 < tick)
        remoteHashes_.erase(remoteHashes_.begin());
    return true;
}

Session *active() { return current.load(); }
void set_active(Session *session) { current.store(session); }

}
