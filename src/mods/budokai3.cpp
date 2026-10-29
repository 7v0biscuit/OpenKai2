// SPDX-License-Identifier: GPL-3.0-only
#include "mods/budokai3.h"
#include "mods/disc_identity.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>

namespace openkai2::mods {
namespace {
uint32_t read32(const std::vector<uint8_t> &bytes, size_t at) {
    return at + 4 <= bytes.size() ? bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | uint32_t(bytes[at + 3]) << 24
                                  : 0u;
}
std::vector<uint8_t> read_bytes(SourceDisc &disc, const SourceDisc::Range &entry, uint64_t offset, size_t bytes) {
    if (offset >= entry.size)
        return {};
    std::ostringstream text;
    disc.copy(SourceDisc::Range{entry.offset + offset, std::min<uint64_t>(bytes, entry.size - offset)}, text);
    const auto data = text.str();
    return std::vector<uint8_t>(data.begin(), data.end());
}
bool starts(const std::vector<uint8_t> &bytes, const char *magic) {
    return bytes.size() >= 4 && std::memcmp(bytes.data(), magic, 4) == 0;
}
// Where the mesh and the textures of a costume model start: an #AMB of two
// files whose 16-byte records give their offsets.
std::optional<std::pair<uint32_t, uint32_t>> model_parts(SourceDisc &disc, const SourceDisc::Range &range) {
    const auto head = read_bytes(disc, range, 0, 32);
    if (head.size() < 32 || !starts(head, "#AMB") || read32(head, 16) != 2)
        return std::nullopt;
    const auto records = read_bytes(disc, range, read32(head, 20), 32);
    const uint32_t mesh = read32(records, 0), textures = read32(records, 16);
    if (records.size() < 32 || !starts(read_bytes(disc, range, mesh, 4), "#AMO") ||
        !starts(read_bytes(disc, range, textures, 4), "#AMT"))
        return std::nullopt;
    return std::pair{mesh, textures};
}
std::string title_case(const std::string &internal) {
    std::string name;
    bool start = true;
    for (const unsigned char c : internal) {
        name += char(start ? c : std::tolower(c));
        start = !std::isalpha(c);
    }
    return name;
}
std::string budokai3_name(std::string internal) {
    internal.erase(0, internal.find_first_not_of('@'));
    static const std::map<std::string, std::string> names{
        {"GOKOU SS4", "SSJ4 Goku"}, {"VEGETA SS4", "SSJ4 Vegeta"}, {"BROLY", "Broly"}, {"UUB", "Uub"},
        {"BULMA", "Bulma"}, {"GOGITA (GOKOU)", "Gogeta"}, {"GOGITA SS4 (GOKOU)", "SSJ4 Gogeta"},
        {"VEGITO (GOKOU)", "Vegito"}, {"GOTAN (GOKOU)", "Gokule"}, {"GOTENKS (GOTEN)", "Gotenks"},
        {"YAMHAN (TENSHINHAN)", "Tiencha"}, {"GOTENKS L", "Adult Gotenks"}, {"YAMHAN S", "Kid Tiencha"},
        {"GOGITA L", "Adult Gogeta"}, {"BUU (GOTENKS)", "Super Buu (Gotenks absorbed)"},
        {"BUU (GOHAN L)", "Super Buu (Gohan absorbed)"}, {"BUU (VEGETA)", "Buu (Vegeta absorbed)"},
        {"BUU (CELL)", "Buu (Cell absorbed)"}, {"BUU (FREEZA)", "Buu (Frieza absorbed)"},
        {"BUU (YAM + TEN)", "Buu (Tien and Yamcha absorbed)"}, {"BUU (PICCOLO)", "Buu (Piccolo absorbed)"}};
    if (const auto found = names.find(internal); found != names.end())
        return found->second;
    const auto known = roster_display_name(internal);
    return known != internal ? known : title_case(internal);
}
} // namespace

std::optional<ModelShape> model_shape(SourceDisc &disc, size_t entry) {
    const auto &entries = disc.entries(roster_archive);
    if (entry >= entries.size())
        return std::nullopt;
    const auto range = disc.range(roster_archive, entry);
    const auto parts = model_parts(disc, range);
    if (!parts)
        return std::nullopt;
    // The mesh header counts its parts at 16 and lists them from the offset
    // at 20, 32 bytes each: the part's number, its data, then the parts it
    // links to.
    const auto head = read_bytes(disc, range, parts->first, 32);
    const uint32_t count = read32(head, 16), table = read32(head, 20);
    if (!count || count > 256)
        return std::nullopt;
    const auto rows = read_bytes(disc, range, parts->first + table, size_t(count) * 32);
    if (rows.size() < size_t(count) * 32)
        return std::nullopt;
    ModelShape shape;
    for (uint32_t i = 0; i < count; ++i)
        shape.parts.push_back({read32(rows, i * 32), read32(rows, i * 32 + 8), read32(rows, i * 32 + 12),
                               read32(rows, i * 32 + 16)});
    return shape;
}

std::optional<DiscRoster> read_budokai3_models(SourceDisc &disc) {
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file("SLUS_209.98");
    } catch (const std::runtime_error &) {
        return std::nullopt;
    }
    const auto image = load_executable(elf);
    if (!image)
        return std::nullopt;
    const auto &memory = image->memory;
    const auto word = [&](uint32_t at) {
        return image->contains(at, 4) ? memory[at] | memory[at + 1] << 8 | memory[at + 2] << 16 |
                                            uint32_t(memory[at + 3]) << 24
                                      : 0u;
    };
    const auto name_at = [&](uint32_t at) {
        std::string name;
        for (uint32_t i = 0; i < budokai3_name_bytes && image->contains(at + i, 1) && memory[at + i]; ++i)
            name += char(memory[at + i]);
        return name;
    };
    if (word(budokai3_records) != budokai3_names || name_at(budokai3_names) != "GOKOU")
        return std::nullopt;
    const auto entries = disc.entries(roster_archive).size();
    DiscRoster roster;
    roster.models = true;
    for (uint32_t i = 0; i < budokai3_count; ++i) {
        const uint32_t record = budokai3_records + i * budokai3_record_bytes, name = budokai3_names + i * budokai3_name_bytes;
        // Forms that share another record's models have no list of their own.
        if (word(record) != name || !word(record + 8))
            continue;
        const auto internal = name_at(name);
        const uint32_t items = std::min<uint32_t>(uint32_t(memory[record + 4]) * memory[record + 5], 64);
        std::vector<uint32_t> models;
        std::vector<ModelShape> shapes;
        for (uint32_t k = 0; k < items; ++k) {
            const auto entry = word(word(record + 8) + k * 12);
            if (!entry || entry >= entries || std::find(models.begin(), models.end(), entry) != models.end())
                continue;
            if (auto shape = model_shape(disc, entry)) {
                models.push_back(entry);
                shapes.push_back(std::move(*shape));
            }
        }
        for (size_t k = 0; k < models.size(); ++k) {
            DiscCharacter character;
            character.id = i << 8 | uint32_t(k);
            character.internal = internal;
            character.name = budokai3_name(internal) +
                             (models.size() > 1 ? ", model " + std::to_string(k + 1) : std::string());
            character.files = {{0u, models[k]}};
            roster.shapes[character.id] = std::move(shapes[k]);
            roster.characters.push_back(std::move(character));
        }
    }
    if (roster.characters.empty())
        return std::nullopt;
    return roster;
}

