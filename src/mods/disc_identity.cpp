// SPDX-License-Identifier: GPL-3.0-only
#include "mods/disc_identity.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace openkai2::mods {
namespace {
std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return text;
}
uint32_t read32(const std::vector<uint8_t> &bytes, size_t at) {
    return at + 4 <= bytes.size() ? bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | uint32_t(bytes[at + 3]) << 24
                                  : 0u;
}
uint16_t read16(const std::vector<uint8_t> &bytes, size_t at) {
    return at + 2 <= bytes.size() ? uint16_t(bytes[at] | bytes[at + 1] << 8) : uint16_t(0);
}
// The disc path SYSTEM.CNF boots, e.g. "SLUS_207.79".
std::optional<std::string> boot_path(SourceDisc &disc) {
    std::vector<uint8_t> text;
    try {
        text = disc.read_file("SYSTEM.CNF");
    } catch (const std::runtime_error &) {
        // Without SYSTEM.CNF, a root file named like a serial is the executable.
        static const std::regex serial(R"([A-Z]{4}_[0-9]{3}\.[0-9]{2})");
        for (const auto &file : disc.files())
            if (std::regex_match(file.path, serial))
                return file.path;
        return std::nullopt;
    }
    static const std::regex boot(R"(BOOT2\s*=\s*cdrom0?:\\?([^;\r\n]+))", std::regex::icase);
    std::smatch match;
    const std::string content(text.begin(), text.end());
    if (!std::regex_search(content, match, boot))
        return std::nullopt;
    std::string path = match[1];
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    return path;
}
// "SLUS_207.79" -> "SLUS-20779"
std::string serial_of(const std::string &path) {
    auto name = upper(path.substr(path.find_last_of('/') + 1));
    name.erase(std::remove(name.begin(), name.end(), '.'), name.end());
    std::replace(name.begin(), name.end(), '_', '-');
    return name;
}
std::string printable(const ExecutableImage &image, uint32_t at, size_t limit = 31) {
    std::string text;
    for (size_t i = 0; i < limit && image.contains(at + uint32_t(i), 1); ++i) {
        const auto c = image.memory[at + i];
        if (!c)
            return text;
        if (c < 32 || c > 126)
            return {};
        text += char(c);
    }
    return {};
}
// Names the Budokai games use for their fighters, in their romanisations.
bool character_name(const std::string &name) {
    static const char *words[] = {"GOKOU", "GOKU", "VEGETA", "KULILIN", "KRILLIN", "PICCOLO", "FREEZA", "FRIEZA",
                                  "CELL", "GOHAN", "TRUNKS", "YAMCHA", "TENSHINHAN", "TIEN", "NAPPA", "RADITZ",
                                  "RECOOM", "GINEW", "GINYU", "ZARBON", "DODORIA", "SAIBAIMAN", "ANDROID", "BUU",
                                  "BROLY", "COOLER", "BARDOCK", "GOTEN", "GOTENKS", "VIDEL", "VIEDL", "DABURA",
                                  "SATAN", "HERCULE", "GERO", "KIBITO", "KAIO", "VEGITO", "GOGETA", "OMEGA", "JANEMBA",
                                  "PIKKON", "BABIDI", "UUB", "PAN"};
    const auto key = upper(name);
    return name.size() >= 3 && std::any_of(std::begin(words), std::end(words), [&](const char *word) {
               return key.rfind(word, 0) == 0;
           });
}
// Unused records keep names too, such as BUU (TEN + YAM); padding does not.
bool plausible_name(const std::string &name) {
    return name.size() >= 3 && std::isupper(static_cast<unsigned char>(name[0])) &&
           std::count_if(name.begin(), name.end(), [](unsigned char c) { return std::isalpha(c); }) * 2 >=
               int(name.size());
}
} // namespace

const char *game_name(DragonBallGame game) {
    switch (game) {
    case DragonBallGame::budokai: return "Dragon Ball Z: Budokai";
    case DragonBallGame::budokai2: return "Dragon Ball Z: Budokai 2";
    case DragonBallGame::budokai3: return "Dragon Ball Z: Budokai 3";
    case DragonBallGame::infinite_world: return "Dragon Ball Z: Infinite World";
    case DragonBallGame::other: return "a Dragon Ball game";
    default: return "not a Dragon Ball game";
    }
}

std::optional<ExecutableImage> load_executable(const std::vector<uint8_t> &elf) {
    if (elf.size() < 52 || std::memcmp(elf.data(), "\x7f" "ELF", 4) != 0)
        return std::nullopt;
    ExecutableImage image;
    for (uint32_t i = 0, header = read32(elf, 28); i < read16(elf, 44); ++i, header += read16(elf, 42)) {
        const auto type = read32(elf, header), offset = read32(elf, header + 4), address = read32(elf, header + 8);
        const auto bytes = read32(elf, header + 16);
        if (type != 1 || offset > elf.size() || bytes > elf.size() - offset || address > 0x2000000u ||
            bytes > 0x2000000u - address)
            continue;
        if (image.memory.size() < address + bytes)
            image.memory.resize(address + bytes, 0);
        std::memcpy(image.memory.data() + address, elf.data() + offset, bytes);
    }
    if (image.memory.empty())
        return std::nullopt;
    return image;
}

