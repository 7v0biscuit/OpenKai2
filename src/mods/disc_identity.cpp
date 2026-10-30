// SPDX-License-Identifier: GPL-3.0-only
#include "mods/disc_identity.h"
#include "mods/budokai3.h"
#include "mods/stages.h"
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
// Up to `bytes` bytes at `offset` into an archive entry, fewer past its end.
std::vector<uint8_t> read_bytes(SourceDisc &disc, const SourceDisc::Range &entry, uint64_t offset, size_t bytes) {
    if (offset >= entry.size)
        return {};
    std::ostringstream text;
    disc.copy(SourceDisc::Range{entry.offset + offset, std::min<uint64_t>(bytes, entry.size - offset)}, text);
    const auto data = text.str();
    return std::vector<uint8_t>(data.begin(), data.end());
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

namespace {
// NUL-terminated character names, by address.
std::vector<uint32_t> name_addresses(const ExecutableImage &image) {
    std::vector<uint32_t> names;
    const auto &memory = image.memory;
    for (uint32_t at = 1; at + 4 < memory.size(); ++at)
        if (!memory[at - 1] && std::isupper(memory[at]) && character_name(printable(image, at)))
            names.push_back(at);
    return names;
}
// Spacings that occur at least five times between nearby addresses, smallest
// first: multiples of a table's spacing line up too, so the smallest wins.
std::vector<uint32_t> common_spacings(const std::vector<uint32_t> &at, uint32_t least) {
    std::map<uint32_t, size_t> spacing;
    for (size_t i = 0; i < at.size(); ++i)
        for (size_t j = i + 1; j < at.size() && at[j] - at[i] <= 4096; ++j)
            if (at[j] - at[i] >= least)
                ++spacing[at[j] - at[i]];
    std::vector<uint32_t> strides;
    for (const auto &[stride, count] : spacing)
        if (count >= 5)
            strides.push_back(stride);
    return strides;
}
// Walks back and forward from `start` while `name_at` finds a name, and keeps
// the run when most of its names are characters.
template <typename NameAt>
std::optional<CharacterTable> walk_table(const ExecutableImage &image, uint32_t start, uint32_t stride, bool pointers,
                                         NameAt name_at) {
    uint32_t first = start;
    while (first >= stride && !name_at(first - stride).empty())
        first -= stride;
    CharacterTable table{first, stride, pointers, {}};
    size_t known = 0;
    for (uint32_t at = first; image.contains(at, 4); at += stride) {
        auto name = name_at(at);
        if (name.empty())
            break;
        known += character_name(name);
        table.names.push_back(std::move(name));
    }
    if (known < 6 || known * 2 < table.names.size())
        return std::nullopt;
    return table;
}
void mark_used(const std::vector<uint32_t> &at, std::vector<bool> &used, const CharacterTable &table) {
    for (size_t j = 0; j < at.size(); ++j)
        if (at[j] >= table.first && at[j] < table.first + table.stride * table.names.size() &&
            (at[j] - table.first) % table.stride == 0)
            used[j] = true;
}
} // namespace

std::vector<CharacterTable> find_character_tables(const ExecutableImage &image) {
    const auto &memory = image.memory;
    const auto names = name_addresses(image);
    std::vector<CharacterTable> tables;

    // Records that hold their names: names at a common spacing.
    const auto held = [&](uint32_t at) {
        auto name = printable(image, at);
        return at && memory[at - 1] == 0 && plausible_name(name) ? name : std::string{};
    };
    std::vector<bool> used(names.size(), false);
    for (const auto stride : common_spacings(names, 32))
        for (size_t i = 0; i < names.size(); ++i)
            if (!used[i])
                if (auto table = walk_table(image, names[i], stride, false, held)) {
                    mark_used(names, used, *table);
                    tables.push_back(std::move(*table));
                }

    // Records that point at their names: words holding a name's address, at a
    // common spacing.
    std::vector<uint32_t> pointers;
    for (uint32_t at = 0; at + 4 <= memory.size(); at += 4)
        if (std::binary_search(names.begin(), names.end(), read32(memory, at)))
            pointers.push_back(at);
    const auto pointed = [&](uint32_t at) {
        const auto target = read32(memory, at);
        return image.contains(target, 1) ? held(target) : std::string{};
    };
    std::vector<bool> followed(pointers.size(), false);
    for (const auto stride : common_spacings(pointers, 4))
        for (size_t i = 0; i < pointers.size(); ++i)
            if (!followed[i])
                if (auto table = walk_table(image, pointers[i], stride, true, pointed)) {
                    mark_used(pointers, followed, *table);
                    tables.push_back(std::move(*table));
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
    out << "OpenKai2 disc report, version 10\n\nGame: " << game_name(identity.game) << "\nSerial: " << identity.serial
        << "\nExecutable: " << identity.executable << "\n\nFiles:\n";
    char line[160];
    const auto hex = [&](const uint8_t *bytes, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            std::snprintf(line, sizeof line, "%02x", bytes[i]);
            out << line;
        }
    };
    for (const auto &file : disc.files()) {
        std::snprintf(line, sizeof line, "  %-32s %12llu bytes%s\n", file.path.c_str(),
                      static_cast<unsigned long long>(file.size),
                      file.archive ? (", " + std::to_string(file.entries) + " entries").c_str() : "");
        out << line;
    }
    // The first bytes of a few files of each format and version, to compare
    // the layouts of the two games' files.
    struct Sample {
        std::string where;
        std::vector<uint8_t> bytes;
    };
    std::map<std::pair<std::string, uint32_t>, std::vector<Sample>> samples;
    const auto sample = [&](const SourceDisc::Range &range, uint64_t offset, uint64_t size, std::string where) {
        const auto head = read_bytes(disc, range, offset, 16);
        if (offset == 0 && size == 0)
            return;
        if (head.size() < 16 || head[0] != '#')
            return;
        auto &kept = samples[{std::string(head.begin(), head.begin() + 4), read32(head, 12)}];
        if (kept.size() < 3)
            kept.push_back({std::move(where), read_bytes(disc, range, offset, size_t(std::min<uint64_t>(size, 512)))});
    };
    for (const auto &file : disc.files()) {
        if (!file.archive)
            continue;
        // Sound archives hold ADX streams, whose format is known; the rest are
        // listed in full so their files can be matched with Budokai 2's.
        const auto name = upper(file.path.substr(file.path.find_last_of('/') + 1));
        const auto &entries = disc.entries(file.path);
        const size_t listed = name.rfind("ADX", 0) == 0 ? std::min<size_t>(entries.size(), 24) : entries.size();
        out << "\nFirst bytes of " << file.path << " entries (" << listed << " of " << entries.size() << "):\n";
        for (size_t i = 0; i < listed; ++i) {
            std::snprintf(line, sizeof line, "  %4zu %10u  ", i, entries[i].size);
            out << line;
            const auto range = disc.range(file.path, i);
            const auto head = read_bytes(disc, range, 0, 32);
            hex(head.data(), head.size());
            out << "  " << entries[i].name << '\n';
            // An #AMB pack: the word at 16 counts its files and the word at
            // 20 is where their 16-byte records start. Each record and the
            // first bytes it leads to show the formats packed inside.
            if (head.size() < 32 || std::memcmp(head.data(), "#AMB", 4) != 0) {
                sample(range, 0, range.size, file.path + " entry " + std::to_string(i));
                continue;
            }
            const uint32_t count = std::min<uint32_t>(read32(head, 16), 64), table = read32(head, 20);
            const auto records = read_bytes(disc, range, table, size_t(count) * 16);
            for (uint32_t k = 0; k + 1 <= records.size() / 16; ++k) {
                std::snprintf(line, sizeof line, "        %2u  %08X %08X %08X %08X  ", k, read32(records, k * 16),
                              read32(records, k * 16 + 4), read32(records, k * 16 + 8), read32(records, k * 16 + 12));
                out << line;
                const auto inner = read_bytes(disc, range, read32(records, k * 16), 16);
                hex(inner.data(), inner.size());
                out << '\n';
                sample(range, read32(records, k * 16), read32(records, k * 16 + 4),
                       file.path + " entry " + std::to_string(i) + " file " + std::to_string(k));
            }
        }
    }
    out << "\nFormat samples: up to three files of each format and version, up to 512 bytes each:\n";
    for (const auto &[key, kept] : samples)
        for (const auto &one : kept) {
            std::snprintf(line, sizeof line, "\n  %s version %u, %s:\n", key.first.c_str(), key.second, one.where.c_str());
            out << line;
            for (size_t at = 0; at < one.bytes.size(); at += 16) {
                std::snprintf(line, sizeof line, "    +%03zX", at);
                out << line;
                for (size_t word = at; word + 4 <= one.bytes.size() && word < at + 16; word += 4) {
                    std::snprintf(line, sizeof line, " %08X", read32(one.bytes, word));
                    out << line;
                }
                out << '\n';
            }
        }
    // Characters' models whose parts link the same way share a check value,
    // and can wear each other's models.
    auto roster = read_disc_roster(disc);
    if (roster)
        read_model_shapes(disc, *roster);
    else
        roster = read_budokai3_models(disc);
    if (roster) {
        out << "\nCostume model shapes (entry, parts, a check value of how they link):\n";
        for (const auto &character : roster->characters) {
            const auto shape = roster->shapes.find(character.id);
            if (shape == roster->shapes.end())
                continue;
            uint32_t check = 2166136261u;
            for (const auto &part : shape->second.parts)
                for (const auto word : part)
                    for (unsigned b = 0; b < 4; ++b)
                        check = (check ^ ((word >> (8 * b)) & 0xFFu)) * 16777619u;
            const auto entry = roster->models ? character.files.front().second
                                              : costume_fields(disc, character).front().second;
            std::snprintf(line, sizeof line, "  %-34s %5u %4zu  %08X\n", character.name.c_str(), entry,
                          shape->second.parts.size(), check);
            out << line;
        }
        // The first character's models in full, one of each shape and at most
        // four, so the games' part layouts can be compared.
        out << "\nModel structure of the first character's models:\n";
        std::vector<ModelShape> shown;
        for (const auto &character : roster->characters) {
            if (shown.size() >= 4 || character.internal != roster->characters.front().internal)
                break;
            const auto fields = roster->models ? character.files : costume_fields(disc, character);
            for (const auto &[offset, entry] : fields) {
                const auto shape = model_shape(disc, entry);
                if (shown.size() >= 4 || !shape || std::find(shown.begin(), shown.end(), *shape) != shown.end())
                    continue;
                shown.push_back(*shape);
                out << "\n  " << character.name << ", entry " << entry << ":\n" << model_structure(disc, entry);
            }
        }
        // Krillin's model fits across the games; his textures and mesh blocks
        // show how the two games' models differ inside.
        out << "\nTextures and mesh blocks of Krillin's first models:\n";
        unsigned listed = 0;
        for (const auto &character : roster->characters) {
            if (character.internal != "KULILIN" || listed >= 2)
                continue;
            const auto fields = roster->models ? character.files : costume_fields(disc, character);
            for (const auto &[offset, entry] : fields)
                if (listed < 2) {
                    ++listed;
                    out << "\n  " << character.name << ", entry " << entry << ":\n" << model_contents(disc, entry);
                }
        }
        // Supreme Kai's Budokai 3 mesh is the size of Budokai 2's, and Krillin's
        // nearly so; check values show which pieces are the same.
        out << "\nCheck values of Krillin's and Supreme Kai's first models:\n";
        for (const char *name : {"KULILIN", "KAIO-SHIN"})
            for (const auto &character : roster->characters)
                if (character.internal == name) {
                    const auto fields = roster->models ? character.files : costume_fields(disc, character);
                    if (!fields.empty())
                        out << "\n  " << character.name << ", entry " << fields.front().second << ":\n"
                            << model_digest(disc, fields.front().second) << model_materials(disc, fields.front().second);
                    break;
                }
    }
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file(identity.executable);
    } catch (const std::runtime_error &) {
    }
    const auto image = load_executable(elf);
    if (!image)
        return out.str();
    const auto &memory = image->memory;

    const auto names = name_addresses(*image);
    out << "\nCharacter names in the executable: " << names.size() << '\n';
    std::vector<size_t> pointers(names.size(), 0);
    for (uint32_t word = 0; word + 4 <= memory.size(); word += 4)
        if (const auto at = std::lower_bound(names.begin(), names.end(), read32(memory, word));
            at != names.end() && *at == read32(memory, word))
            ++pointers[size_t(at - names.begin())];
    for (size_t i = 0; i < names.size(); ++i) {
        std::snprintf(line, sizeof line, "  0x%08X  %2zu pointers  ", names[i], pointers[i]);
        out << line << printable(*image, names[i]) << '\n';
    }

    const auto tables = find_character_tables(*image);
    out << "\nCharacter tables: " << tables.size() << '\n';
    for (const auto &table : tables) {
        std::snprintf(line, sizeof line, "\n  At 0x%08X, %u bytes apart, %zu names%s:\n", table.first, table.stride,
                      table.names.size(), table.pointers ? ", through pointers" : "");
        out << line;
        for (size_t i = 0; i < table.names.size(); ++i)
            out << "    " << i << "  " << table.names[i] << '\n';
        // Words that hold small numbers in most records may be file numbers.
        out << "  Fields that look like file numbers (offset from the "
            << (table.pointers ? "pointer" : "name") << ": values of the first records):\n";
        // Offsets wrap round into the record before the name, so the whole
        // record is covered whatever the name's place in it.
        for (uint32_t offset = table.pointers ? 4 : 32; offset + 4 <= table.stride; offset += 4) {
            size_t small = 0;
            std::string values;
            for (size_t i = 0; i < table.names.size(); ++i) {
                const uint32_t value = read32(memory, table.first + i * table.stride + offset);
                small += value && value < 0x10000;
                if (i < 8)
                    values += " " + std::to_string(int32_t(value));
            }
            if (small * 2 >= table.names.size()) {
                std::snprintf(line, sizeof line, "    +0x%03X:", offset);
                out << line << values << '\n';
            }
        }
        // The first records word by word, so fields the scan misses show too.
        if (table.stride >= 64) {
            out << "  The first records as words:\n";
            for (size_t i = 0; i < table.names.size() && i < 3; ++i) {
                out << "    " << table.names[i] << '\n';
                for (uint32_t offset = 0; offset < table.stride && offset < 1024; offset += 16) {
                    std::snprintf(line, sizeof line, "      +0x%03X", offset);
                    out << line;
                    for (uint32_t word = offset; word < offset + 16 && word < table.stride; word += 4) {
                        std::snprintf(line, sizeof line, " %08X", read32(memory, table.first + i * table.stride + word));
                        out << line;
                    }
                    out << '\n';
                }
            }
        }
    }

    // Runs of nearby names, then every string in them: the full lists,
    // names the character test does not know included.
    std::vector<std::pair<uint32_t, uint32_t>> lists;
    for (size_t i = 0, j; i < names.size(); i = j) {
        for (j = i + 1; j < names.size() && names[j] - names[j - 1] <= 0x1000; ++j) {
        }
        if (j - i >= 5)
            lists.emplace_back(names[i], std::min<uint32_t>(names[j - 1] + 0x40, uint32_t(memory.size())));
    }
    const auto in_lists = [&](uint32_t value) {
        return std::any_of(lists.begin(), lists.end(), [&](const auto &list) {
            return value >= list.first && value < list.second;
        });
    };
    out << "\nStrings among the names:\n";
    for (const auto &[first, end] : lists)
        for (uint32_t at = first; at < end; ++at)
            if (!memory[at - 1] && memory[at] >= 32 && memory[at] < 127)
                if (const auto text = printable(*image, at, 47); text.size() >= 2) {
                    std::snprintf(line, sizeof line, "  0x%08X  ", at);
                    out << line << text << '\n';
                }

    // Records of tables that point at names, through the records whose name
    // the walk above stopped at, and the data their other pointers lead to.
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    std::vector<uint32_t> targets;
    const auto words = [&](uint32_t at, uint32_t bytes) {
        std::string text;
        for (uint32_t word = 0; word < bytes; word += 4) {
            std::snprintf(line, sizeof line, " %08X", read32(memory, at + word));
            text += line;
        }
        return text;
    };
    const auto pointer = [&](uint32_t value) {
        return value >= 0x100000u && value + 96 <= memory.size() && value % 4 == 0 && !in_lists(value);
    };
    auto by_stride = tables;
    std::stable_sort(by_stride.begin(), by_stride.end(),
                     [](const auto &a, const auto &b) { return a.stride < b.stride; });
    for (const auto &table : by_stride) {
        if (!table.pointers || table.stride < 8 ||
            std::any_of(spans.begin(), spans.end(),
                        [&](const auto &span) { return table.first >= span.first && table.first < span.second; }))
            continue;
        const auto alike = [&](uint32_t at) {
            if (!image->contains(at, table.stride))
                return false;
            const auto name = read32(memory, at);
            bool used = false;
            for (uint32_t word = 4; word < table.stride; word += 4)
                used |= read32(memory, at + word) != 0;
            return used && (name == 0 || in_lists(name));
        };
        uint32_t first = table.first, end = table.first;
        for (unsigned n = 0; n < 256 && first >= table.stride && alike(first - table.stride); ++n)
            first -= table.stride;
        for (unsigned n = 0; n < 512 && alike(end); ++n)
            end += table.stride;
        spans.emplace_back(first, end);
        std::snprintf(line, sizeof line, "\nRecords from 0x%08X, %u bytes each:\n", first, table.stride);
        out << line;
        for (uint32_t at = first; at < end; at += table.stride) {
            const auto name = read32(memory, at);
            std::snprintf(line, sizeof line, "  0x%08X  %-18s", at, name ? printable(*image, name).c_str() : "-");
            out << line << words(at, std::min<uint32_t>(table.stride, 256)) << '\n';
            for (uint32_t word = 4; word < table.stride; word += 4)
                if (pointer(read32(memory, at + word)))
                    targets.push_back(read32(memory, at + word));
        }
    }
    // Three steps out, so lists that point at lists show too.
    std::vector<uint32_t> shown;
    for (int depth = 0; depth < 3 && !targets.empty(); ++depth) {
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        std::vector<uint32_t> next;
        if (depth == 0)
            out << "\nData the records point at, 96 bytes each:\n";
        for (const auto at : targets) {
            if (shown.size() >= 2000 || std::binary_search(shown.begin(), shown.end(), at))
                continue;
            shown.insert(std::upper_bound(shown.begin(), shown.end(), at), at);
            std::snprintf(line, sizeof line, "  0x%08X", at);
            out << line << words(at, 96) << '\n';
            for (uint32_t word = 0; word < 96; word += 4)
                if (pointer(read32(memory, at + word)))
                    next.push_back(read32(memory, at + word));
        }
        targets = std::move(next);
    }

    // Stages: Budokai 2's table is mapped; for Budokai 3, the records around
    // its stage names, word by word, to map them.
    if (const auto stages = read_disc_stages(disc)) {
        out << "\nStages (index, name, B stage, files as record offset = DATA_CMN entry):\n";
        for (const auto &stage : *stages) {
            std::snprintf(line, sizeof line, "  %2u  %-16s %s ", stage.index, stage.internal.c_str(),
                          stage.variant ? "B" : " ");
            out << line;
            for (const auto &[offset, entry] : stage.files) {
                std::snprintf(line, sizeof line, " %02X=%u", offset, entry);
                out << line;
            }
            out << '\n';
        }
    }
    if (identity.game == DragonBallGame::budokai3 && image->contains(budokai3_stage_names, budokai3_stage_bytes)) {
        const auto named = [&](uint32_t at) {
            const auto text = printable(*image, at);
            return image->contains(at, budokai3_stage_bytes) && (at == 0 || !memory[at - 1]) && text.size() >= 2 &&
                   std::isupper(static_cast<unsigned char>(text[0]));
        };
        uint32_t first = budokai3_stage_names;
        for (unsigned n = 0; n < 64 && first >= budokai3_stage_bytes && named(first - budokai3_stage_bytes); ++n)
            first -= budokai3_stage_bytes;
        out << "\nStage records from 0x";
        std::snprintf(line, sizeof line, "%08X, %u bytes each:\n", first, budokai3_stage_bytes);
        out << line;
        for (uint32_t at = first, n = 0; n < 96 && named(at); at += budokai3_stage_bytes, ++n) {
            std::snprintf(line, sizeof line, "  0x%08X  %-16s", at, printable(*image, at).c_str());
            out << line << words(at, budokai3_stage_bytes) << '\n';
        }
    }
    return out.str();
}

} // namespace openkai2::mods
