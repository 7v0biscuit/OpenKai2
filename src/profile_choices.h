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
inline constexpr std::array<const char *, 16> frame_names = {"Accent ring", "Saiyan gold", "Kamehameha glow", "Seven Dragon Balls",
                                                            "Scouter", "Capsule Corp", "Shenron coil", "Kaio-ken aura",
                                                            "Spirit Bomb", "Frieza ring", "Namekian", "Majin seal",
                                                            "Galick flare", "Destructo disc", "Solar flare", "Time chamber"};
inline constexpr unsigned frame_count = unsigned(frame_names.size());
inline constexpr std::array<const char *, 16> backdrop_names = {"Plain", "Starfield", "Speed lines", "Ki burst",
                                                               "Namek sky", "Tournament stage", "Capsule grid", "Dragon Radar",
                                                               "Kame House sea", "Time chamber void", "Red planet", "Lookout clouds",
                                                               "Lava wasteland", "Snowy peaks", "Cell Games ring", "Aurora"};
inline constexpr unsigned backdrop_count = unsigned(backdrop_names.size());
struct TitleChoice {
    const char *name;
    unsigned level;
    int achievement;
};
inline constexpr std::array<TitleChoice, 37> title_choices = {{
    {"No title", 0, -1}, {"Earthling", 1, -1}, {"Martial Artist", 2, -1}, {"Turtle School", 3, -1},
    {"Crane School", 3, -1}, {"Z Fighter", 5, -1}, {"Saiyan Warrior", 8, -1}, {"Namekian Sage", 8, -1},
    {"Super Saiyan", 12, -1}, {"Capsule Corp Ace", 12, -1}, {"World Champion", 0, 11}, {"Saga Hero", 0, 10},
    {"Untouchable", 0, 4}, {"Hercule's Rival", 0, 12}, {"Legendary Super Saiyan", 40, -1}, {"Prince of All Saiyans", 60, -1},
    {"Turtle Hermit", 10, -1}, {"Kaio-ken Master", 15, -1}, {"Fusion Warrior", 20, -1}, {"Elite Saiyan", 25, -1},
    {"Super Saiyan 2", 30, -1}, {"Mystic Warrior", 35, -1}, {"Guardian of Earth", 45, -1}, {"Super Saiyan 3", 50, -1},
    {"Kami's Successor", 70, -1}, {"Grandmaster", 80, -1}, {"Speed Demon", 0, 14}, {"No Second Chances", 0, 17},
    {"Beyond the Limit", 0, 18}, {"Record Breaker", 0, 19}, {"Unstoppable Force", 0, 22}, {"Online Contender", 0, 27},
    {"Limit Breaker", 0, 30}, {"Ultra Instinct", 0, 31}, {"Against All Odds", 0, 32}, {"Perfect Form", 0, 34},
    {"Online Rampage", 0, 40},
}};
inline constexpr unsigned title_count = unsigned(title_choices.size());
struct FrameColor {
    const char *name;
    unsigned char r, g, b;
};
inline constexpr std::array<FrameColor, 12> frame_colors = {{
    {"Frame's own colors", 0, 0, 0}, {"Gold", 255, 204, 64}, {"Silver", 214, 222, 236}, {"Super Saiyan Blue", 40, 180, 250},
    {"Namek green", 90, 210, 110}, {"Kaio-ken red", 250, 70, 50}, {"Rose", 255, 90, 170}, {"Frieza purple", 170, 100, 240},
    {"Ki white", 250, 250, 255}, {"Capsule blue", 70, 120, 230}, {"Majin pink", 255, 130, 200}, {"Ember orange", 255, 140, 40},
}};
inline constexpr unsigned frame_color_count = unsigned(frame_colors.size());

}