std::vector<CharacterTable> find_character_tables(const ExecutableImage &image) {
    // Every NUL-terminated character name, then names that sit at a common
    // spacing from each other.
    std::vector<uint32_t> names;
    const auto &memory = image.memory;
    for (uint32_t at = 1; at + 4 < memory.size(); ++at)
        if (!memory[at - 1] && std::isupper(memory[at]) && character_name(printable(image, at)))
            names.push_back(at);
    std::map<uint32_t, size_t> spacing;
    for (size_t i = 0; i < names.size(); ++i)
        for (size_t j = i + 1; j < names.size() && names[j] - names[i] <= 4096; ++j)
            if (names[j] - names[i] >= 32)
                ++spacing[names[j] - names[i]];
    std::vector<CharacterTable> tables;
    std::vector<bool> used(names.size(), false);
    // Multiples of a table's spacing line up too, so the smallest wins.
    for (const auto &[stride, count] : spacing) {
        if (count < 5)
            continue;
        for (size_t i = 0; i < names.size(); ++i) {
            if (used[i])
                continue;
            // Walk back and forward from a name while the slots hold names.
            uint32_t first = names[i];
            while (first >= stride + 1 && plausible_name(printable(image, first - stride)) &&
                   memory[first - stride - 1] == 0)
                first -= stride;
            CharacterTable table{first, stride, {}};
            size_t known = 0;
            for (uint32_t at = first; image.contains(at, 1); at += stride) {
                const auto name = printable(image, at);
                if (!plausible_name(name) || memory[at - 1] != 0)
                    break;
                known += character_name(name);
                table.names.push_back(name);
            }
            if (known < 6 || known * 2 < table.names.size())
                continue;
            for (size_t j = 0; j < names.size(); ++j)
                if (names[j] >= first && names[j] < first + stride * table.names.size() && (names[j] - first) % stride == 0)
                    used[j] = true;
            tables.push_back(std::move(table));
        }
    }
    return tables;
}

DiscIdentity identify_disc(SourceDisc &disc) {
    DiscIdentity identity;
    const auto boot = boot_path(disc);
    if (!boot)
        return identity;
    identity.executable = *boot;
    identity.serial = serial_of(*boot);
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file(*boot);
    } catch (const std::runtime_error &) {
        return identity;
    }
    const std::string text(elf.begin(), elf.end());
    // The memory card folder for this disc's own serial names the game.
    const auto digits = identity.serial.substr(identity.serial.find('-') + 1);
    const std::regex folder("B[A-Z][A-Z]{4}-?" + digits + "(DBZ[A-Z0-9]*)");
    std::smatch match;
    if (std::regex_search(text, match, folder)) {
        const std::string suffix = match[1];
        identity.game = suffix == "DBZ"    ? DragonBallGame::budokai
                        : suffix == "DBZ2" ? DragonBallGame::budokai2
                        : suffix == "DBZ3" ? DragonBallGame::budokai3
                                           : DragonBallGame::other;
    }
    const auto key = upper(text);
    if (key.find("INFINITE WORLD") != std::string::npos || key.find("INFINITEWORLD") != std::string::npos)
        identity.game = DragonBallGame::infinite_world;
    if (identity.game == DragonBallGame::none)
        if (const auto image = load_executable(elf); image && !find_character_tables(*image).empty())
            identity.game = DragonBallGame::other;
    return identity;
}

std::string disc_report(SourceDisc &disc) {
    std::ostringstream out;
    const auto identity = identify_disc(disc);
    out << "OpenKai2 disc report\n\nGame: " << game_name(identity.game) << "\nSerial: " << identity.serial
        << "\nExecutable: " << identity.executable << "\n\nFiles:\n";
    char line[160];
    for (const auto &file : disc.files()) {
        std::snprintf(line, sizeof line, "  %-32s %12llu bytes%s\n", file.path.c_str(),
                      static_cast<unsigned long long>(file.size),
                      file.archive ? (", " + std::to_string(file.entries) + " entries").c_str() : "");
        out << line;
    }
    for (const auto &file : disc.files()) {
        if (!file.archive)
            continue;
        out << "\nFirst bytes of " << file.path << " entries:\n";
        const auto &entries = disc.entries(file.path);
        for (size_t i = 0; i < entries.size() && i < 24; ++i) {
            std::snprintf(line, sizeof line, "  %4zu %10u  ", i, entries[i].size);
            out << line;
            if (entries[i].size >= 16) {
                const auto range = disc.range(file.path, i);
                std::ostringstream head;
                SourceDisc::Range first{range.offset, 16};
                disc.copy(first, head);
                for (const unsigned char c : head.str()) {
                    std::snprintf(line, sizeof line, "%02x", c);
                    out << line;
                }
            }
            out << "  " << entries[i].name << '\n';
        }
    }
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file(identity.executable);
    } catch (const std::runtime_error &) {
    }
    const auto image = load_executable(elf);
    const auto tables = image ? find_character_tables(*image) : std::vector<CharacterTable>{};
    out << "\nCharacter tables: " << tables.size() << '\n';
    for (const auto &table : tables) {
        std::snprintf(line, sizeof line, "\n  At 0x%08X, %u bytes apart, %zu names:\n", table.first, table.stride,
                      table.names.size());
        out << line;
        for (size_t i = 0; i < table.names.size(); ++i)
            out << "    " << i << "  " << table.names[i] << '\n';
        // Words that hold small numbers in most records may be file numbers.
        out << "  Fields that look like file numbers (offset from the name: values of the first records):\n";
        // Offsets wrap round into the record before the name, so the whole
        // record is covered whatever the name's place in it.
        for (uint32_t offset = 32; offset + 4 <= table.stride; offset += 4) {
            size_t small = 0;
            std::string values;
            for (size_t i = 0; i < table.names.size(); ++i) {
                const uint32_t value = read32(image->memory, table.first + i * table.stride + offset);
                small += value && value < 0x10000;
                if (i < 8)
                    values += " " + std::to_string(int32_t(value));
            }
            if (small * 2 >= table.names.size()) {
                std::snprintf(line, sizeof line, "    +0x%03X:", offset);
                out << line << values << '\n';
            }
        }
    }
    return out.str();
}

} // namespace openkai2::mods
