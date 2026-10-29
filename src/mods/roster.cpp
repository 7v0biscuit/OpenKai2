// SPDX-License-Identifier: GPL-3.0-only
#include "mods/roster.h"
#include "disc_image.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
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
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
// Decimal, 0x hex, or negative for signed fields.
long long signed_number(const std::string &text, const std::string &where) {
    size_t used = 0;
    long long value = 0;
    try {
        value = std::stoll(text, &used, 0);
    } catch (const std::exception &) {
        used = 0;
    }
    if (text.empty() || used != text.size() || value < -0x80000000ll || value > 0xffffffffll)
        throw std::runtime_error(where + ": " + text + " is not a number");
    return value;
}
uint32_t number(const std::string &text, const std::string &where) {
    const auto value = signed_number(text, where);
    if (value < 0)
        throw std::runtime_error(where + ": " + text + " cannot be negative");
    return uint32_t(value);
}
// A record offset written as hex, e.g. 74 or 0x74.
uint32_t offset(const std::string &text, const std::string &where) {
    const auto value = number(text.rfind("0x", 0) == 0 ? text : "0x" + text, where);
    if (value < roster_name_bytes || value >= roster_record_bytes)
        throw std::runtime_error(where + ": offset " + text + " is outside the record after its name");
    return value;
}
uint8_t *record(uint8_t *memory, uint32_t id) { return memory + roster_records + id * roster_record_bytes; }
std::string name_of(const uint8_t *record) {
    const auto *text = reinterpret_cast<const char *>(record);
    return std::string(text, strnlen(text, roster_name_bytes));
}
} // namespace

const DiscCharacter *DiscRoster::find(uint32_t id) const {
    for (const auto &character : characters)
        if (character.id == id)
            return &character;
    return nullptr;
}

std::string roster_display_name(const std::string &internal) {
    static const std::map<std::string, std::string> names{
        {"GOKOU", "Goku"}, {"GOHAN S", "Kid Gohan"}, {"GOHAN M", "Teen Gohan"}, {"GOHAN L", "Gohan"},
        {"VEGETA", "Vegeta"}, {"KULILIN", "Krillin"}, {"TRUNKS L", "Future Trunks"}, {"PICCOLO", "Piccolo"},
        {"TENSHINHAN", "Tien"}, {"YAMCHA", "Yamcha"}, {"RADITZ", "Raditz"}, {"NAPPA", "Nappa"},
        {"GINEW", "Captain Ginyu"}, {"RECOOM", "Recoome"}, {"ZARBON", "Zarbon"}, {"DODORIA", "Dodoria"},
        {"FREEZA", "Frieza"}, {"ANDROID No.16", "Android 16"}, {"ANDROID No.17", "Android 17"},
        {"ANDROID No.18", "Android 18"}, {"ANDROID No.19", "Android 19"}, {"CELL", "Cell"},
        {"Mr.SATAN", "Hercule"}, {"SAIBAIMAN", "Saibaman"}, {"CELL Jr.", "Cell Jr."},
        {"GREAT SAIYAMAN", "Great Saiyaman"}, {"Dr.GERO", "Dr. Gero"}, {"GOTEN", "Goten"},
        {"TRUNKS S", "Kid Trunks"}, {"BUU L", "Majin Buu"}, {"BUU M", "Super Buu"}, {"BUU S", "Kid Buu"},
        {"DABURA", "Dabura"}, {"VIEDL", "Videl"}, {"M VEGETA", "Majin Vegeta"}, {"KAIO-SHIN", "Supreme Kai"},
        {"GOTENKS", "Gotenks"}, {"YAMHAN", "Tiencha"}, {"VEGITO (GOKOU)", "Vegito (Goku)"},
        {"GOTAN (GOKOU)", "Gokule (Goku)"}, {"KIBITO-SHIN", "Kibito Kai"}, {"VEGITO (VEGETA)", "Vegito (Vegeta)"},
        {"GOTAN (SATAN)", "Gokule (Hercule)"}, {"KIBITO", "Kibito"}, {"BABIDI", "Babidi"}};
    const auto found = names.find(internal);
    return found == names.end() ? internal : found->second;
}

