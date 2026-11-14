// SPDX-License-Identifier: GPL-3.0-only
#include "netplay_game.h"
#include "disc_image.h"
#include "host_input.h"
#include "launcher/version.h"
#include "delta_states.h"
#include "machine_state.h"
#include "simulation_pacing.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "state_archive.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <memory>
#include <thread>

namespace ps2_stubs {
void setStableCardDates(bool stable);
}

namespace openkai2::netplay {
namespace {
std::filesystem::path card_root(const Arguments &arguments) {
    return default_cache_directory().parent_path() / "netplay" / (arguments.host ? "host" : "join");
}

bool pad_source(int port, LatchedPadState::Packet &packet) {
    auto *session = active();
    if (!session || port < 0 || port > 1)
        return false;
    Input port0{}, port1{};
    if (!session->inputs(port0, port1))
        return false;
    const Input &value = port == 0 ? port0 : port1;
    std::copy(value.begin(), value.end(), packet.begin() + 2);
    return true;
}

unsigned parse_number(const std::string &text, unsigned low, unsigned high) {
    char *end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (text.empty() || !end || *end || value < low || value > high)
        throw std::runtime_error("bad online setting: " + text);
    return unsigned(value);
}
}

bool parse_argument(const std::string &argument, Arguments &arguments) {
    if (argument.rfind("--netplay=", 0) == 0) {
        const std::string value = argument.substr(10);
        arguments.enabled = true;
        if (value.rfind("host", 0) == 0) {
            arguments.host = true;
            if (value.size() > 4 && value[4] == ':')
                arguments.port = uint16_t(parse_number(value.substr(5), 1, 65535));
        } else if (value.rfind("join:", 0) == 0) {
            arguments.host = false;
            std::string target = value.substr(5);
            const auto colon = target.rfind(':');
            if (colon != std::string::npos && target.find(':') == colon) {
                arguments.port = uint16_t(parse_number(target.substr(colon + 1), 1, 65535));
                target.resize(colon);
            }
            if (target.empty())
                throw std::runtime_error("no address to join");
            arguments.address = target;
        } else {
            throw std::runtime_error("bad online setting: " + value);
        }
        return true;
    }
    if (argument.rfind("--netplay-delay=", 0) == 0) {
        const std::string value = argument.substr(16);
        arguments.delay = value == "auto" ? 0u : parse_number(value, 1, max_delay);
        return true;
    }
    auto text = [&](const char *prefix, std::string &target, size_t limit) {
        const size_t length = std::strlen(prefix);
        if (argument.compare(0, length, prefix) != 0)
            return false;
        target = argument.substr(length);
        if (target.size() > limit)
            target.resize(limit);
        return true;
    };
    if (text("--netplay-password=", arguments.password, 64) || text("--netplay-lobby=", arguments.lobby, 255) ||
        text("--netplay-room=", arguments.room, 40) || text("--netplay-room-id=", arguments.room_id, 40) ||
        text("--netplay-invite-token=", arguments.invite_token, 64))
        return true;
    if (argument == "--netplay-list" || argument == "--netplay-invite-only") {
        arguments.listed = true;
        arguments.hidden = argument == "--netplay-invite-only";
        return true;
    }
    if (argument.rfind("--netplay-name=", 0) == 0) {
        std::string name = argument.substr(15);
        if (name.size() > 24)
            name.resize(24);
        if (!name.empty())
            arguments.name = name;
        return true;
    }
    return false;
}

void prepare_memory_card(const Arguments &arguments) {
    auto paths = PS2Runtime::getIoPaths();
    const auto root = card_root(arguments);
    std::error_code error;
    if (arguments.host) {
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(root);
        const auto own = paths.mcRoot;
        for (const auto &[from, to] : {std::pair{own, root / "mc0"}, std::pair{own.parent_path() / "mc1", root / "mc1"}})
            if (std::filesystem::is_directory(from, error))
                std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, error);
    }
    std::filesystem::create_directories(root / "mc0");
    paths.mcRoot = root / "mc0";
    PS2Runtime::setIoPaths(paths);
    std::cout << "[netplay] memory card " << paths.mcRoot.string() << std::endl;
}

