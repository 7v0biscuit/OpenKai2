// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>

namespace openkai2 {

inline constexpr std::array<const char *, 36> fighter_names = {
    "Goku", "Kid Gohan", "Teen Gohan", "Vegeta", "Krillin", "Trunks", "Piccolo", "Tien", "Yamcha", "Raditz",
    "Nappa", "Captain Ginyu", "Recoome", "Zarbon", "Dodoria", "Frieza", "Android 16", "Android 17", "Android 18",
    "Android 19", "Cell", "Hercule", "Saibaman", "Cell Jr.", "Great Saiyaman", "Dr. Gero", "Goten", "Kid Trunks",
    "Gohan", "Majin Buu", "Super Buu", "Kid Buu", "Dabura", "Videl", "Majin Vegeta", "Supreme Kai"};
inline constexpr unsigned fighter_count = unsigned(fighter_names.size());

}