std::optional<DiscRoster> read_disc_roster(SourceDisc &disc) {
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file("SLUS_207.79");
    } catch (const std::runtime_error &) {
        return std::nullopt;
    }
    // Map the executable's loadable segments into a flat image of memory.
    const auto u16 = [&](size_t at) { return at + 2 <= elf.size() ? uint32_t(elf[at] | elf[at + 1] << 8) : 0u; };
    const auto u32 = [&](size_t at) { return at + 4 <= elf.size() ? uint32_t(u16(at) | u16(at + 2) << 16) : 0u; };
    if (elf.size() < 52 || std::memcmp(elf.data(), "\x7f" "ELF", 4) != 0)
        return std::nullopt;
    std::vector<uint8_t> memory(roster_select_grid + 64, 0);
    for (uint32_t i = 0, header = u32(28); i < u16(44); ++i, header += u16(42)) {
        const auto type = u32(header), offset = u32(header + 4), address = u32(header + 8), bytes = u32(header + 16);
        if (type != 1 || offset > elf.size() || bytes > elf.size() - offset || address >= memory.size())
            continue;
        const auto copied = std::min<size_t>(bytes, memory.size() - address);
        std::memcpy(memory.data() + address, elf.data() + offset, copied);
    }
    if (name_of(record(memory.data(), 0)) != "GOKOU" || name_of(record(memory.data(), roster_count - 1)) != "BUU (TEN + YAM)" ||
        memory[roster_select_grid + roster_select_cells] != 0x3f)
        return std::nullopt;
    const auto entries = disc.entries(roster_archive).size();
    DiscRoster roster;
    std::copy_n(memory.data() + roster_select_grid, roster_select_cells, roster.grid.begin());
    for (uint32_t id = 0; id < roster_count; ++id) {
        const auto *data = record(memory.data(), id);
        DiscCharacter character;
        character.id = id;
        character.internal = name_of(data);
        character.name = roster_display_name(character.internal);
        for (uint32_t at = roster_files_begin; at < roster_files_end; at += 4) {
            const uint32_t value = data[at] | data[at + 1] << 8 | data[at + 2] << 16 | uint32_t(data[at + 3]) << 24;
            if (value && value < entries)
                character.files.push_back({at, value});
        }
        if (!character.files.empty())
            roster.characters.push_back(std::move(character));
    }
    return roster;
}

std::vector<uint32_t> roster_slots_in_use(const std::vector<fs::path> &mods) {
    std::vector<uint32_t> slots;
    for (const auto &mod : mods) {
        std::ifstream in(mod / "roster.ini");
        for (std::string line; std::getline(in, line);) {
            const auto equals = line.find('=');
            if (equals == std::string::npos || lower(trim(line.substr(0, equals))) != "slot")
                continue;
            try {
                slots.push_back(uint32_t(std::stoul(trim(line.substr(equals + 1)), nullptr, 0)));
            } catch (const std::exception &) {
            }
        }
    }
    return slots;
}

std::optional<uint32_t> free_roster_slot(const std::vector<uint32_t> &taken) {
    for (uint32_t id = 0; id < roster_count; ++id)
        if (roster_slot_unused(id) && std::find(taken.begin(), taken.end(), id) == taken.end())
            return id;
    return std::nullopt;
}

