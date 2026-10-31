// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
#include <functional>
#include <string>

namespace openkai2 {
enum class PresenceStatus { off, waiting, connected };
class DiscordPresence {
  public:
    explicit DiscordPresence(std::wstring testPipe = {});
    ~DiscordPresence();
    DiscordPresence(const DiscordPresence &) = delete;
    DiscordPresence &operator=(const DiscordPresence &) = delete;
    void enable(bool enabled);
    void watch_enabled(std::function<bool()> reader);
    void update(std::string details, std::string state);
    PresenceStatus status() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
namespace discord {
inline constexpr char application_id[] = "1565006652468625510";
inline constexpr char image_asset[] = "openkai2";
std::string activity_payload(unsigned pid, const std::string &details, const std::string &state,
                             long long started, unsigned long long nonce, bool clear = false);
}
}
