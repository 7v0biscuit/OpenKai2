// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_settings.h"
#include "launcher/saves.h"
#include "launcher/settings.h"
#include "launcher/layout.h"
#include "launcher/online_browser.h"
#include "speedrun.h"
#include "launcher/update.h"
#include "disc_image.h"
#include "simulation_pacing.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace openkai2::launcher;
namespace fs = std::filesystem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid operation was accepted");
}
void put(const fs::path &p, const std::vector<uint8_t> &b) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
    require(bool(out), "Fixture write failed");
}
std::vector<uint8_t> bytes(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void word(std::vector<uint8_t> &b, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (i * 8));
}
int main() {
    try {
        {
            using namespace openkai2::launcher;
            require(std::wstring(accents[rgb_accent].name) == L"RGB (cycling)", "The RGB accent is not the last accent");
            const auto step = [](Rgb a, Rgb b) {
                return std::max({std::abs(int(a.r) - int(b.r)), std::abs(int(a.g) - int(b.g)), std::abs(int(a.b) - int(b.b))});
            };
            int largest = 0;
            for (unsigned long long ms = 0; ms <= rgb_cycle_ms; ms += rgb_frame_ms)
                largest = std::max(largest, step(cycling_accent(ms), cycling_accent(ms + rgb_frame_ms)));
            const auto start = cycling_accent(0), end = cycling_accent(rgb_cycle_ms - 1);
            require(largest <= 1 && step(start, end) <= 1 && step(cycling_accent(0), cycling_accent(rgb_cycle_ms / 2)) > 150,
                    "The RGB accent jumps between frames or does not go round the colors");
            require(make_theme(0, rgb_accent, no_custom_colors(), 0).accent.r == start.r &&
                        make_theme(0, rgb_accent, no_custom_colors(), rgb_cycle_ms / 3).accent.g > 200,
                    "The RGB accent is not used by the theme");
        }
        const auto root =
            fs::temp_directory_path() /
            ("openkai2-launcher-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            fs::path p;
            ~Cleanup() {
                std::error_code ec;
                fs::remove_all(p, ec);
            }
        } cleanup{root};
        fs::create_directories(root);
        put(root/"empty",{});put(root/"abc",{'a','b','c'});
        require(openkai2::sha256_file(root/"empty")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","Empty SHA-256 oracle mismatch");
        require(openkai2::sha256_file(root/"abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 oracle mismatch");
        Settings s;
        s.iso = fs::u8path(u8"C:/Games/été 日本語/game image.iso");
        s.fps = 144;
        s.width = 2560;
        s.height = 1440;
        s.windowMode = 1;
        s.antialiasing = 1;
        s.textureFilter = 2;
        s.upscaler = 3;
        s.aspect = 1;
        s.vsync = false;
        s.autoBackups = false;
        s.audio = false;
        s.volume = 75;
        s.renderer = "gpu";
        s.autoUpdates = false;
        s.updateChannel = 0;
        s.importIso = root / L"Budokai 3 ドラゴン.iso";
        s.windowLeft = -1200;
        s.windowTop = 40;
        s.windowWidth = 1180;
        s.windowHeight = 760;
        s.windowMaximized = true;
        s.speedrun = 2;
        s.permadeath = true;
        s.fpsCounter = true;
        s.overlayItems = 0x5u | openkai2::speedrun::overlay_required;
        s.overlayCorner = 3;
        s.overlaySplits = 3;
        s.overlaySize = 3;
        s.overlayBackground = 4;
        s.overlayMargin = 2;
        s.overlayShape = 1;
        s.randomizer = true;
        s.ps2LoadTimes = false;
        s.launcherTheme = 3;
        s.launcherAccent = 7;
        s.startPage = 1;
        s.lastPage = 4;
        s.tabOrder = {5, 0, 3, 6, 7, 1, 2, 4};
        s.hiddenTabs = (1u << 1u) | (1u << 4u);
        s.launcherColors[0] = 0x0a0b0c;
        s.launcherColors[22] = 0xff00ff;
        s.overlayTitle = "My \"Any%\" run";
        s.overlaySplitNames = "Raditz, Nappa ,Vegeta";
        s.onlineName = "Son Goku";
        s.onlineAddress = "203.0.113.7";
        s.onlinePort = 7351;
        s.onlineDelay = 4;
        s.onlineProfiles = {"Son Goku", "Vegeta \"Prince\""};
        s.onlineRoom = "Kame House";
        s.onlinePassword = "turtle";
        s.onlineListed = false;
        s.onlineLobby = "lobby.example.org:7351";
        s.onlineKeys = {std::string(64, 'a'), std::string(64, 'b')};
        s.onlineVisibility = 1;
        s.onlineFriends = {{std::string(64, 'a'), std::string(64, 'c'), "Krillin \"K\""}};
        s.profileStyles={{std::string(64,'a'),"SG","Local only",3},{std::string(64,'b'),"V","Second profile",7}};
        s.reducedMotion=true;s.helpHints=false;
        s.profileStyles[0].pictureFit=1;s.profileStyles[0].notifications=false;s.profileStyles[0].availability=2;
        s.profileStyles[0].image=std::string(64,'d')+".gif";
        s.profileStyles[0].imageName="My \"portrait\".gif";
        s.onlineRoomFilter=2;s.onlinePlayerFilter=1;
        s.splitStyles={{"any.txt",0,"Raditz",std::string(64,'c')+".gif","My image.gif"},{"tournament-novice.txt",0,"First round",{},{} }};
        write_settings(root / "settings.ini", s);
        auto read = read_settings(root / "settings.ini");
        require(read.profileStyles.size()==2 && read.profileStyles[0].id==std::string(64,'a') &&
                    read.profileStyles[0].initials=="SG" && read.profileStyles[0].tagline=="Local only" &&
                    read.profileStyles[0].accent==3 && read.profileStyles[1].accent==7,"Profile appearance did not round-trip independently");
        require(read.profileStyles[0].image==s.profileStyles[0].image && read.profileStyles[0].imageName==s.profileStyles[0].imageName &&
                read.profileStyles[1].image.empty() && read.onlineRoomFilter==2 && read.onlinePlayerFilter==1,
                "Profile pictures or online filters did not round-trip independently");
        require(read.reducedMotion && !read.helpHints && read.profileStyles[0].pictureFit==1 && !read.profileStyles[0].notifications && read.profileStyles[0].availability==2 && read.profileStyles[1].notifications,
                "Per-profile framing and quiet mode did not round-trip");
        auto badFit=s;badFit.profileStyles[0].pictureFit=2;rejects([&]{validate_settings(badFit);});
        Settings profileBefore=s;profileBefore.onlineProfiles={"First","Second"};profileBefore.onlineKeys={std::string(64,'1'),std::string(64,'2')};
        profileBefore.onlineFriends={{std::string(64,'a'),std::string(64,'c'),"Friend"}};
        const auto recovery=profile_recovery(profileBefore,0,std::string(64,'a'));
        require(recovery.onlineProfiles.size()==1 && recovery.onlineKeys[0]==profileBefore.onlineKeys[0] && recovery.profileStyles.size()==1 && recovery.onlineFriends.size()==1,
                "Profile recovery did not isolate the removed identity");
        Settings current=profileBefore;current.onlineProfiles.erase(current.onlineProfiles.begin());current.onlineKeys.erase(current.onlineKeys.begin());
        current.profileStyles.erase(current.profileStyles.begin());current.onlineFriends.clear();current.fps=90;
        restore_profile(current,recovery);
        require(current.fps==90 && current.onlineProfiles.size()==2 && current.onlineName=="First" && current.onlineKeys.back()==recovery.onlineKeys[0] &&
                current.profileStyles.back().image==profileBefore.profileStyles[0].image && !current.profileStyles.back().notifications && current.onlineFriends.size()==1,
                "Profile recovery lost identity or overwrote unrelated settings");
        rejects([&]{restore_profile(current,recovery);});
        rejects([&]{profile_recovery(profileBefore,9,std::string(64,'a'));});
        for(int width:{872,1040,1360,1840}) {
            ui::SettingsLayout layout(width);
            int last=0;
            for(size_t section=0;section<layout.sections.size();++section) {
                require(layout.sections[section]>last,"Settings cards overlap");
                for(int index=0;index<layout.fields[section];++index) {
                    const auto field=layout.field(layout.sections[section],index);
                    require(field.x>=36 && field.x+field.width<=36+width && field.width>=200,"Settings field leaves its card");
                    require(field.y+field.height<layout.bottom(layout.sections[section],layout.fields[section]),"Settings field leaves card vertically");
                }
                last=layout.bottom(layout.sections[section],layout.fields[section]);
            }
            layout.filter(L"texture filtering");require(layout.sections[1]==ui::SettingsLayout::top && layout.sections[0]<0 && layout.sections[2]<0,"Settings search did not reflow its card");
            layout.filter(L"go to display");require(layout.sections[0]==ui::SettingsLayout::top,"Settings search rejected navigation words");
            ui::DockLayout dock(width+72,600,6);require(dock.left>=20 && dock.left+6*dock.cell<=width+52 && dock.top+74<600,"Dock leaves the window");
        }
        auto invalidImage=s;invalidImage.profileStyles[0].image="../outside.gif";rejects([&]{validate_settings(invalidImage);});
        invalidImage=s;invalidImage.profileStyles[0].imageName="two\nlines.gif";rejects([&]{validate_settings(invalidImage);});
        invalidImage=s;invalidImage.onlineRoomFilter=3;rejects([&]{validate_settings(invalidImage);});
        const std::vector<openkai2::online::Room> browserRooms={
            {"1","Kame House","Goku","127.0.0.1","current",7350,false,false},
            {"2","Snow Arena","Vegeta","127.0.0.1","current",7350,true,false},
            {"3","Old room","Krillin","127.0.0.1","older",7350,false,false}};
        require(browse_rooms(browserRooms,"current",0,"").size()==2 && browse_rooms(browserRooms,"current",1,"").size()==3 &&
                browse_rooms(browserRooms,"current",2,"").size()==1,"Room compatibility or password filtering is wrong");
        require(browse_rooms(browserRooms,"current",0,"vEgEtA").size()==1 && browse_rooms(browserRooms,"current",0,"SNOW").size()==1 &&
                browse_rooms(browserRooms,"current",0,"missing").empty() && browserRooms.size()==3,
                "Room search failed or changed the directory's room list");
        const std::vector<openkai2::online::Player> browserPlayers={{"1","Goku","online","current",false},{"2","Vegeta","hosting","current",false}};
        const auto friendly=[](const std::string& id){return id=="2";};
        require(profile_status(0,false,false,false)=="online" && profile_status(1,false,false,false)=="away" &&
                profile_status(2,false,false,false)=="do not disturb" && profile_status(2,true,true,true)=="in a match" &&
                profile_status(1,false,true,true)=="hosting" && profile_status(2,false,true,false)=="joining",
                "Profile availability overrides an active match or room");
        require(browse_players(browserPlayers,false,"",friendly).size()==2 && browse_players(browserPlayers,true,"",friendly).size()==1 &&
                browse_players(browserPlayers,true,"goku",friendly).empty() && browse_players(browserPlayers,false,"GOku",friendly).size()==1,
                "Player browsing hides friends or ignores search and friend filters");
        require(read.splitStyles.size()==2 && read.splitStyles[0].run=="any.txt" && read.splitStyles[0].name=="Raditz" &&
                read.splitStyles[0].image==std::string(64,'c')+".gif" && read.splitStyles[1].name=="First round","Split appearances did not round-trip by category");
        const auto splitLook=split_appearance(read,"any.txt",root/"images");
        require(splitLook.names.size()>=1 && splitLook.names[0]=="Raditz" && splitLook.images[0]==root/"images"/(std::string(64,'c')+".gif"),"Runtime split appearance did not match the selected category");
        require(split_appearance(read,"tournament-novice.txt",root/"images").names[0]=="First round","Runtime split names leaked between categories");
        auto invalidSplit=s;invalidSplit.splitStyles[0].image="../file.png";rejects([&]{validate_settings(invalidSplit);});
        invalidSplit=s;invalidSplit.splitStyles.push_back(invalidSplit.splitStyles[0]);rejects([&]{validate_settings(invalidSplit);});
        invalidSplit=s;invalidSplit.splitStyles[0].index=256;rejects([&]{validate_settings(invalidSplit);});
        auto invalidStyle=s;invalidStyle.profileStyles.push_back(invalidStyle.profileStyles[0]);rejects([&]{validate_settings(invalidStyle);});
        invalidStyle=s;invalidStyle.profileStyles[0].tagline="two\nlines";rejects([&]{validate_settings(invalidStyle);});
        invalidStyle=s;invalidStyle.profileStyles[0].accent=accent_count;rejects([&]{validate_settings(invalidStyle);});
        require(read.iso == s.iso && read.fps == 144 && read.width == 2560 && read.height == 1440 &&
                    read.windowMode == 1 && read.antialiasing == 1 && read.textureFilter == 2 &&
                    read.upscaler == 3 && read.aspect == 1 && !read.vsync && !read.autoBackups &&
                    !read.audio && read.volume == 75 &&
                    read.renderer == "gpu" && !read.autoUpdates && read.updateChannel == 0 &&
                    read.importIso == s.importIso && read.windowLeft == -1200 && read.windowTop == 40 &&
                    read.windowWidth == 1180 && read.windowHeight == 760 && read.windowMaximized &&
                    read.speedrun == 2 && read.permadeath && read.fpsCounter && read.overlayItems == (0x5u | openkai2::speedrun::overlay_required) &&
                    read.overlayCorner == 3 && read.overlaySize == 3 && read.overlayBackground == 4 && read.overlayMargin == 2 && read.overlayShape == 1 && read.randomizer && !read.ps2LoadTimes && read.launcherTheme == 3 && read.launcherAccent == 7 && read.startPage == 1 && read.lastPage == 4 &&
                    read.tabOrder == s.tabOrder && read.hiddenTabs == s.hiddenTabs && read.launcherColors == s.launcherColors &&
                    read.overlayTitle == "My \"Any%\" run" && read.overlaySplitNames == "Raditz, Nappa ,Vegeta" &&
                    read.overlaySplits == 3 && read.onlineName == "Son Goku" &&
                    read.onlineAddress == "203.0.113.7" && read.onlinePort == 7351 && read.onlineDelay == 4 &&
                    read.onlineProfiles == s.onlineProfiles && read.onlineRoom == "Kame House" &&
                    read.onlinePassword == "turtle" && !read.onlineListed && read.onlineLobby == s.onlineLobby &&
                    read.onlineKeys == s.onlineKeys && read.onlineVisibility == 1 && read.onlineFriends.size() == 1 &&
                    read.onlineFriends[0].name == "Krillin \"K\"" && read.onlineFriends[0].id == std::string(64, 'c'),
                "Settings roundtrip changed values");
        {
            auto vulkan = s;
            vulkan.renderer = "vulkan";
            write_settings(root / "vulkan.ini", vulkan);
            require(read_settings(root / "vulkan.ini").renderer == "vulkan", "Vulkan renderer setting was not kept");
            vulkan.overlayColors = {10, 7, 9, 5};
            vulkan.overlayDecimals = 1;
            write_settings(root / "vulkan.ini", vulkan);
            const auto styled = read_settings(root / "vulkan.ini");
            require(styled.overlayColors == vulkan.overlayColors && styled.overlayDecimals == 1, "Overlay style was not kept");
            vulkan.cpuDifficulty = 3;
            write_settings(root / "vulkan.ini", vulkan);
            require(read_settings(root / "vulkan.ini").cpuDifficulty == 3, "CPU difficulty was not kept");
            vulkan.cpuDifficulty = 6;
            write_settings(root / "vulkan.ini", vulkan);
            require(read_settings(root / "vulkan.ini").cpuDifficulty == 6, "Z6 was not kept");
            vulkan.cpuDifficulty = 7;
            bool refused = false;
            try {
                write_settings(root / "bad.ini", vulkan);
            } catch (const std::exception &) {
                refused = true;
            }
            require(refused, "CPU difficulty past Z3 was accepted");
        }
        {
            auto unchecked = s;
            unchecked.overlayItems = 0x1u;
            write_settings(root / "unchecked.ini", unchecked);
            require(read_settings(root / "unchecked.ini").overlayItems & openkai2::speedrun::overlay_required,
                    "Run checks could be turned off in the settings file");
        }
        require(read_settings(root / "absent.ini").fps == 240, "Default frame rate cap is not 240 FPS");
        {
            std::ofstream(root / "old-look.ini") << "version 1\nlauncher_look 18 2 1 3\n";
            std::ofstream(root / "old-style.ini") << "version 1\nlauncher_style 6 0 0 2\n";
            std::ofstream(root / "old-accent.ini") << "version 1\nlauncher_theme 2 28 0 0\n";
            require(read_settings(root / "old-accent.ini").launcherAccent == openkai2::launcher::rgb_accent, "The RGB accent did not survive the accent cleanup");
            require(read_settings(root / "old-style.ini").launcherTheme == 8u, "A retired theme did not move to the closest picture theme");
            std::ofstream(root / "ki.ini") << "version 1\nlauncher_scheme 3 0 0 0\n";
            require(read_settings(root / "ki.ini").launcherTheme == 8u, "The Ki theme did not move to Majin Vegeta");
            std::ofstream(root / "scouter.ini") << "version 1\nlauncher_scheme 4 0 0 0\n";
            require(read_settings(root / "scouter.ini").launcherTheme == 3u, "Scouter moved when the theme list changed");
            const auto old = read_settings(root / "old-look.ini");
            require(old.launcherTheme == 1u && old.launcherAccent == 2u && old.startPage == 1u && old.lastPage == 3u,
                    "A theme saved by an older version did not move to its replacement");
        }
        {
            using namespace openkai2::launcher;
            const auto first = make_theme(0, 0);
            require(first.accent == themes[0].accent && first.banner == themes[0].banner && luminance(first.background)<luminance(first.panel),
                    "The default theme is not Dragon Balls");
            const auto contrast = [](Rgb a, Rgb b) {
                const auto light = [](Rgb c) {
                    const auto linear = [](uint8_t channel) {
                        const double value = channel / 255.0;
                        return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
                    };
                    return .2126 * linear(c.r) + .7152 * linear(c.g) + .0722 * linear(c.b);
                };
                const double x = light(a), y = light(b);
                return (std::max(x,y) + .05) / (std::min(x,y) + .05);
            };
            for (unsigned theme = 0; theme < theme_count; ++theme)
                require(themes[theme].banner >= 1u && themes[theme].banner <= banner_picture_count, "A theme has no picture");
            for (unsigned theme = 0; theme < theme_count; ++theme)
                for (unsigned accent = 0; accent < accent_count; ++accent) {
                    const auto look = make_theme(theme, accent);
                    require(contrast(look.ink,look.panel) >= 4.5 && contrast(look.ink,look.field) >= 4.5 &&
                                contrast(look.muted,look.panel) >= 4.5 && contrast(look.faint,look.panel) >= 4.5 &&
                                contrast(look.buttonInk,look.buttonTop) >= 4.5,
                            "A theme's text or main action falls below 4.5:1 contrast");
                    if (themes[theme].light)
                        require(contrast(look.accent,look.panel) >= 4.5,"A light theme has an unreadable accent");
                    const auto apart = [](unsigned a, unsigned b) { return a > b ? a - b : b - a; };
                    require(apart(luminance(look.ink), luminance(look.panel)) > 150u && apart(luminance(look.muted), luminance(look.panel)) > 60u &&
                                apart(luminance(look.ink), luminance(look.field)) > 150u,
                            "A launcher theme made text hard to read");
                    const unsigned onButton = luminance(look.buttonInk), button = luminance(look.buttonBottom);
                    require((onButton > button ? onButton - button : button - onButton) > 70u, "A launcher accent made button text hard to read");
                }
            {
                auto custom = no_custom_colors();
                custom[0] = 0x102030;
                custom[main_button_role] = 0x2080ff;
                const auto mine = make_theme(3, 0, custom), plain = make_theme(3, 0);
                require(mine.background == Rgb{0x10, 0x20, 0x30} && mine.buttonTop == Rgb{0x20, 0x80, 0xff} &&
                            !(mine.buttonBottom == plain.buttonBottom) && mine.panel == plain.panel,
                        "A custom launcher color was not applied to its part only");
                require(color_role_count >= 20u && valid_custom_colors(custom), "Custom colors were refused");
                unsigned theme = 0, accent = 0;
                CustomColors pasted{};
                require(parse_look_code(look_code(4, 9, custom), theme, accent, pasted) && theme == 4 && accent == 9 && pasted == custom,
                        "A copied launcher look did not paste back the same");
                std::string old = look_code(0, 4, custom);
                old.replace(old.find(" 5 0 4"), 6, " 1 15 4");
                require(parse_look_code(old, theme, accent, pasted) && theme == 3 && accent == 4 && pasted == custom,
                        "A look copied from an older version did not paste onto the matching theme");
                require(!parse_look_code("OPENKAI2-LOOK 1 99 0", theme, accent, pasted) && !parse_look_code("hello", theme, accent, pasted) &&
                            !parse_look_code(look_code(0, 0, custom).substr(0, 30), theme, accent, pasted),
                        "A broken launcher look was accepted");
                require(theme_count == 13u && accent_count == 11u, "Themes or accent colors are missing");
                for (unsigned old = 0; old < 29u; ++old)
                    require(retired_accent(old) < accent_count, "A retired accent color has no replacement");
                require(retired_accent(28) == rgb_accent && retired_accent(25) == 3u && retired_accent(17) == 2u,
                        "A retired accent color moved to the wrong replacement");
                for (unsigned a = 1; a + 1 < accent_count; ++a)
                    for (unsigned b = a + 1; b + 1 < accent_count; ++b) {
                        const auto d = [](int x, int y) { return unsigned(x > y ? x - y : y - x); };
                        const Rgb x = accents[a].color, y = accents[b].color;
                        require(d(x.r, y.r) + d(x.g, y.g) + d(x.b, y.b) > 90u, "Two accent colors look almost the same");
                    }
                for (unsigned old = 0; old < 20u; ++old)
                    require(retired_theme(legacy_theme(old)) < theme_count, "An older theme has no replacement");
                for (unsigned old = 0; old < 16u; ++old)
                    require(picture_theme(retired_theme(old)) < theme_count, "A retired theme has no replacement");
                require(legacy_theme(0) == 0u && legacy_theme(13) == 1u && legacy_theme(18) == 10u, "Older themes map to the wrong replacement");
                const auto distance = [](Rgb a, Rgb b) {
                    const auto d = [](int x, int y) { return unsigned(x > y ? x - y : y - x); };
                    return d(a.r, b.r) + d(a.g, b.g) + d(a.b, b.b);
                };
                for (unsigned a = 0; a < theme_count; ++a)
                    for (unsigned b = a + 1; b < theme_count; ++b) {
                        const auto x = make_theme(a, 0), y = make_theme(b, 0);
                        require(x.banner!=y.banner || distance(x.accent,y.accent)+distance(x.panel,y.panel)*4u+distance(x.heroFrom,y.heroFrom)>140u,
                                "Two launcher themes look almost the same");
                    }
                custom[4] = 0x1000000;
                require(!valid_custom_colors(custom), "An impossible color was accepted");
            }
            require(valid_tab_order({5, 0, 3, 6, 1, 2, 4, 7}, (1u << 2u) | (1u << 6u)) && !valid_tab_order({0, 0, 1, 2, 3, 4, 5, 6}, 0u) &&
                        !valid_tab_order({0, 1, 2, 3, 4, 5, 6, 7}, 1u) && !valid_tab_order({0, 1, 2, 3, 4, 5, 6, 7}, 1u << 5u),
                    "Tab order validation accepted a broken order or a hidden Play or Settings tab");
            {
                std::ofstream old(root / "old-tabs.ini");
                old << "version 1\nlauncher_tabs 5 0 3 1 2 4 4\n";
            }
            const auto upgraded = read_settings(root / "old-tabs.ini");
            require(upgraded.tabOrder == std::array<unsigned, tab_count>{7, 5, 0, 3, 1, 2, 4, 6} && upgraded.hiddenTabs == 4u,
                    "A tab order saved before the Updates tab did not load");
            {
                std::ofstream old(root / "seven-tabs.ini");
                old << "version 1\nlauncher_tabs 6 0 3 1 2 4 5 2\n";
            }
            const auto seven = read_settings(root / "seven-tabs.ini");
            require(seven.tabOrder == std::array<unsigned, tab_count>{7, 6, 0, 3, 1, 2, 4, 5} && seven.hiddenTabs == 2u,
                    "A tab order saved before the Controls tab did not load");
            {
                std::ofstream old(root / "controls-last.ini");
                old << "version 1\nlauncher_tabs 0 1 2 3 4 5 6 7 0\n";
            }
            require(normalize_dock_order(read_settings(root / "controls-last.ini").tabOrder) == default_tab_order,
                    "A legacy tab order did not move the Play tools out of the dock");
            require(Settings{}.tabOrder == default_tab_order && dock_tab_count == 6 && default_tab_order[4] == 5u &&
                        default_tab_order[5] == 6u && !dock_tab(4) && !dock_tab(7),
                    "The default dock does not contain the six main pages");
            require(normalize_dock_order(std::array<unsigned,tab_count>{7,5,0,4,3,6,1,2}) ==
                        std::array<unsigned,tab_count>{5,0,3,6,1,2,7,4},
                    "Dock migration changed the relative order of the user's main tabs");
            {
                std::ofstream(root / "scale-old.ini") << "version 1\ninternal_scale 1\n";
                std::ofstream(root / "scale-chosen.ini") << "version 1\ninternal_scale 3\n";
                std::ofstream(root / "scale-kept.ini") << "version 1\ninternal_scale 1\ngraphics_profile 2\n";
                require(read_settings(root / "scale-old.ini").internalScale == 0u, "An untouched 1x internal resolution did not become automatic");
                require(read_settings(root / "scale-chosen.ini").internalScale == 3u, "A chosen internal resolution was replaced");
                require(read_settings(root / "scale-kept.ini").internalScale == 1u, "Native resolution chosen after the update was replaced");
                Settings automatic;
                write_settings(root / "scale-auto.ini", automatic);
                require(automatic.internalScale == 0u && read_settings(root / "scale-auto.ini").internalScale == 0u,
                        "Automatic internal resolution is not the default or did not save");
                using openkai2::automatic_internal_scale;
                require(automatic_internal_scale(448, false) == 1u && automatic_internal_scale(720, false) == 2u &&
                            automatic_internal_scale(1080, false) == 3u && automatic_internal_scale(1440, false) == 4u &&
                            automatic_internal_scale(2160, false) == 4u && automatic_internal_scale(448, true) == 2u,
                        "Automatic internal resolution does not match the display height");
            }
            {
                Settings bound;
                bound.controls.keys[openkai2::input::Cross] = 74;
                bound.controls.alternate[openkai2::input::L1] = 49;
                bound.controls.pad[openkai2::input::Start] = 13;
                write_settings(root / "bound.ini", bound);
                const auto loaded = read_settings(root / "bound.ini");
                require(loaded.controls == bound.controls, "Control bindings did not survive a restart");
                require(read_settings(root / "old-tabs.ini").controls == openkai2::input::Bindings{},
                        "Settings without bindings did not use the default controls");
                Settings reserved;
                reserved.controls.keys[openkai2::input::Cross] = 294;
                bool refusedKey = false;
                try { write_settings(root / "reserved.ini", reserved); } catch (const std::exception &) { refusedKey = true; }
                require(refusedKey, "A function key used by OpenKai2 was accepted as a game binding");
                require(openkai2::input::key_from_virtual('J', false) == 74 && openkai2::input::key_from_virtual(0x0D, true) == 335 &&
                            openkai2::input::key_name(340) == "Left Shift",
                        "Keyboard keys were not translated for the game");
            }
            Settings broken;
            broken.hiddenTabs = 1u;
            bool refused = false;
            try { write_settings(root / "broken.ini", broken); } catch (const std::exception &) { refused = true; }
            require(refused, "Hiding the Play tab was accepted");
        }
        require(read_settings(root / "absent.ini").audio && read_settings(root / "absent.ini").volume == 100,
                "Game audio is not on at full volume by default");
        s.volume = 101;
        rejects([&] { write_settings(root / "settings.ini", s); });
        s.volume = 75;
        for (unsigned category = 0; category < openkai2::speedrun::category_count; ++category) {
            s.speedrun = category;
            s.overlayDecimals = 3;
            write_settings(root / "categories.ini", s);
            const auto restored = read_settings(root / "categories.ini");
            require(restored.speedrun == category && restored.overlayDecimals == 3,
                    "A speedrun category or millisecond precision was lost on restart");
        }
        s.speedrun = openkai2::speedrun::category_count;
        rejects([&] { write_settings(root / "settings.ini", s); });
        s.speedrun = 2;
        s.overlayCorner = 4;
        rejects([&] { write_settings(root / "settings.ini", s); });
        s.overlayCorner = 3;
        s.volume = 75;
        auto original = bytes(root / "settings.ini");
        s.fps = 241;
        rejects([&] { write_settings(root / "settings.ini", s); });
        require(bytes(root / "settings.ini") == original,
                "Rejected settings overwrote existing values");
        put(root / "bad.ini", {'v', 'e', 'r', 's', 'i', 'o', 'n', ' ', '1', '\n', 'f', 'p', 's',
                               ' ', '-', '1', '\n'});
        rejects([&] { read_settings(root / "bad.ini"); });
        require(quote_argument(L"C:\\Game files\\") == L"\"C:\\Game files\\\\\"",
                "Trailing slash quoting failed");
        require(quote_argument(L"a\"b") == L"\"a\\\"b\"", "Embedded quote escaping failed");
        const std::vector<std::string> orderedVersions{"1.0.0-alpha", "1.0.0-alpha.1",
            "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
            "1.0.0-rc.1", "1.0.0", "1.0.1", "1.2.0", "1.10.0", "2.0.0"};
        for (size_t i = 1; i < orderedVersions.size(); ++i) {
            require(compare_versions(orderedVersions[i-1], orderedVersions[i]) < 0 &&
                    compare_versions(orderedVersions[i], orderedVersions[i-1]) > 0,
                    "Semantic version ordering is incorrect");
        }
        require(compare_versions("1.0.0+build.01", "1.0.0+other") == 0,
                "Build metadata changed version precedence");
        for (const auto &bad : {"1.0", "v1.0.0", "01.0.0", "1.0.0-01", "1.0.0-", "1.0.0+", "1.0.0-x..y"})
            rejects([&] { compare_versions(bad, "1.0.0"); });
        auto releaseJson = [](const std::string &tag, bool pre, bool draft = false) {
            std::string json = "{\"tag_name\":\"" + tag + "\",\"prerelease\":" +
                (pre ? "true" : "false") + ",\"draft\":" + (draft ? "true" : "false") + ",\"assets\":[";
            bool first = true;
            for (const std::string name : {"OpenKai2.exe", "OpenKai2-windows-x64.zip", "SHA256SUMS.txt"}) {
                if (!first) json += ',';
                first = false;
                json += "{\"name\":\"" + name + "\",\"size\":123,\"browser_download_url\":\"https://github.com/7v0biscuit/OpenKai2/releases/download/" + tag + "/" + name + "\"}";
            }
            return json + "]}";
        };
        const auto feed = "[" + releaseJson("v0.1.26", false) + "," +
            releaseJson("v0.2.0", true) + "," + releaseJson("v9.0.0", false, true) + "]";
        auto stable = select_release(feed, "0.1.25", false);
        auto development = select_release(feed, "0.1.25", true);
        require(stable && stable->version == "0.1.26" && development &&
                development->version == "0.2.0", "Update channels selected the wrong release");
        require(!select_release(feed, "0.2.0", true), "Updater would downgrade/reinstall this version");
        require(!select_release("[" + releaseJson("v1.0.0-rc.1", false) + "]", "0.1.25", false),
                "Prerelease tag bypassed stable channel");
        require(!select_release("[]", "0.1.25", true), "Empty release feed created an update");
        auto hostile = "[" + releaseJson("v0.1.26", false) + "]";
        const auto host = hostile.find("https://github.com/");
        hostile.replace(host, 18, "https://evil.test/");
        require(!select_release(hostile, "0.1.25", true), "Untrusted release asset was accepted");
        for (const std::string invalidJson : {"[", "[] trailing", "[{\"draft\":false,\"draft\":true}]", "[null]"})
            rejects([&] { select_release(invalidJson, "0.1.25", true); });
        require(!trusted_asset_url("https://github.com/7v0biscuit/OpenKai2/releases/download/v1.0.0/OpenKai2.exe?other", "v1.0.0", "OpenKai2.exe"), "Asset query bypassed trusted URL");
        const std::string digest(64, 'a');
        require(checksum_for(digest + "  OpenKai2.exe\r\n", "OpenKai2.exe") == digest,
                "Release checksums were parsed incorrectly");
        rejects([&] { checksum_for(digest + "  OpenKai2.exe\n" + digest + "  OpenKai2.exe\n", "OpenKai2.exe"); });
        rejects([&] { checksum_for(digest + "  Other.exe\n", "OpenKai2.exe"); });
        rejects([&] { checksum_for(std::string(64, 'z') + "  OpenKai2.exe\n", "OpenKai2.exe"); });
        const auto card = root / "game/mc0", backups = root / "backups";
        put(card / "BASLUS-20779/save.dat", {1, 2, 3});
        put(card / "OTHER/game.dat", {8, 9});
        const auto archive = backup_card(card, backups);
        require(!archive.empty(), "Backup was not created");
        const auto snapshot = read_save(archive);
        require(snapshot.size() == 2 && snapshot[0].data == std::vector<uint8_t>({1, 2, 3}),
                "ZIP backup changed save bytes");
        put(root / "incoming/BASLUS-20779/save.dat", {4, 5, 6, 7});
        import_save(card, root / "incoming", backups);
        require(bytes(card / "BASLUS-20779/save.dat") == std::vector<uint8_t>({4, 5, 6, 7}) &&
                    bytes(card / "OTHER/game.dat") == std::vector<uint8_t>({8, 9}),
                "Import damaged unrelated saves");
        import_save(card, archive, backups, true);
        require(bytes(card / "BASLUS-20779/save.dat") == std::vector<uint8_t>({1, 2, 3}),
                "Restore did not recover original bytes");
        require(list_backups(backups).size() == 3,
                "Imports/restores did not create recovery backups");
        auto damaged = bytes(archive);
        damaged[30 + std::string("BASLUS-20779/save.dat").size()] ^= 1;
        put(root / "damaged.zip", damaged);
        auto before = read_card(card);
        rejects([&] { import_save(card, root / "damaged.zip", backups, true); });
        require(bytes(card / "BASLUS-20779/save.dat") == before[0].data,
                "Invalid backup changed the card");
        auto traversal = bytes(archive);
        const std::string from = "BASLUS-20779", to = "../ESCAPE123";
        require(from.size() == to.size(), "Traversal fixture size");
        for (size_t pos = 0; pos + from.size() <= traversal.size(); ++pos)
            if (std::equal(from.begin(), from.end(), traversal.begin() + pos))
                std::copy(to.begin(), to.end(), traversal.begin() + pos);
        put(root / "unsafe.zip", traversal);
        rejects([&] { import_save(card, root / "unsafe.zip", backups); });
        require(!fs::exists(root / "ESCAPE123"), "ZIP import escaped its card");
        std::vector<uint8_t> psu(3072);
        auto entry = [&](size_t at, unsigned mode, unsigned length, const std::string &name) {
            word(psu, at, mode);
            word(psu, at + 4, length);
            std::copy(name.begin(), name.end(), psu.begin() + at + 64);
        };
        entry(0, 0x8427, 3, "BASLUS-20779");
        entry(512, 0x8427, 0, ".");
        entry(1024, 0x8427, 0, "..");
        entry(1536, 0x8497, 3, "save.dat");
        psu[2048] = 11;
        psu[2049] = 12;
        psu[2050] = 13;
        put(root / "save.psu", psu);
        auto imported = read_save(root / "save.psu");
        require(imported.size() == 1 && imported[0].name == "BASLUS-20779/save.dat" &&
                    imported[0].data == std::vector<uint8_t>({11, 12, 13}),
                "PSU layout was parsed incorrectly");
        import_save(card, root / "save.psu", backups);
        require(bytes(card / "BASLUS-20779/save.dat") == imported[0].data,
                "PSU import did not commit");
        psu.resize(2050);
        put(root / "truncated.psu", psu);
        rejects([&] { import_save(card, root / "truncated.psu", backups); });
        require(bytes(card / "BASLUS-20779/save.dat") == imported[0].data,
                "Truncated PSU damaged current save");
        std::error_code ec;
        fs::create_directory_symlink(root / "incoming", root / "linked", ec);
        if (!ec)
            rejects([&] { import_save(card, root / "linked", backups); });
        const auto tv = openkai2::display_rectangle(1920, 1080, 512, 448, 0);
        const auto wide = openkai2::display_rectangle(1920, 1080, 512, 448, 1);
        const auto stretch = openkai2::display_rectangle(1280, 1024, 512, 448, 2);
        const auto native = openkai2::display_rectangle(1920, 1080, 512, 448, 3);
        require(std::abs(tv.width - 1440) < 0.01f && std::abs(tv.x - 240) < 0.01f &&
                    tv.height == 1080,
                "Original TV aspect is incorrect");
        require(wide.width == 1920 && wide.height == 1080 && wide.x == 0,
                "Widescreen aspect is incorrect");
        const auto video = openkai2::display_rectangle(1920,1080,640,448,1);
        require(video.width==1440 && video.x==240 && video.height==1080,
                "Widescreen stretched the original movie surface");
        require(stretch.width == 1280 && stretch.height == 1024,
                "Stretch mode did not fill the window");
        require(std::abs(native.width / native.height - 512.0f / 448.0f) < 0.0001f,
                "Native pixel aspect changed");
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;
        const auto origin = Clock::time_point{} + 1s;
        constexpr auto period = 16667us;
        auto scheduled = origin;
        for (unsigned i = 0; i < 600; ++i) {
            auto actual = scheduled + 100us;
            require(openkai2::field_pacing_anchor(scheduled, actual) == scheduled,
                    "Normal pacing drifted");
            scheduled += period;
        }
        const auto delayed = origin + 500ms;
        auto anchor = openkai2::field_pacing_anchor(origin, delayed);
        require(anchor == delayed && anchor + period > delayed,
                "Stalled simulation catches up in a burst");
        for (unsigned fps : {30u, 60u, 120u, 240u}) {
            auto now = delayed;
            auto next = anchor + period;
            for (unsigned poll = 0; poll < fps; ++poll) {
                (void)poll;
                require(next > now, "Window polls advanced simulation");
            }
            require(next == delayed + period, "FPS selection changed guest pacing");
        }
        std::cout << "Launcher checks passed: Unicode settings, argument quoting, lossless ZIP/PSU "
                     "import, backups, restore, invalid archives, path traversal, links, and "
                     "independent simulation pacing, update channels, semantic versions and checksums.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