void append_roster(const fs::path &mod, const RosterCharacter &character,
                   const std::vector<std::pair<uint32_t, std::string>> &files) {
    std::ofstream out(mod / "roster.ini", std::ios::app);
    out << "\n[character]\nslot = " << character.slot << '\n';
    if (character.base)
        out << "base = " << *character.base << '\n';
    if (character.name)
        out << "name = " << *character.name << '\n';
    for (const auto &[offset, value] : files) {
        char key[16];
        std::snprintf(key, sizeof key, "file.%x", offset);
        out << key << " = " << value << '\n';
    }
    if (character.select_cell)
        out << "select = " << *character.select_cell << '\n';
    for (const auto spot : character.alternate_cells)
        out << "alternate = " << spot << '\n';
    if (!out)
        throw std::runtime_error("Cannot write " + (mod / "roster.ini").string());
}

bool roster_slot_unused(uint32_t id) { return id == 1 || id == 13 || id == 14 || id == 19 || (id >= 43 && id < roster_count); }

std::vector<RosterCharacter> load_roster(const std::vector<fs::path> &mods, const DiscOverlay::Summary &overlay) {
    const auto key = IsoIndex::key(roster_archive).value_or(std::string());
    const auto added = overlay.added.find(key);
    std::vector<RosterCharacter> characters;
    for (const auto &mod : mods) {
        const auto file = mod / "roster.ini";
        std::ifstream in(file);
        if (!in)
            continue;
        std::optional<RosterCharacter> current;
        bool slotted = false;
        const auto finish = [&](const std::string &where) {
            if (!current)
                return;
            if (!slotted)
                throw std::runtime_error(where + ": the [character] section has no slot");
            characters.push_back(std::move(*current));
            current.reset();
        };
        size_t line_number = 0;
        for (std::string line; std::getline(in, line);) {
            ++line_number;
            const auto where = file.string() + ":" + std::to_string(line_number);
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;
            if (line == "[character]") {
                finish(where);
                current = RosterCharacter{};
                current->source = file;
                slotted = false;
                continue;
            }
            const auto equals = line.find('=');
            if (!current || equals == std::string::npos)
                throw std::runtime_error(where + ": expected [character] or key = value");
            const auto name = lower(trim(line.substr(0, equals)));
            auto value = trim(line.substr(equals + 1));
            const auto comment = value.find(" ;");
            if (comment != std::string::npos)
                value = trim(value.substr(0, comment));
            const auto entry = [&](const std::string &text) -> uint32_t {
                if (lower(text).rfind("add:", 0) != 0)
                    return number(text, where);
                const auto wanted = lower(trim(text.substr(4)));
                if (added != overlay.added.end())
                    if (const auto found = added->second.find(wanted); found != added->second.end())
                        return found->second;
                throw std::runtime_error(where + ": no enabled mod adds " + text.substr(4) + " to " + roster_archive);
            };
            if (name == "slot") {
                current->slot = number(value, where);
                if (current->slot >= roster_count)
                    throw std::runtime_error(where + ": slot must be 0 to " + std::to_string(roster_count - 1));
                slotted = true;
            } else if (name == "base") {
                current->base = number(value, where);
                if (*current->base >= roster_count)
                    throw std::runtime_error(where + ": base must be 0 to " + std::to_string(roster_count - 1));
            } else if (name == "name") {
                if (value.empty() || value.size() >= roster_name_bytes)
                    throw std::runtime_error(where + ": a name needs 1 to 31 characters");
                std::string upper = value;
                std::transform(upper.begin(), upper.end(), upper.begin(),
                               [](unsigned char c) { return char(std::toupper(c)); });
                current->name = upper;
            } else if (name == "alternate") {
                const auto spot = number(value, where);
                if (spot >= roster_select_cells)
                    throw std::runtime_error(where + ": alternate must be a cell from 0 to " +
                                             std::to_string(roster_select_cells - 1));
                current->alternate_cells.push_back(spot);
            } else if (name == "select") {
                current->select_cell = number(value, where);
                if (*current->select_cell >= roster_select_cells)
                    throw std::runtime_error(where + ": select must be a cell from 0 to " +
                                             std::to_string(roster_select_cells - 1));
            } else {
                // file.<offset>, u8.<offset>, u16.<offset>, u32.<offset> or f32.<offset>
                const auto dot = name.find('.');
                const auto kind = name.substr(0, dot);
                if (dot == std::string::npos ||
                    (kind != "file" && kind != "u8" && kind != "u16" && kind != "u32" && kind != "f32"))
                    throw std::runtime_error(where + ": unknown key " + name);
                RosterField field;
                field.offset = offset(name.substr(dot + 1), where);
                field.bytes = kind == "u8" ? 1 : kind == "u16" ? 2 : 4;
                if (field.offset % field.bytes || field.offset + field.bytes > roster_record_bytes)
                    throw std::runtime_error(where + ": " + name + " is not aligned to its size");
                if (kind == "f32") {
                    field.real = true;
                    float real = 0;
                    try {
                        size_t used = 0;
                        real = std::stof(value, &used);
                        if (used != value.size())
                            throw std::invalid_argument(value);
                    } catch (const std::exception &) {
                        throw std::runtime_error(where + ": " + value + " is not a number");
                    }
                    std::memcpy(&field.value, &real, 4);
                } else {
                    const long long wide = kind == "file" ? entry(value) : signed_number(value, where);
                    const long long limit = 1ll << (8 * field.bytes);
                    if (wide >= limit || wide < -limit / 2)
                        throw std::runtime_error(where + ": " + value + " does not fit in " + kind);
                    field.value = uint32_t(wide);
                }
                current->fields.push_back(field);
            }
        }
        finish(file.string());
    }
    return characters;
}

