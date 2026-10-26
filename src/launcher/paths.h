// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
namespace openkai2::launcher {
inline std::string path_utf8(const std::filesystem::path &path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
} // namespace openkai2::launcher
