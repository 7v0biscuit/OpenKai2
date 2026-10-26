// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
namespace openkai2::launcher {
int run_window(const std::filesystem::path &initialIso = {});
}