std::vector<std::string> apply_roster(uint8_t *memory, size_t bytes, const std::vector<RosterCharacter> &characters) {
    std::vector<std::string> log;
    if (characters.empty())
        return log;
    if (bytes < roster_select_grid + 64 || name_of(record(memory, 0)) != "GOKOU" ||
        name_of(record(memory, roster_count - 1)) != "BUU (TEN + YAM)" || memory[roster_select_grid] != 0 ||
        memory[roster_select_grid + roster_select_cells] != 0x3f)
        throw std::runtime_error("The loaded executable does not hold the Budokai 2 USA roster; roster mods were not applied");
    for (const auto &character : characters) {
        auto *target = record(memory, character.slot);
        if (character.base && *character.base != character.slot) {
            std::memmove(target, record(memory, *character.base), roster_record_bytes);
            // The select screen divides by the costume count, which is 0 for unused IDs.
            if (bytes > 0x37B310u + roster_count && !memory[0x37B310u + character.slot])
                memory[0x37B310u + character.slot] = memory[0x37B310u + *character.base];
        }
        if (character.name) {
            std::memset(target, 0, roster_name_bytes);
            std::memcpy(target, character.name->data(), character.name->size());
        }
        for (const auto &field : character.fields)
            for (unsigned i = 0; i < field.bytes; ++i)
                target[field.offset + i] = uint8_t(field.value >> (8 * i));
        std::string line = "roster: ID " + std::to_string(character.slot) + " is " + name_of(target);
        if (character.base)
            line += ", based on ID " + std::to_string(*character.base);
        if (!character.fields.empty())
            line += ", " + std::to_string(character.fields.size()) + " fields set";
        if (character.select_cell) {
            const auto previous = memory[roster_select_grid + *character.select_cell];
            memory[roster_select_grid + *character.select_cell] = uint8_t(character.slot);
            line += ", select cell " + std::to_string(*character.select_cell) + " (was ID " + std::to_string(previous) + ")";
        }
        for (const auto spot : character.alternate_cells)
            line += ", shares select cell " + std::to_string(spot);
        if (!roster_slot_unused(character.slot) && character.base)
            line += "; this replaces a character the game uses";
        log.push_back(line + " (" + character.source.parent_path().filename().string() + ")");
    }
    return log;
}

} // namespace openkai2::mods
