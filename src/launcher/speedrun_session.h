// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "speedrun_rules.h"
#include <filesystem>

namespace openkai2::launcher {
class SpeedrunCards {
  public:
    SpeedrunCards() = default;
    ~SpeedrunCards();
    SpeedrunCards(const SpeedrunCards &) = delete;
    SpeedrunCards &operator=(const SpeedrunCards &) = delete;
    std::filesystem::path prepare(const std::filesystem::path &card, speedrun::RunProfile profile);
  private:
    std::filesystem::path directory_;
};
}