std::vector<std::pair<uint32_t, uint32_t>> costume_fields(SourceDisc &game, const DiscCharacter &character) {
    // The costume models come first in the record, from 0x74.
    std::vector<std::pair<uint32_t, uint32_t>> fields;
    for (const auto &[offset, entry] : character.files)
        if (offset < 0x110u && model_shape(game, entry))
            fields.push_back({offset, entry});
    return fields;
}

std::string model_structure(SourceDisc &disc, size_t entry) {
    const auto range = disc.range(roster_archive, entry);
    const auto parts = model_parts(disc, range);
    if (!parts)
        return {};
    std::string text;
    char line[160];
    const auto words = [&](const std::vector<uint8_t> &bytes, size_t from, size_t count) {
        for (size_t at = from; at < from + count && at + 4 <= bytes.size(); at += 4) {
            std::snprintf(line, sizeof line, " %08X", read32(bytes, at));
            text += line;
        }
    };
    const auto head = read_bytes(disc, range, parts->first, 0x70);
    const uint32_t count = std::min<uint32_t>(read32(head, 16), 256), table = read32(head, 20);
    text += "    header:";
    words(head, 0, head.size());
    text += '\n';
    const auto rows = read_bytes(disc, range, parts->first + table, size_t(count) * 32);
    for (uint32_t i = 0; i < count && (i + 1) * 32 <= rows.size(); ++i) {
        std::snprintf(line, sizeof line, "    part %2u:", i);
        text += line;
        words(rows, i * 32, 32);
        text += "  data:";
        words(read_bytes(disc, range, parts->first + uint64_t(read32(rows, i * 32 + 4)), 64), 0, 64);
        text += '\n';
    }
    return text;
}

void read_model_shapes(SourceDisc &game, DiscRoster &roster) {
    for (const auto &character : roster.characters) {
        const auto costumes = costume_fields(game, character);
        if (!costumes.empty())
            if (auto shape = model_shape(game, costumes.front().second))
                roster.shapes[character.id] = std::move(*shape);
    }
}

std::vector<uint32_t> fitting_wearers(const DiscRoster &models, uint32_t model, const DiscRoster &wearers) {
    std::vector<uint32_t> fit;
    const auto shape = models.shapes.find(model);
    if (shape == models.shapes.end())
        return fit;
    for (const auto &character : wearers.characters)
        if (const auto theirs = wearers.shapes.find(character.id);
            theirs != wearers.shapes.end() && theirs->second == shape->second)
            fit.push_back(character.id);
    return fit;
}

std::vector<uint8_t> model_for_budokai2(SourceDisc &disc, size_t entry) {
    const auto range = disc.range(roster_archive, entry);
    const auto parts = model_parts(disc, range);
    if (!parts)
        throw std::runtime_error("Entry " + std::to_string(entry) + " is not a costume model");
    auto bytes = disc.read_entry(roster_archive, entry);
    const size_t version = size_t(parts->second) + 12;
    if (version + 4 <= bytes.size() && read32(bytes, version) == 2)
        bytes[version] = 1;
    return bytes;
}

} // namespace openkai2::mods
