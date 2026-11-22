// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openkai2::retro {

inline constexpr unsigned budokai2_game = 19174;
inline constexpr const char *set_page = "https://retroachievements.org/game/19174";
inline constexpr const char *site = "https://retroachievements.org/";

struct Http {
    int status = 0;
    std::string body;
};
using Transport = std::function<Http(const std::string &url, const std::string &post, const std::string &type)>;
void set_transport(Transport transport);
Http send(const std::string &url, const std::string &post, const std::string &type);

std::string disc_hash(const std::filesystem::path &image);

struct LoginResult {
    bool ok = false;
    std::string user, token, error;
    unsigned score = 0, softcore = 0;
};
LoginResult login(const std::string &user, const std::string &password);

struct Lookup {
    bool ok = false;
    unsigned game = 0;
    std::string error;
};
Lookup resolve(const std::string &hash);

struct Entry {
    unsigned id = 0, points = 0;
    std::string title, description, badge;
    bool unlocked = false, hardcore = false;
    int64_t when = 0;
};
struct SetInfo {
    bool ok = false;
    unsigned game = 0;
    std::string title, hash, error;
    std::vector<Entry> entries;
    unsigned points() const;
    unsigned earned() const;
};
SetInfo fetch_set(const std::string &user, const std::string &token, unsigned game);
std::filesystem::path cache_path();
SetInfo load_cache(const std::filesystem::path &file);
void save_cache(const std::filesystem::path &file, const SetInfo &set);

struct Event {
    enum class Kind { unlocked, completed, message };
    Kind kind = Kind::message;
    std::string title, description;
    unsigned points = 0;
};

class Session {
  public:
    struct Config {
        std::string user, token, hash;
        bool hardcore = false;
        std::function<uint32_t(uint32_t address, uint8_t *buffer, uint32_t bytes)> read;
        std::filesystem::path cache;
    };
    explicit Session(Config config);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    void frame();
    std::vector<Event> take();
    std::string status() const;
    bool playing() const;

    struct State;

  private:
    std::unique_ptr<State> state_;
};

}
