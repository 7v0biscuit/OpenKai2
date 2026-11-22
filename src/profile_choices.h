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
inline constexpr std::array<const char *, 8> region_names = {"North America East", "North America West", "South America", "Europe",
                                                            "Africa", "Middle East", "Asia", "Oceania"};
inline constexpr unsigned region_count = unsigned(region_names.size());
inline constexpr std::array<const char *, 4> playstyle_names = {"Casual", "Competitive", "Learning", "Speedrunner"};
inline constexpr unsigned playstyle_count = unsigned(playstyle_names.size());
inline constexpr std::array<const char *, 12> frame_names = {"Accent ring", "Saiyan gold", "Kamehameha glow", "Seven Dragon Balls",
                                                            "Scouter", "Capsule Corp", "Shenron coil", "Kaio-ken aura",
                                                            "Spirit Bomb", "Frieza ring", "Namekian", "Majin seal"};
inline constexpr unsigned frame_count = unsigned(frame_names.size());
inline constexpr std::array<const char *, 8> backdrop_names = {"Plain", "Starfield", "Speed lines", "Ki burst",
                                                              "Namek sky", "Tournament stage", "Capsule grid", "Dragon Radar"};
inline constexpr unsigned backdrop_count = unsigned(backdrop_names.size());
struct TitleChoice {
    const char *name;
    unsigned level;
    int achievement;
};
inline constexpr std::array<TitleChoice, 16> title_choices = {{
    {"No title", 0, -1}, {"Earthling", 1, -1}, {"Martial Artist", 2, -1}, {"Turtle School", 3, -1},
    {"Crane School", 3, -1}, {"Z Fighter", 5, -1}, {"Saiyan Warrior", 8, -1}, {"Namekian Sage", 8, -1},
    {"Super Saiyan", 12, -1}, {"Capsule Corp Ace", 12, -1}, {"World Champion", 0, 11}, {"Saga Hero", 0, 10},
    {"Untouchable", 0, 4}, {"Hercule's Rival", 0, 12}, {"Legendary Super Saiyan", 40, -1}, {"Prince of All Saiyans", 60, -1},
}};
inline constexpr unsigned title_count = unsigned(title_choices.size());

}
