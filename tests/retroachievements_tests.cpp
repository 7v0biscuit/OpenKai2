// SPDX-License-Identifier: GPL-3.0-only
#include "retroachievements.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace openkai2;
void require(bool passed, const std::string &message) {
    if (!passed)
        throw std::runtime_error(message);
}

const char *patch = "{\"Success\":true,\"GameId\":19174,\"Title\":\"Dragon Ball Z: Budokai 2\",\"ConsoleId\":21,"
                    "\"ImageIconUrl\":\"http://server/Images/1.png\",\"RichPresenceGameId\":19174,\"RichPresencePatch\":\"\",\"Sets\":[{"
                    "\"AchievementSetId\":1,\"GameId\":19174,\"Title\":null,\"Type\":\"core\",\"ImageIconUrl\":\"http://server/Images/1.png\","
                    "\"Achievements\":["
                    "{\"ID\":7001,\"Title\":\"Test Kamehameha\",\"Description\":\"Set the test byte\",\"Flags\":3,\"Points\":10,"
                    "\"MemAddr\":\"0xH0010=5\",\"Author\":\"Test\",\"BadgeName\":\"00001\",\"Created\":1,\"Modified\":1},"
                    "{\"ID\":7002,\"Title\":\"Already Earned\",\"Description\":\"Unlocked before\",\"Flags\":3,\"Points\":5,"
                    "\"MemAddr\":\"0xH0011=1\",\"Author\":\"Test\",\"BadgeName\":\"00002\",\"Created\":1,\"Modified\":1}"
                    "],\"Leaderboards\":[]}]}";

std::mutex seen_mutex;
std::vector<std::string> seen;

retro::Http fake(const std::string &url, const std::string &post, const std::string &) {
    {
        std::lock_guard lock(seen_mutex);
        seen.push_back(post);
    }
    if (url.find("retroachievements.org") == std::string::npos)
        return {404, ""};
    if (post.find("r=login2") != std::string::npos) {
        if (post.find("p=wrong") != std::string::npos)
            return {401, "{\"Success\":false,\"Error\":\"Invalid username/password.\",\"Code\":\"invalid_credentials\"}"};
        return {200, "{\"Success\":true,\"User\":\"Goku\",\"Token\":\"TOKEN123\",\"Score\":120,\"SoftcoreScore\":40,\"Messages\":0}"};
    }
    if (post.find("r=gameid") != std::string::npos)
        return {200, post.find("m=9358fc389ad2873c0a15653de9b1ce68") != std::string::npos ? "{\"Success\":true,\"GameID\":0}" : "{\"Success\":true,\"GameID\":19174}"};
    if (post.find("r=achievementsets") != std::string::npos) {
        if (post.find("m=unlinked") != std::string::npos)
            return {404, "{\"Success\":false,\"Error\":\"Unknown game\",\"Code\":\"not_found\",\"Status\":404}"};
        return {200, patch};
    }
    if (post.find("r=startsession") != std::string::npos)
        return {200, "{\"Success\":true,\"Unlocks\":[{\"ID\":7002,\"When\":1700000000}],\"HardcoreUnlocks\":[],\"ServerNow\":1700000100}"};
    if (post.find("r=awardachievement") != std::string::npos)
        return {200, "{\"Success\":true,\"Score\":130,\"SoftcoreScore\":50,\"AchievementID\":7001,\"AchievementsRemaining\":0}"};
    return {200, "{\"Success\":true}"};
}

bool saw(const char *needle) {
    std::lock_guard lock(seen_mutex);
    for (const auto &post : seen)
        if (post.find(needle) != std::string::npos)
            return true;
    return false;
}
}

int main() {
    try {
        retro::set_transport(fake);
        const auto good = retro::login("Goku", "secret");
        require(good.ok && good.user == "Goku" && good.token == "TOKEN123" && good.score == 120, "Sign-in did not return the token and score");
        require(saw("p=secret"), "The password was not sent to the sign-in request");
        const auto bad = retro::login("Goku", "wrong");
        require(!bad.ok && bad.token.empty() && !bad.error.empty(), "A wrong password signed in");

        const auto unlinked = retro::resolve("9358fc389ad2873c0a15653de9b1ce68");
        require(unlinked.ok && unlinked.game == 0, "An unlinked hash resolved to a game");
        const auto linked = retro::resolve("c2a0157c94a7e6ac4092a35a01f708ab");
        require(linked.ok && linked.game == 19174, "A linked hash did not resolve");

        const auto set = retro::fetch_set("Goku", "TOKEN123", 19174);
        require(set.ok && set.title == "Dragon Ball Z: Budokai 2" && set.entries.size() == 2 && set.points() == 15,
                "The achievement set was not read");

        const auto root = std::filesystem::temp_directory_path() / "openkai2-retro-tests";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        std::vector<uint8_t> memory(0x2000000u, 0);
        std::atomic<bool> reading{false};
        retro::Session::Config config;
        config.user = "Goku";
        config.token = "TOKEN123";
        config.hash = "linkedhash";
        config.cache = root / "retroachievements.txt";
        config.read = [&](uint32_t address, uint8_t *buffer, uint32_t bytes) -> uint32_t {
            reading = true;
            if (size_t(address) + bytes > memory.size())
                return 0;
            std::memcpy(buffer, memory.data() + address, bytes);
            return bytes;
        };
        {
            retro::Session session(config);
            for (int i = 0; i < 400 && !session.playing(); ++i) {
                session.frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(session.playing(), "The game did not load: " + session.status());
            require(session.status().find("1 of 2") != std::string::npos, "Earlier unlocks were not counted: " + session.status());
            for (int i = 0; i < 5; ++i)
                session.frame();
            memory[0x10] = 5;
            std::vector<retro::Event> events;
            for (int i = 0; i < 400; ++i) {
                session.frame();
                for (auto &event : session.take())
                    events.push_back(event);
                if (saw("r=awardachievement"))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            bool unlocked = false;
            for (const auto &event : events)
                if (event.kind == retro::Event::Kind::unlocked && event.title == "Test Kamehameha" && event.points == 10)
                    unlocked = true;
            require(unlocked, "Setting the memory value did not unlock the achievement");
            require(reading, "The session never read game memory");
            require(saw("r=awardachievement") && saw("a=7001"), "The unlock was not sent to RetroAchievements");
            require(!saw("a=7002"), "An achievement that was already earned was sent again");
        }
        const auto cached = retro::load_cache(root / "retroachievements.txt");
        require(cached.ok && cached.game == 19174 && cached.entries.size() == 2 && cached.earned() == 15,
                "The launcher cache did not record both unlocks");

        config.hash = "unlinked";
        {
            retro::Session session(config);
            for (int i = 0; i < 400 && session.status().find("not linked") == std::string::npos; ++i) {
                session.frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(!session.playing() && session.status().find("not linked") != std::string::npos,
                    "An unlinked disc did not report that it is not linked: " + session.status());
        }
        config.token.clear();
        {
            retro::Session session(config);
            require(!session.playing() && session.status().find("Sign in") != std::string::npos, "A signed-out session tried to play");
        }
        std::filesystem::remove_all(root);
        std::cout << "RetroAchievements tests passed." << std::endl;
        return 0;
    } catch (const std::exception &problem) {
        std::cerr << "::error title=RetroAchievements::" << problem.what() << std::endl;
        return 1;
    }
}
