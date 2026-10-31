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
        s.windowMode > 1 || s.antialiasing > 1 || s.textureFilter > 2 || s.upscaler > 3 ||
        s.aspect > 3 || s.volume > 100 || s.windowWidth < 0 || s.windowHeight < 0 ||
        s.windowWidth > 32768 || s.windowHeight > 32768 || s.updateChannel > 1 || (s.renderer != "auto" && s.renderer != "gpu" && s.renderer != "cpu"))
        throw std::runtime_error("Invalid graphics settings");
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
        else
            known = false; // Preserve forward compatibility with unrelated keys.
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
        << s.windowHeight << ' ' << s.windowMaximized << '\n';
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
} // namespace openkai2::launcher