std::unique_ptr<Session> start_game_session(PS2Runtime &runtime, const Arguments &arguments, const std::string &identity) {
    Options options;
    options.host = arguments.host;
    options.address = arguments.address;
    options.port = arguments.port;
    options.delay = arguments.delay;
    options.name = arguments.name;
    options.identity = identity;
    options.password = arguments.password;
    options.lobby = arguments.lobby;
    options.listed = arguments.host && arguments.listed;
    options.room = arguments.room;
    options.room_id = arguments.room_id;
    options.hidden = arguments.hidden;
    options.invite_token = arguments.invite_token;
    options.version = launcher::application_version;
    if (const char *rollback = std::getenv("OPENKAI2_NETPLAY_ROLLBACK"); rollback && *rollback == '0')
        options.rollback = false;
    if (const char *lag = std::getenv("OPENKAI2_NETPLAY_LAG_MS"); lag && *lag)
        options.lag_ms = unsigned(std::min(500ul, std::strtoul(lag, nullptr, 10)));
    const auto root = card_root(arguments);
    Machine machine;
    machine.tick = [&runtime] { return runtime.eeScheduler().currentVSyncTick(); };
    machine.save = [&runtime] {
        std::vector<uint8_t> body;
        StateArchive archive(body);
        serialize_machine(runtime.eeScheduler(), archive);
        return body;
    };
    machine.load = [&runtime](const std::vector<uint8_t> &body) {
        StateArchive archive(body);
        serialize_machine(runtime.eeScheduler(), archive);
        if (!archive.finished())
            throw std::runtime_error("The host's game state does not match this build");
    };
    const char *desyncTest = std::getenv("OPENKAI2_NETPLAY_DESYNC_TEST");
    const auto corrupting = std::make_shared<std::atomic<int>>(!arguments.host && desyncTest && *desyncTest == '1' ? 0 : -1);
    machine.load = [load = machine.load, corrupting](const std::vector<uint8_t> &body) {
        load(body);
        if (corrupting->load() > 1)
            corrupting->store(-1);
    };
    machine.hash = [&runtime, host = arguments.host, corrupting] {
        uint64_t value = machine_memory_hash();
        if (corrupting->load() >= 0 && corrupting->fetch_add(1) + 1 >= 2) {
            value ^= 1u;
            std::cout << "[netplay] desync test: reporting a wrong memory check" << std::endl;
        }
        if (const char *dump = std::getenv("OPENKAI2_NETPLAY_DUMP"); dump && *dump) {
            const auto tick = runtime.eeScheduler().currentVSyncTick();
            std::ofstream out(std::filesystem::path(dump) / ((host ? "host-" : "join-") + std::to_string(tick) + ".bin"),
                              std::ios::binary);
            out.write(reinterpret_cast<const char *>(runtime.memory().getRDRAM()), PS2_RAM_SIZE);
        }
        return value;
    };
    const char *autoplay = std::getenv("OPENKAI2_NETPLAY_AUTOPLAY");
    if (autoplay && *autoplay == '1') {
        machine.local_input = [&runtime, host = arguments.host] {
            static constexpr uint16_t hostPresses[] = {0x0008, 0x4000, 0x0040, 0x4000};
            static constexpr uint16_t guestPresses[] = {0x0020, 0x4000, 0x0010};
            const uint64_t tick = runtime.eeScheduler().currentVSyncTick();
            Input value = neutral_input();
            const uint64_t period = host ? 60 : 45;
            if (tick % period < 3) {
                const uint16_t press = host ? hostPresses[(tick / period) % 4] : guestPresses[(tick / period) % 3];
                value[0] = uint8_t(~press);
                value[1] = uint8_t(~press >> 8);
            }
            return value;
        };
    } else {
        machine.local_input = [] {
            const auto packet = consume_host_input(0);
            Input value{};
            std::copy(packet.begin() + 2, packet.begin() + 8, value.begin());
            return value;
        };
    }
    machine.stopping = [&runtime] { return runtime.isStopRequested(); };
    if (options.rollback) {
        auto states = std::make_shared<DeltaStates>(max_prediction + 4u,
                                                    std::clamp(std::thread::hardware_concurrency(), 2u, 4u) - 1u);
        states->pictureless(true);
        const auto serialize = [&runtime](StateArchive &archive) { serialize_machine(runtime.eeScheduler(), archive); };
        machine.capture = [states, serialize](uint64_t tick) {
            if (states->size() && tick <= states->newest())
                return;
            states->capture(tick, serialize);
        };
        machine.restore = [states, serialize](uint64_t tick) { return states->restore(tick, serialize); };
        machine.replaying = [](bool on) { replay_turbo.store(on, std::memory_order_relaxed); };
        machine.forget = [states] { states->clear(); };
    }
    machine.read_cards = [root] { return read_card_files(root); };
    machine.write_cards = [root](const std::vector<CardFile> &files) { write_card_files(root, files); };
    ps2_stubs::setStableCardDates(true);
    auto session = std::make_unique<Session>(options, std::move(machine));
    set_active(session.get());
    set_pad_source(&pad_source);
    session->start();
    std::cout << "[netplay] " << (arguments.host ? "hosting on port " + std::to_string(arguments.port)
                                                : "joining " + arguments.address + ":" + std::to_string(arguments.port))
              << " delay " << (arguments.delay ? std::to_string(arguments.delay) : std::string("auto")) << std::endl;
    return session;
}

void stop_game_session(std::unique_ptr<Session> &session) {
    if (!session)
        return;
    set_pad_source(nullptr);
    set_active(nullptr);
    replay_turbo.store(false, std::memory_order_relaxed);
    const auto stats = session->rollback_stats();
    std::cout << "[netplay] rollback predicted=" << stats.predicted << " mispredicted=" << stats.mispredicted
              << " rollbacks=" << stats.rollbacks << " replayed_frames=" << stats.replayed << " longest=" << stats.longest
              << " stalls=" << stats.stalls << " sync_waits=" << stats.waits << " sync_frames=" << stats.waited
              << " resyncs=" << stats.resyncs << std::endl;
    session->stop();
    session.reset();
}

}
