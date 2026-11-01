// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/settings.h"
#include "disc_image.h"
#include "launcher/paths.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace openkai2::launcher {
std::filesystem::path settings_path() {
    return default_cache_directory().parent_path() / "settings.ini";
}
void validate_settings(const Settings &s) {
    if (s.fps > 240 || s.width < 640 || s.width > 7680 || s.height < 448 || s.height > 4320 ||
        s.windowMode > 1 || s.antialiasing > 1 || s.textureFilter > 2 || s.upscaler > 5 ||
        s.aspect > 3 || s.volume > 100 || s.windowWidth < 0 || s.windowHeight < 0 ||
        s.windowWidth > 32768 || s.windowHeight > 32768 || s.updateChannel > 1 || s.speedrun > 2 || s.overlayItems > speedrun::overlay_all || s.overlayCorner > 3 ||
        s.overlaySize > 2 || s.overlayBackground > 2 || s.overlaySplits > 3 || (s.renderer != "auto" && s.renderer != "gpu" && s.renderer != "cpu"))
        throw std::runtime_error("Invalid graphics settings");
    if (s.onlinePort < 1 || s.onlinePort > 65535 || s.onlineDelay > 8 || s.onlineVisibility > 2 || s.onlineKeys.size() > 32 || s.onlineName.size() > 64 ||
        s.onlineAddress.size() > 255 || s.onlineRoom.size() > 64 || s.onlinePassword.size() > 64 ||
        s.onlineLobby.size() > 255 || s.onlineProfiles.size() > 32)
        throw std::runtime_error("Invalid online settings");
}
Settings read_settings(const std::filesystem::path &file) {
    Settings s;
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
        else if (key == "vsync")
            row >> s.vsync;
        else if (key == "audio")
            row >> s.audio;
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
        else if (key == "speedrun")
            row >> s.speedrun >> s.permadeath;
        else if (key == "fps_counter")
            row >> s.fpsCounter;
        else if (key == "overlay")
            row >> s.overlayItems >> s.overlayCorner >> s.overlaySize >> s.overlayBackground >> s.overlaySplits;
        else if (key == "online")
            row >> std::quoted(s.onlineName) >> std::quoted(s.onlineAddress) >> s.onlinePort >> s.onlineDelay;
        else if (key == "online_room")
            row >> std::quoted(s.onlineRoom) >> std::quoted(s.onlinePassword) >> s.onlineListed >> std::quoted(s.onlineLobby);
        else if (key == "online_visibility")
            row >> s.onlineVisibility;
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
    validate_settings(s);
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
        << "\nupscaler " << s.upscaler << "\naspect " << s.aspect << "\nvsync " << s.vsync
        << "\naudio " << s.audio << "\nvolume " << s.volume
        << "\nauto_backups " << s.autoBackups << "\nauto_updates " << s.autoUpdates
        << "\nupdate_channel " << s.updateChannel << "\nrenderer " << std::quoted(s.renderer)
        << "\ndiscord_presence " << s.discordPresence << "\nimport_iso " << std::quoted(path_utf8(s.importIso))
        << "\nlauncher_window " << s.windowLeft << ' ' << s.windowTop << ' ' << s.windowWidth << ' '
        << s.windowHeight << ' ' << s.windowMaximized << "\nspeedrun " << s.speedrun << ' ' << s.permadeath
        << "\nfps_counter " << s.fpsCounter << "\noverlay " << s.overlayItems << ' ' << s.overlayCorner << ' '
        << s.overlaySize << ' ' << s.overlayBackground << ' ' << s.overlaySplits << "\nonline "
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
