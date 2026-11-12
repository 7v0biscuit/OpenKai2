// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/settings.h"
#include "speedrun.h"
#include "speedrun_images.h"
#include "disc_image.h"
#include "launcher/paths.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace openkai2::launcher {
SplitAppearance split_appearance(const Settings& settings, const std::string& run, const std::filesystem::path& image_directory) {
    SplitAppearance result;
    size_t start = 0;
    while (start <= settings.overlaySplitNames.size() && result.names.size() < 256) {
        const auto end = settings.overlaySplitNames.find(',', start);
        const auto name = settings.overlaySplitNames.substr(start, end == std::string::npos ? end : end-start);
        const auto first = name.find_first_not_of(" \t"), last = name.find_last_not_of(" \t");
        result.names.push_back(first == std::string::npos ? std::string() : name.substr(first,last-first+1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    while (!result.names.empty() && result.names.back().empty()) result.names.pop_back();
    for (const auto& style : settings.splitStyles) {
        if (style.run != run || style.index >= 256 || !speedrun::valid_image_file(style.image)) continue;
        if (result.names.size() <= style.index) result.names.resize(style.index+1);
        if (result.images.size() <= style.index) result.images.resize(style.index+1);
        result.names[style.index] = style.name;
        result.images[style.index] = style.image.empty() ? std::filesystem::path() : image_directory/style.image;
    }
    return result;
}

std::filesystem::path textures_directory() {
    return default_cache_directory().parent_path() / "textures";
}
std::filesystem::path settings_path() {
    return default_cache_directory().parent_path() / "settings.ini";
}
void validate_settings(const Settings &s) {
    if (s.fps > 240 || s.width < 640 || s.width > 7680 || s.height < 448 || s.height > 4320 ||
        s.windowMode > 1 || s.antialiasing > 3 || s.textureFilter > 2 || s.upscaler > 5 ||
        s.aspect > 3 || s.internalScale < 1 || s.internalScale > 4 || s.hdTextures > 4 || s.volume > 100 || s.cpuDifficulty > 6 || s.windowWidth < 0 || s.windowHeight < 0 ||
        s.windowWidth > 32768 || s.windowHeight > 32768 || s.updateChannel > 1 || s.speedrun >= speedrun::category_count || s.overlayItems > speedrun::overlay_all || s.overlayCorner > 3 ||
        s.overlaySize > 3 || s.overlayBackground > 4 || s.overlayMargin > 2 || s.overlayShape > 1 || s.overlayTitle.size() > 192 || s.overlaySplitNames.size() > 2400 || s.overlaySplits > 3 || s.overlayDecimals > 3 ||
        std::any_of(s.overlayColors.begin(), s.overlayColors.end(), [](unsigned c) { return c >= speedrun::overlay_color_count; }) || (s.renderer != "auto" && s.renderer != "gpu" && s.renderer != "vulkan" && s.renderer != "cpu"))
        throw std::runtime_error("Invalid graphics settings");
    if (s.onlinePort < 1 || s.onlinePort > 65535 || s.onlineDelay > 8 || s.onlineVisibility > 2 || s.onlineKeys.size() > 32 || s.onlineName.size() > 64 ||
        s.onlineAddress.size() > 255 || s.onlineRoom.size() > 64 || s.onlinePassword.size() > 64 ||
        s.onlineLobby.size() > 255 || s.onlineProfiles.size() > 32 || s.onlineRoomFilter > 2 || s.onlinePlayerFilter > 1)
        throw std::runtime_error("Invalid online settings");
    if (s.launcherTheme >= theme_count || s.launcherAccent >= accent_count || s.startPage > 1 || s.lastPage >= tab_count ||
        !valid_tab_order(s.tabOrder, s.hiddenTabs) || !valid_custom_colors(s.launcherColors))
        throw std::runtime_error("Invalid launcher appearance settings");
    std::set<std::string> profileIds;
    if (s.profileStyles.size() > 32) throw std::runtime_error("Too many profile appearances");
    for (const auto& style : s.profileStyles) {
        if (style.id.size()!=64 || !std::all_of(style.id.begin(),style.id.end(),[](unsigned char c){return std::isxdigit(c)!=0;}) ||
            !profileIds.insert(style.id).second || style.accent>=accent_count || style.initials.size()>8 || style.tagline.size()>192 ||
            style.initials.find_first_of("\r\n")!=std::string::npos || style.tagline.find_first_of("\r\n")!=std::string::npos ||
            !speedrun::valid_image_file(style.image) || style.imageName.size()>1024 || style.imageName.find_first_of("\r\n")!=std::string::npos)
            throw std::runtime_error("Invalid profile appearance");
    }
    if (s.splitStyles.size() > 2048)
        throw std::runtime_error("Too many split appearance entries");
    std::set<std::pair<std::string,unsigned>> splitRows;
    for (const auto& style : s.splitStyles) {
        if (style.run.empty() || style.run.size() > 128 || !std::all_of(style.run.begin(), style.run.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_' || c == '.';
            }) || style.index >= 256 || !splitRows.emplace(style.run, style.index).second ||
            style.name.size() > 192 || style.imageName.size() > 1024 ||
            style.name.find_first_of("\r\n") != std::string::npos || style.imageName.find_first_of("\r\n") != std::string::npos ||
            !speedrun::valid_image_file(style.image))
            throw std::runtime_error("Invalid split appearance");
    }
    if (!input::valid(s.controls))
        throw std::runtime_error("Invalid control bindings");
}
Settings read_settings(const std::filesystem::path &file) {
    Settings s;
    std::vector<std::array<std::string,3>> profileImages;
    bool tabLayout = false;
    std::ifstream in(file);
    if (!in) {
        if (std::filesystem::exists(file))
            throw std::runtime_error("Cannot read settings");
        return s;
    }
    std::string line;
    bool version = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        std::string key;
        row >> key;
        bool known = true;
        if (key == "version") {
            unsigned v = 0;
            row >> v;
            if (v != 1)
                throw std::runtime_error("Unsupported settings version");
            version = true;
        } else if (key == "iso") {
            std::string p;
            row >> std::quoted(p);
            s.iso = std::filesystem::u8path(p);
        } else if (key == "import_iso") {
            std::string p;
            row >> std::quoted(p);
            s.importIso = std::filesystem::u8path(p);
        } else if (key == "fps")
            row >> s.fps;
        else if (key == "width")
            row >> s.width;
        else if (key == "height")
            row >> s.height;
        else if (key == "window_mode")
            row >> s.windowMode;
        else if (key == "antialiasing")
            row >> s.antialiasing;
        else if (key == "texture_filter")
            row >> s.textureFilter;
        else if (key == "upscaler")
            row >> s.upscaler;
        else if (key == "aspect")
            row >> s.aspect;
        else if (key == "internal_scale")
            row >> s.internalScale;
        else if (key == "hd_textures")
            row >> s.hdTextures;
        else if (key == "music")
            row >> s.music;
        else if (key == "vsync")
            row >> s.vsync;
        else if (key == "audio")
            row >> s.audio;
        else if (key == "cpu_difficulty")
            row >> s.cpuDifficulty;
        else if (key == "volume")
            row >> s.volume;
        else if (key == "auto_backups")
            row >> s.autoBackups;
        else if (key == "auto_updates")
            row >> s.autoUpdates;
        else if (key == "discord_presence")
            row >> s.discordPresence;
        else if (key == "update_channel")
            row >> s.updateChannel;
        else if (key == "renderer")
            row >> std::quoted(s.renderer);
        else if (key == "launcher_window")
            row >> s.windowLeft >> s.windowTop >> s.windowWidth >> s.windowHeight >> s.windowMaximized;
        else if (key == "launcher_look" || key == "launcher_style" || key == "launcher_theme" || key == "launcher_scheme" || key == "launcher_skin") {
            row >> s.launcherTheme >> s.launcherAccent >> s.startPage >> s.lastPage;
            if (key == "launcher_look")
                s.launcherTheme = legacy_theme(s.launcherTheme);
            if (key == "launcher_look" || key == "launcher_style")
                s.launcherTheme = retired_theme(s.launcherTheme);
            if (key != "launcher_skin")
                s.launcherTheme = picture_theme(s.launcherTheme);
            if (key != "launcher_scheme" && key != "launcher_skin")
                s.launcherAccent = retired_accent(s.launcherAccent);
        }
        else if (key == "launcher_colors") {
            for (unsigned role = 0; role < color_role_count; ++role) {
                int32_t value = -1;
                if (!(row >> value))
                    break;
                s.launcherColors[role] = value;
            }
            row.clear();
        }
        else if (key == "launcher_tabs") {
            std::vector<unsigned> values;
            for (unsigned value = 0; row >> value;)
                values.push_back(value);
            row.clear();
            while (values.size() >= 2u && values.size() < tab_count + 1u)
                values.insert(values.end() - 1, unsigned(values.size() - 1u));
            if (values.size() != tab_count + 1u)
                throw std::runtime_error("Invalid settings value: " + key);
            std::copy(values.begin(), values.begin() + tab_count, s.tabOrder.begin());
            s.hiddenTabs = values.back();
        }
        else if (key == "controls_keys" || key == "controls_alternate" || key == "controls_pad") {
            auto &target = key == "controls_keys" ? s.controls.keys : key == "controls_alternate" ? s.controls.alternate : s.controls.pad;
            for (auto &value : target)
                row >> value;
        }
        else if (key == "launcher_tab_layout") {
            unsigned layout = 0;
            row >> layout;
            tabLayout = layout >= 2u;
        }
        else if (key == "speedrun")
            row >> s.speedrun >> s.permadeath;
        else if (key == "fps_counter")
            row >> s.fpsCounter;
        else if (key == "randomizer")
            row >> s.randomizer;
        else if (key == "ps2_load_times")
            row >> s.ps2LoadTimes;
        else if (key == "overlay")
        {
            row >> s.overlayItems >> s.overlayCorner >> s.overlaySize >> s.overlayBackground >> s.overlaySplits;
            s.overlayItems |= speedrun::overlay_required;
        }
        else if (key == "overlay_custom")
            row >> s.overlayMargin >> s.overlayShape >> std::quoted(s.overlayTitle) >> std::quoted(s.overlaySplitNames);
        else if (key == "split_style") {
            Settings::SplitStyle style;
            row >> std::quoted(style.run) >> style.index >> std::quoted(style.name) >> std::quoted(style.image) >> std::quoted(style.imageName);
            s.splitStyles.push_back(std::move(style));
        }
        else if (key == "overlay_style") {
            for (auto &color : s.overlayColors)
                row >> color;
            row >> s.overlayDecimals;
        }
        else if (key == "online")
            row >> std::quoted(s.onlineName) >> std::quoted(s.onlineAddress) >> s.onlinePort >> s.onlineDelay;
        else if (key == "online_room")
            row >> std::quoted(s.onlineRoom) >> std::quoted(s.onlinePassword) >> s.onlineListed >> std::quoted(s.onlineLobby);
        else if (key == "online_visibility")
            row >> s.onlineVisibility;
        else if (key == "online_filters")
            row >> s.onlineRoomFilter >> s.onlinePlayerFilter;
        else if (key == "profile_image") {
            std::array<std::string,3> image;
            row >> std::quoted(image[0]) >> std::quoted(image[1]) >> std::quoted(image[2]);
            profileImages.push_back(std::move(image));
        }
        else if (key == "profile_style") {
            Settings::ProfileStyle style;
            row >> std::quoted(style.id) >> style.accent >> std::quoted(style.initials) >> std::quoted(style.tagline);
            s.profileStyles.push_back(std::move(style));
        }
        else if (key == "online_friend") {
            Settings::Friend entry;
            row >> entry.owner >> entry.id >> std::quoted(entry.name);
            if (!row.fail() && s.onlineFriends.size() < 2000)
                s.onlineFriends.push_back(entry);
        } else if (key == "online_keys") {
            s.onlineKeys.clear();
            std::string rest;
            std::getline(row, rest);
            row.clear();
            std::istringstream keys(rest);
            std::string value;
            while (keys >> value)
                s.onlineKeys.push_back(value);
            row.setstate(std::ios::eofbit);
        }
        else if (key == "online_profiles") {
            s.onlineProfiles.clear();
            std::string rest;
            std::getline(row, rest);
            row.clear();
            std::istringstream names(rest);
            std::string name;
            while (names >> std::quoted(name))
                s.onlineProfiles.push_back(name);
            if (!names.eof())
                throw std::runtime_error("Invalid settings value: " + key);
            row.setstate(std::ios::eofbit);
        }
        else
            known = false;
        if (known) {
            if (row.fail())
                throw std::runtime_error("Invalid settings value: " + key);
            row >> std::ws;
            if (!row.eof())
                throw std::runtime_error("Invalid settings row: " + key);
        }
    }
    if (!version)
        throw std::runtime_error("Missing settings version");
    if (!tabLayout) {
        auto &order = s.tabOrder;
        const auto controls = std::find(order.begin(), order.end(), 7u);
        if (controls != order.end()) {
            std::rotate(controls, controls + 1, order.end());
            const auto before = std::find_if(order.begin(), order.end() - 1, [](unsigned tab) { return tab == 5u || tab == 6u; });
            std::rotate(before, order.end() - 1, order.end());
        }
    }
    std::set<std::string> imageIds;
    for (const auto& image : profileImages) {
        if(!imageIds.insert(image[0]).second)throw std::runtime_error("Duplicate profile image");
        auto found=std::find_if(s.profileStyles.begin(),s.profileStyles.end(),[&](const auto& style){return style.id==image[0];});
        if(found==s.profileStyles.end()) {
            s.profileStyles.push_back({image[0],{},{},0,{},{}});
            found=std::prev(s.profileStyles.end());
        }
        found->image=image[1];found->imageName=image[2];
    }
    validate_settings(s);
    if (s.hdTextures && s.internalScale == 1)
        s.internalScale = 2;
    return s;
}
void write_settings(const std::filesystem::path &file, const Settings &s) {
    validate_settings(s);
    std::filesystem::create_directories(file.parent_path());
    auto temporary = file;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("Cannot save settings");
    out << "# OpenKai2\nversion 1\niso " << std::quoted(path_utf8(s.iso)) << "\nfps " << s.fps
        << "\nwidth " << s.width << "\nheight " << s.height << "\nwindow_mode " << s.windowMode
        << "\nantialiasing " << s.antialiasing << "\ntexture_filter " << s.textureFilter
        << "\nupscaler " << s.upscaler << "\naspect " << s.aspect << "\ninternal_scale " << s.internalScale << "\nhd_textures " << s.hdTextures << "\nmusic " << s.music << "\nvsync " << s.vsync
        << "\naudio " << s.audio << "\nvolume " << s.volume << "\ncpu_difficulty " << s.cpuDifficulty
        << "\nauto_backups " << s.autoBackups << "\nauto_updates " << s.autoUpdates
        << "\nupdate_channel " << s.updateChannel << "\nrenderer " << std::quoted(s.renderer)
        << "\ndiscord_presence " << s.discordPresence << "\nimport_iso " << std::quoted(path_utf8(s.importIso))
        << "\nlauncher_window " << s.windowLeft << ' ' << s.windowTop << ' ' << s.windowWidth << ' '
        << s.windowHeight << ' ' << s.windowMaximized << "\nlauncher_skin " << s.launcherTheme << ' ' << s.launcherAccent << ' ' << s.startPage << ' ' << s.lastPage
        << "\nlauncher_tabs";
    for (unsigned tab : s.tabOrder)
        out << ' ' << tab;
    out << ' ' << s.hiddenTabs << "\nlauncher_tab_layout 2\nlauncher_colors";
    for (int32_t value : s.launcherColors)
        out << ' ' << value;
    out << "\nspeedrun " << s.speedrun << ' ' << s.permadeath
        << "\nrandomizer " << s.randomizer << "\nps2_load_times " << s.ps2LoadTimes;
    for (const auto &[name, values] : {std::pair{"controls_keys", &s.controls.keys}, {"controls_alternate", &s.controls.alternate},
                                       {"controls_pad", &s.controls.pad}}) {
        out << '\n' << name;
        for (int value : *values)
            out << ' ' << value;
    }
    out << "\nfps_counter " << s.fpsCounter << "\noverlay " << s.overlayItems << ' ' << s.overlayCorner << ' '
        << s.overlaySize << ' ' << s.overlayBackground << ' ' << s.overlaySplits << "\noverlay_style "
        << s.overlayColors[0] << ' ' << s.overlayColors[1] << ' ' << s.overlayColors[2] << ' ' << s.overlayColors[3] << ' '
        << s.overlayDecimals << "\noverlay_custom " << s.overlayMargin << ' ' << s.overlayShape << ' '
        << std::quoted(s.overlayTitle) << ' ' << std::quoted(s.overlaySplitNames) << "\nonline "
        << std::quoted(s.onlineName) << ' ' << std::quoted(s.onlineAddress) << ' ' << s.onlinePort << ' '
        << s.onlineDelay << "\nonline_room " << std::quoted(s.onlineRoom) << ' ' << std::quoted(s.onlinePassword) << ' '
        << s.onlineListed << ' ' << std::quoted(s.onlineLobby) << "\nonline_profiles";
    for (const auto &name : s.onlineProfiles)
        out << ' ' << std::quoted(name);
    out << "\nonline_keys";
    for (const auto &value : s.onlineKeys)
        out << ' ' << value;
    out << "\nonline_visibility " << s.onlineVisibility << '\n';
    for (const auto &entry : s.onlineFriends)
        out << "online_friend " << entry.owner << ' ' << entry.id << ' ' << std::quoted(entry.name) << '\n';
    out << "online_filters " << s.onlineRoomFilter << ' ' << s.onlinePlayerFilter << '\n';
    for (const auto& style : s.profileStyles) {
        if(!style.image.empty())out << "profile_image " << std::quoted(style.id) << ' ' << std::quoted(style.image) << ' ' << std::quoted(style.imageName) << '\n';
        out << "profile_style " << std::quoted(style.id) << ' ' << style.accent << ' ' << std::quoted(style.initials) << ' ' << std::quoted(style.tagline) << '\n';
    }
    for (const auto& style : s.splitStyles)
        out << "split_style " << std::quoted(style.run) << ' ' << style.index << ' ' << std::quoted(style.name) << ' '
            << std::quoted(style.image) << ' ' << std::quoted(style.imageName) << '\n';
    out.close();
    if (!out)
        throw std::runtime_error("Cannot finish saving settings");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), file.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings file");
#else
    std::filesystem::rename(temporary, file);
#endif
}
std::wstring quote_argument(const std::wstring &v) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : v) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result += c;
        } else {
            result.append(slashes, L'\\');
            result += c;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}
}
