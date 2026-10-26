// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace openkai2::launcher {
struct UpdateAsset {
    std::string name, url;
    uint64_t size = 0;
};
struct UpdateRelease {
    std::string version, tag;
    bool prerelease = false;
    std::vector<UpdateAsset> assets;
};
// Strict semantic version precedence; returns -1, 0 or 1, and rejects malformed versions.
int compare_versions(const std::string &a, const std::string &b);
std::optional<UpdateRelease> select_release(const std::string &json,
                                           const std::string &current, bool prereleases);
const UpdateAsset &update_asset(const UpdateRelease &release, const std::string &name);
bool trusted_asset_url(const std::string &url, const std::string &tag,
                       const std::string &name);
std::string checksum_for(const std::string &checksums, const std::string &name);
struct StagedUpdate {
    std::filesystem::path directory, package, target;
    std::string version, sha256;
    bool portable = false;
};
#ifdef _WIN32
std::optional<UpdateRelease> check_for_updates(bool prereleases);
StagedUpdate stage_update(const UpdateRelease &, const std::function<void(const std::string &)> &);
// Starts the local update worker and waits for its ready signal before the launcher exits.
void apply_update(const StagedUpdate &);
#endif
} // namespace openkai2::launcher
