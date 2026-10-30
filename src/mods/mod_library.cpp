// SPDX-License-Identifier: GPL-3.0-only
#include "mods/mod_library.h"
#include "disc_image.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>

namespace openkai2::mods {
namespace fs = std::filesystem;
namespace {
std::string trim(std::string text) {
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), space));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), space).base(), text.end());
    return text;
}
std::string utf8(const fs::path &path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
fs::path from_utf8(const std::string &text) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
#else
    return fs::u8path(text);
#endif
}
size_t count_files(const fs::path &root) {
    std::error_code error;
    if (!fs::is_directory(root, error))
        return 0;
    size_t count = 0;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error))
            ++count;
    return count;
}
ModInfo read_mod(const fs::path &folder) {
    ModInfo mod;
    mod.folder = folder;
    mod.id = utf8(folder.filename());
    mod.name = mod.id;
    std::ifstream in(folder / "mod.ini");
    for (std::string line; std::getline(in, line);) {
        const auto equals = line.find('=');
        if (line.empty() || line[0] == '#' || line[0] == ';' || equals == std::string::npos)
            continue;
        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1));
        if (key == "name" && !value.empty())
            mod.name = value;
        else if (key == "author")
            mod.author = value;
        else if (key == "version")
            mod.version = value;
        else if (key == "description")
            mod.description = value;
    }
    mod.replaced_files = count_files(folder / "files");
    mod.archive_entries = count_files(folder / "afs");
    return mod;
}
} // namespace

fs::path default_mods_directory() { return default_cache_directory().parent_path() / "mods"; }

std::vector<ModInfo> scan_mods(const fs::path &directory) {
    std::map<std::string, ModInfo> found;
    std::error_code error;
    if (fs::is_directory(directory, error))
        for (const auto &item : fs::directory_iterator(directory, error))
            if (item.is_directory(error) && utf8(item.path().filename()).front() != '.' &&
                utf8(item.path().filename()) != reports_folder)
                found.emplace(utf8(item.path().filename()), read_mod(item.path()));
    std::vector<ModInfo> ordered;
    std::ifstream order(directory / "load-order.txt");
    for (std::string line; std::getline(order, line);) {
        line = trim(line);
        if (line.size() < 2 || (line[0] != '+' && line[0] != '-'))
            continue;
        const auto mod = found.find(line.substr(1));
        if (mod == found.end())
            continue;
        mod->second.enabled = line[0] == '+';
        ordered.push_back(std::move(mod->second));
        found.erase(mod);
    }
    for (auto &[id, mod] : found)
        ordered.push_back(std::move(mod));
    return ordered;
}

void save_load_order(const fs::path &directory, const std::vector<ModInfo> &mods) {
    fs::create_directories(directory);
    const auto temporary = directory / "load-order.txt.tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "# Mods load top to bottom; later ones win. + enabled, - disabled.\n";
        for (const auto &mod : mods)
            out << (mod.enabled ? '+' : '-') << mod.id << '\n';
        if (!out)
            throw std::runtime_error("Cannot write the mod load order");
    }
    fs::rename(temporary, directory / "load-order.txt");
}

std::vector<fs::path> enabled_mod_folders(const fs::path &directory) {
    std::vector<fs::path> folders;
    for (const auto &mod : scan_mods(directory))
        if (mod.enabled)
            folders.push_back(mod.folder);
    return folders;
}

fs::path create_mod(const fs::path &directory, const std::string &name, const std::string &author,
                    const std::string &description) {
    std::string id;
    for (unsigned char c : name)
        id += std::isalnum(c) || c == '-' || c == '_' ? char(c) : c == ' ' ? '_' : char(0);
    id.erase(std::remove(id.begin(), id.end(), char(0)), id.end());
    if (id.empty())
        id = "mod";
    auto folder = directory / from_utf8(id);
    for (unsigned n = 2; fs::exists(folder); ++n)
        folder = directory / from_utf8(id + "_" + std::to_string(n));
    fs::create_directories(folder);
    std::ofstream ini(folder / "mod.ini");
    ini << "name=" << name << "\nauthor=" << author << "\nversion=1.0\ndescription=" << description << '\n';
    if (!ini)
        throw std::runtime_error("Cannot create the mod folder");
    ini.close();
    auto mods = scan_mods(directory);
    save_load_order(directory, mods);
    return folder;
}

void rename_mod(const fs::path &folder, const std::string &name) {
    if (trim(name).empty())
        throw std::runtime_error("Type a name for the mod");
    std::vector<std::string> lines;
    bool named = false;
    {
        std::ifstream in(folder / "mod.ini");
        for (std::string line; std::getline(in, line);) {
            const auto equals = line.find('=');
            if (equals != std::string::npos && trim(line.substr(0, equals)) == "name") {
                if (named)
                    continue;
                line = "name=" + trim(name);
                named = true;
            }
            lines.push_back(line);
        }
    }
    if (!named)
        lines.insert(lines.begin(), "name=" + trim(name));
    const auto temporary = folder / "mod.ini.tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        for (const auto &line : lines)
            out << line << '\n';
        if (!out)
            throw std::runtime_error("Cannot write the mod's mod.ini");
    }
    fs::rename(temporary, folder / "mod.ini");
}

void delete_mod(const fs::path &directory, const std::string &id) {
    const auto folder = directory / from_utf8(id);
    if (id.empty() || id == "." || id == ".." || id.find_first_of("/\\") != std::string::npos ||
        !fs::is_directory(folder))
        throw std::runtime_error("No mod folder named " + id);
    fs::remove_all(folder);
    save_load_order(directory, scan_mods(directory));
}

} // namespace openkai2::mods
