// SPDX-License-Identifier: GPL-3.0-only
#include "mods/budokai3.h"
#include "mods/disc_identity.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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
}

std::optional<ModelShape> model_shape(SourceDisc &disc, size_t entry) {
    const auto &entries = disc.entries(roster_archive);
    if (entry >= entries.size())
        return std::nullopt;
    const auto range = disc.range(roster_archive, entry);
    const auto parts = model_parts(disc, range);
    if (!parts)
        return std::nullopt;
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
    const auto pack = read_bytes(disc, range, parts->second, 32);
    const uint32_t textures = std::min<uint32_t>(read32(pack, 16), 64);
    const auto offsets = read_bytes(disc, range, parts->second + read32(pack, 20), size_t(textures) * 4);
    for (uint32_t i = 0; i < textures && (i + 1) * 4 <= offsets.size(); ++i) {
        const auto header = read_bytes(disc, range, parts->second + uint64_t(read32(offsets, i * 4)), 16);
        shape.textures.push_back({read32(header, 8), read32(header, 12)});
    }
    return shape;
}

std::vector<size_t> match_textures(const std::vector<std::array<uint32_t, 2>> &model,
                                   const std::vector<std::array<uint32_t, 2>> &wearer) {
    std::vector<size_t> picks;
    size_t next = 0;
    for (const auto &slot : wearer) {
        while (next < model.size() && model[next] != slot)
            ++next;
        if (next == model.size())
            return {};
        picks.push_back(next++);
    }
    return picks;
}

bool textures_fit(const ModelShape &model, const ModelShape &wearer) {
    return !wearer.textures.empty() && !match_textures(model.textures, wearer.textures).empty();
}

std::optional<uint32_t> budokai3_voice_bank(SourceDisc &disc, uint32_t character) {
    if (character >= budokai3_fighter_count)
        return std::nullopt;
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file("SLUS_209.98");
    } catch (const std::runtime_error &) {
        return std::nullopt;
    }
    const auto image = load_executable(elf);
    if (!image)
        return std::nullopt;
    const auto word = [&](uint32_t at) {
        const auto &memory = image->memory;
        return image->contains(at, 4) ? memory[at] | memory[at + 1] << 8 | memory[at + 2] << 16 |
                                            uint32_t(memory[at + 3]) << 24
                                      : 0u;
    };
    const uint32_t record = budokai3_fighters + character * budokai3_fighter_bytes;
    if (word(record + budokai3_fighter_mark) != budokai3_fighter_mark_value)
        return std::nullopt;
    const auto bank = word(record + budokai3_fighter_voice);
    if (!bank || bank >= disc.entries(budokai3_voice_archive).size())
        return std::nullopt;
    return bank;
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

std::string model_contents(SourceDisc &disc, size_t entry) {
    const auto range = disc.range(roster_archive, entry);
    const auto parts = model_parts(disc, range);
    if (!parts)
        return {};
    std::string text;
    char line[160];
    const auto words = [&](const std::vector<uint8_t> &bytes) {
        for (size_t at = 0; at + 4 <= bytes.size(); at += 4) {
            std::snprintf(line, sizeof line, " %08X", read32(bytes, at));
            text += line;
        }
    };
    const uint64_t pack = parts->second;
    const auto head = read_bytes(disc, range, pack, 32);
    text += "    textures:";
    words(head);
    text += '\n';
    const uint32_t count = std::min<uint32_t>(read32(head, 16), 64);
    const auto offsets = read_bytes(disc, range, pack + read32(head, 20), size_t(count) * 4);
    for (uint32_t i = 0; i < count && (i + 1) * 4 <= offsets.size(); ++i) {
        const auto texture = read_bytes(disc, range, pack + read32(offsets, i * 4), 48);
        std::snprintf(line, sizeof line, "    texture %2u:", i);
        text += line;
        words(texture);
        text += "  pixels:";
        words(read_bytes(disc, range, pack + read32(texture, 20), 32));
        text += "  palette:";
        words(read_bytes(disc, range, pack + read32(texture, 36), 64));
        text += '\n';
    }
    const auto mesh = read_bytes(disc, range, parts->first, 48);
    const uint32_t blocks = std::min<uint32_t>(read32(mesh, 32), 64);
    const auto list = read_bytes(disc, range, parts->first + 48, size_t(blocks) * 4);
    for (uint32_t i = 0; i < blocks && (i + 1) * 4 <= list.size(); ++i) {
        std::snprintf(line, sizeof line, "    block %2u at %08X:", i, read32(list, i * 4));
        text += line;
        words(read_bytes(disc, range, parts->first + read32(list, i * 4), 96));
        text += '\n';
    }
    return text;
}

std::string model_digest(SourceDisc &disc, size_t entry) {
    const auto range = disc.range(roster_archive, entry);
    const auto parts = model_parts(disc, range);
    if (!parts)
        return {};
    const auto bytes = disc.read_entry(roster_archive, entry);
    const auto check = [&](uint64_t from, uint64_t size) {
        uint32_t value = 2166136261u;
        for (uint64_t at = from; at < from + size && at < bytes.size(); ++at)
            value = (value ^ bytes[size_t(at)]) * 16777619u;
        return value;
    };
    std::string text;
    char line[160];
    const auto words = [&](uint64_t from, uint64_t size) {
        for (uint64_t at = from; at + 4 <= from + size && at + 4 <= bytes.size(); at += 4) {
            std::snprintf(line, sizeof line, " %08X", read32(bytes, size_t(at)));
            text += line;
        }
    };
    const uint32_t mesh = parts->first, pack = parts->second;
    const uint32_t meshSize = read32(bytes, read32(bytes, 20) + 4), packSize = read32(bytes, read32(bytes, 20) + 20);
    std::snprintf(line, sizeof line, "    mesh %u bytes, check %08X; textures %u bytes, check %08X\n", meshSize,
                  check(mesh, meshSize), packSize, check(pack, packSize));
    text += line;
    const uint32_t blocks = std::min<uint32_t>(read32(bytes, mesh + 32), 64);
    std::vector<uint32_t> starts;
    for (uint32_t i = 0; i < blocks; ++i)
        starts.push_back(read32(bytes, mesh + 48 + i * 4));
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t end = meshSize;
        for (const auto other : starts)
            if (other > starts[i] && other < end)
                end = other;
        std::snprintf(line, sizeof line, "    block %2u: %6u bytes, check %08X\n", i, end - starts[i],
                      check(mesh + uint64_t(starts[i]), end - starts[i]));
        text += line;
    }
    const uint32_t textures = std::min<uint32_t>(read32(bytes, pack + 16), 64), table = read32(bytes, pack + 20);
    for (uint32_t i = 0; i < textures; ++i) {
        const uint32_t header = pack + read32(bytes, pack + table + i * 4);
        std::snprintf(line, sizeof line, "    texture %2u: pixels %6u bytes, check %08X; palette %4u bytes, check %08X\n",
                      i, read32(bytes, header + 24), check(pack + uint64_t(read32(bytes, header + 20)), read32(bytes, header + 24)),
                      read32(bytes, header + 40), check(pack + uint64_t(read32(bytes, header + 36)), read32(bytes, header + 40)));
        text += line;
    }
    if (blocks > 1) {
        text += "    block 1 starts:";
        words(mesh + uint64_t(starts[1]), 512);
        text += '\n';
    }
    text += "    texture pack ends:";
    words(pack + uint64_t(packSize) - std::min<uint32_t>(packSize, 128), std::min<uint32_t>(packSize, 128));
    text += '\n';
    return text;
}

std::string mesh_differences(SourceDisc &budokai3, size_t entry, SourceDisc &game, size_t own) {
    const auto theirParts = model_parts(budokai3, budokai3.range(roster_archive, entry));
    const auto ourParts = model_parts(game, game.range(roster_archive, own));
    if (!theirParts || !ourParts)
        return "    not both costume models\n";
    const auto theirs = budokai3.read_entry(roster_archive, entry), ours = game.read_entry(roster_archive, own);
    const uint32_t theirMesh = theirParts->first, ourMesh = ourParts->first;
    const uint32_t size = std::min(theirParts->second - theirMesh, ourParts->second - ourMesh);
    const auto plain = [](uint32_t word) {
        const uint32_t exponent = word >> 23 & 0xFF;
        return word == 0 || word == 0x80000000u || (exponent >= 0x60 && exponent <= 0x9F);
    };
    std::vector<uint32_t> starts;
    for (uint32_t i = 0, blocks = std::min<uint32_t>(read32(ours, ourMesh + 32), 64); i < blocks; ++i)
        starts.push_back(read32(ours, ourMesh + 48 + i * 4));
    const auto block = [&](uint32_t at) {
        int found = -1;
        for (size_t i = 0; i < starts.size(); ++i)
            if (starts[i] <= at && (found < 0 || starts[i] > starts[size_t(found)]))
                found = int(i);
        return found;
    };
    size_t words = 0, floats = 0, codes = 0;
    std::map<std::pair<uint32_t, uint32_t>, size_t> pairs;
    std::string listed;
    char line[256];
    for (uint32_t at = 0; at + 4 <= size; at += 4) {
        const uint32_t ourWord = read32(ours, ourMesh + at), theirWord = read32(theirs, theirMesh + at);
        if (ourWord == theirWord)
            continue;
        ++words;
        if (plain(ourWord) && plain(theirWord)) {
            ++floats;
            continue;
        }
        ++codes;
        ++pairs[{ourWord, theirWord}];
        if (codes <= 200) {
            const uint32_t row = at & ~15u;
            std::snprintf(line, sizeof line, "    +%06X block %2d: %08X -> %08X   row B2", at, block(at), ourWord, theirWord);
            listed += line;
            for (uint32_t k = 0; k < 16 && row + k + 4 <= size; k += 4) {
                std::snprintf(line, sizeof line, " %08X", read32(ours, ourMesh + row + k));
                listed += line;
            }
            listed += "  B3";
            for (uint32_t k = 0; k < 16 && row + k + 4 <= size; k += 4) {
                std::snprintf(line, sizeof line, " %08X", read32(theirs, theirMesh + row + k));
                listed += line;
            }
            listed += '\n';
        }
    }
    std::snprintf(line, sizeof line,
                  "    %u bytes compared: %zu words differ, %zu of them both vertex-like floats, %zu codes or flags\n",
                  size, words, floats, codes);
    std::string text = line;
    std::vector<std::pair<size_t, std::pair<uint32_t, uint32_t>>> common;
    for (const auto &[pair, count] : pairs)
        common.push_back({count, pair});
    std::sort(common.rbegin(), common.rend());
    text += "    most common changes (Budokai 2 -> Budokai 3, times):\n";
    for (size_t i = 0; i < common.size() && i < 40; ++i) {
        std::snprintf(line, sizeof line, "      %08X -> %08X  %zu\n", common[i].second.first, common[i].second.second,
                      common[i].first);
        text += line;
    }
    text += "    first differing codes or flags (offset in the mesh):\n" + listed;
    return text;
}

std::string model_materials(SourceDisc &disc, size_t entry) {
    if (!model_parts(disc, disc.range(roster_archive, entry)))
        return {};
    const auto bytes = disc.read_entry(roster_archive, entry);
    const uint32_t mesh = read32(bytes, read32(bytes, 20));
    std::string text;
    char line[160];
    for (const auto record : material_records(bytes, true)) {
        std::snprintf(line, sizeof line, "    material at mesh +%06zX: %08X %08X %08X %08X\n", record - mesh,
                      read32(bytes, record), read32(bytes, record + 4), read32(bytes, record + 8),
                      read32(bytes, record + 12));
        text += line;
    }
    const auto draws = draw_records(bytes);
    size_t flagged = 0;
    for (const auto record : draws)
        flagged += (read32(bytes, record) & budokai2_draw_flag) != 0;
    std::snprintf(line, sizeof line, "    draw records: %zu, %zu with Budokai 2's flag\n", draws.size(), flagged);
    text += line;
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
            theirs != wearers.shapes.end() && textures_fit(shape->second, theirs->second))
            fit.push_back(character.id);
    return fit;
}

std::vector<size_t> material_records(const std::vector<uint8_t> &model, bool any) {
    std::vector<size_t> records;
    const uint32_t mesh = read32(model, read32(model, 20)), size = read32(model, read32(model, 20) + 4);
    if (mesh > model.size() || size > model.size() - mesh)
        return records;
    const uint32_t blocks = std::min<uint32_t>(read32(model, mesh + 32), 64);
    std::vector<uint32_t> starts;
    for (uint32_t i = 0; i < blocks; ++i)
        starts.push_back(read32(model, mesh + 48 + i * 4));
    for (const auto start : starts) {
        uint32_t end = size;
        for (const auto other : starts)
            if (other > start && other < end)
                end = other;
        for (uint32_t at = (start + 15) / 16 * 16; at + 16 <= end; at += 16) {
            const size_t record = size_t(mesh) + at;
            if (read32(model, record + 4) == 0x29BDu &&
                (any || (read32(model, record) & ~budokai2_material_flag) == 0x1B5u))
                records.push_back(record);
        }
    }
    return records;
}

std::vector<size_t> draw_records(const std::vector<uint8_t> &model) {
    std::vector<size_t> records;
    const uint32_t mesh = read32(model, read32(model, 20)), size = read32(model, read32(model, 20) + 4);
    if (mesh > model.size() || size > model.size() - mesh || read32(model, mesh + 32) == 0)
        return records;
    const uint32_t start = read32(model, mesh + 48);
    uint32_t end = size;
    for (uint32_t i = 1, blocks = std::min<uint32_t>(read32(model, mesh + 32), 64); i < blocks; ++i)
        if (const uint32_t other = read32(model, mesh + 48 + i * 4); other > start && other < end)
            end = other;
    for (uint32_t at = (start + 15) / 16 * 16; at + 16 <= end; at += 16) {
        const size_t record = size_t(mesh) + at;
        const uint32_t word = read32(model, record), target = read32(model, record + 4);
        const uint32_t kind = word >> 28;
        if ((word & 0x0FFFFFC0u) == 0x200u && (kind == 0 || kind == 6 || kind == 8 || kind == 9) && target &&
            target % 16 == 0 && target < size)
            records.push_back(record);
    }
    return records;
}

namespace {
void write32(std::vector<uint8_t> &bytes, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = uint8_t(value >> (8 * i));
}
uint32_t brightness(uint32_t color) {
    return (77u * (color & 0xFFu) + 150u * (color >> 8 & 0xFFu) + 29u * (color >> 16 & 0xFFu)) >> 8;
}
}

void bake_budokai3_shading(std::vector<uint8_t> &model) {
    const uint32_t records = read32(model, 20), pack = read32(model, records + 16), packSize = read32(model, records + 20);
    if (pack > model.size() || packSize > model.size() - pack || packSize < 32)
        return;
    const uint32_t count = std::min<uint32_t>(read32(model, pack + 16), 64), table = read32(model, pack + 20);
    struct Block {
        size_t at = 0, size = 0;
    };
    const auto block = [&](size_t header, size_t field) {
        const size_t at = size_t(pack) + read32(model, header + field), size = read32(model, header + field + 4);
        const bool sent = size % 16 == 0 && at + 32 <= model.size() &&
                          read32(model, at + 12) == (0x50000000u | uint32_t(size / 16 + 1));
        const Block found{sent ? at + 32 : at, size};
        return found.at + found.size <= size_t(pack) + packSize ? found : Block{};
    };
    const auto header = [&](uint32_t texture) -> size_t {
        if (texture >= count || table + (texture + 1) * 4 > packSize)
            return 0;
        const size_t at = size_t(pack) + read32(model, pack + table + texture * 4);
        return at + 48 <= size_t(pack) + packSize ? at : 0;
    };
    std::map<uint32_t, std::map<uint32_t, size_t>> uses;
    for (const auto record : material_records(model, true))
        ++uses[read32(model, record + 8)][read32(model, record + 12)];
    std::map<uint32_t, uint32_t> lit;
    for (const auto &[texture, ramps] : uses)
        for (const auto &[ramp, times] : ramps) {
            const size_t at = header(ramp);
            if (lit.count(ramp) || !at || !(read32(model, at + 4) & 0x80000000u) || read32(model, at + 8) != 0x13u)
                continue;
            const auto pixels = block(at, 20), palette = block(at, 36);
            if (!pixels.size || palette.size != 1024)
                continue;
            std::array<size_t, 256> seen{};
            for (size_t k = 0; k < pixels.size; ++k)
                ++seen[model[pixels.at + k]];
            const auto colour = [&](unsigned index) {
                return read32(model, palette.at + ((index & ~24u) | ((index & 8u) << 1) | ((index & 16u) >> 1)) * 4);
            };
            bool opaque = true;
            for (unsigned k = 0; k < 256; ++k)
                opaque &= !seen[k] || colour(k) >> 24 >= 0x80u;
            if (opaque)
                lit[ramp] = colour(unsigned(std::max_element(seen.begin(), seen.end()) - seen.begin()));
        }
    for (const auto &[texture, ramps] : uses) {
        uint32_t ramp = 0;
        size_t most = 0;
        for (const auto &[candidate, times] : ramps)
            if (lit.count(candidate) && times > most) {
                ramp = candidate;
                most = times;
            }
        const size_t at = header(texture);
        if (!most || !at || lit.count(texture))
            continue;
        const auto palette = block(at, 36);
        const uint32_t tint = lit[ramp];
        for (size_t k = 0; k + 4 <= palette.size; k += 4) {
            const uint32_t color = read32(model, palette.at + k);
            if (color >> 24 >= 0x80u)
                continue;
            uint32_t baked = color & 0xFF000000u;
            for (unsigned shift = 0; shift < 24; shift += 8)
                baked |= uint32_t(std::max(0, int(color >> shift & 0xFFu) - int(tint >> shift & 0xFFu))) << shift;
            write32(model, palette.at + k, baked);
        }
    }
    for (const auto &[ramp, tint] : lit) {
        const auto palette = block(header(ramp), 36);
        const int base = int(brightness(tint));
        for (size_t k = 0; k + 4 <= palette.size; k += 4) {
            const int shade = int(brightness(read32(model, palette.at + k))) - base;
            write32(model, palette.at + k,
                    shade >= 0 ? uint32_t(std::min(shade * 128 / 160, 0x40)) << 24
                               : uint32_t(std::min(-shade * 128 / 255, 0x40)) << 24 | 0x00FFFFFFu);
        }
    }
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
    for (const auto record : material_records(bytes, false))
        bytes[record + 1] |= uint8_t(budokai2_material_flag >> 8);
    bake_budokai3_shading(bytes);
    return bytes;
}

namespace {
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> split_model(const std::vector<uint8_t> &model) {
    const uint32_t records = read32(model, 20);
    const auto part = [&](uint32_t record) {
        const uint32_t offset = read32(model, records + record), size = read32(model, records + record + 4);
        if (offset > model.size() || size > model.size() - offset)
            throw std::runtime_error("A costume model's files run past its end");
        return std::vector<uint8_t>(model.begin() + offset, model.begin() + offset + size);
    };
    return {part(0), part(16)};
}
void put32(std::vector<uint8_t> &bytes, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = uint8_t(value >> (8 * i));
}
std::vector<uint8_t> join_model(const std::vector<uint8_t> &header, const std::vector<uint8_t> &mesh,
                                const std::vector<uint8_t> &textures) {
    const auto align = [](size_t at) { return (at + 63) / 64 * 64; };
    const size_t meshAt = 0x40, texturesAt = align(meshAt + mesh.size());
    std::vector<uint8_t> model(texturesAt + textures.size(), 0);
    std::copy(header.begin(), header.begin() + 0x20, model.begin());
    put32(model, 16, 2);
    put32(model, 20, 0x20);
    const uint32_t records[2][3] = {{uint32_t(meshAt), uint32_t(mesh.size()), 1},
                                    {uint32_t(texturesAt), uint32_t(textures.size()), 2}};
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned k = 0; k < 3; ++k)
            put32(model, 0x20 + i * 16 + k * 4, records[i][k]);
    std::copy(mesh.begin(), mesh.end(), model.begin() + meshAt);
    std::copy(textures.begin(), textures.end(), model.begin() + texturesAt);
    return model;
}
uint32_t transfer_bytes(const std::vector<uint8_t> &pack, uint32_t at, uint32_t size) {
    const bool sent = size % 16 == 0 && size_t(at) + 32 <= pack.size() &&
                      read32(pack, at + 12) == (0x50000000u | (size / 16 + 1));
    return sent && size_t(at) + size + 32 <= pack.size() ? size + 32 : size;
}
struct PackTexture {
    std::vector<uint8_t> header, pixels, palette;
};
std::vector<PackTexture> pack_textures(const std::vector<uint8_t> &pack) {
    const auto slice = [&](uint32_t at, uint32_t size) {
        if (at > pack.size() || size > pack.size() - at)
            throw std::runtime_error("A texture runs past the end of its pack");
        return std::vector<uint8_t>(pack.begin() + at, pack.begin() + at + size);
    };
    std::vector<PackTexture> textures;
    const uint32_t count = std::min<uint32_t>(read32(pack, 16), 64), table = read32(pack, 20);
    for (uint32_t i = 0; i < count; ++i) {
        PackTexture texture;
        texture.header = slice(read32(pack, table + i * 4), 48);
        texture.pixels = slice(read32(texture.header, 20), transfer_bytes(pack, read32(texture.header, 20),
                                                                          read32(texture.header, 24)));
        texture.palette = slice(read32(texture.header, 36), transfer_bytes(pack, read32(texture.header, 36),
                                                                           read32(texture.header, 40)));
        textures.push_back(std::move(texture));
    }
    return textures;
}
bool lighting_texture(const PackTexture &texture) {
    if (read32(texture.header, 4) & 0x80000000u)
        return true;
    const auto &palette = texture.palette;
    const size_t first = palette.size() >= 36 && read32(palette, 12) >> 24 == 0x50 ? 32 : 0;
    if (palette.size() < first + 4)
        return false;
    for (size_t at = first; at + 4 <= palette.size(); at += 4)
        if (read32(palette, at) & 0x00FFFFFFu)
            return false;
    return true;
}
std::vector<std::array<uint32_t, 2>> layout_of(const std::vector<PackTexture> &textures) {
    std::vector<std::array<uint32_t, 2>> layout;
    for (const auto &texture : textures)
        layout.push_back({read32(texture.header, 8), read32(texture.header, 12)});
    return layout;
}
}

namespace {
std::vector<uint8_t> build_pack(const std::vector<uint8_t> &like, const std::vector<PackTexture> &textures) {
    const auto align = [](size_t at) { return (at + 15) / 16 * 16; };
    const uint32_t count = uint32_t(textures.size()), table = 0x20, headers = uint32_t(align(table + count * 4));
    std::vector<uint8_t> pack(align(headers + count * 48), 0);
    std::copy(like.begin(), like.begin() + 0x20, pack.begin());
    put32(pack, 12, 1);
    put32(pack, 16, count);
    put32(pack, 20, table);
    for (uint32_t i = 0; i < count; ++i) {
        auto header = textures[i].header;
        put32(header, 0, i);
        const auto place = [&](const std::vector<uint8_t> &bytes) {
            const auto at = uint32_t(pack.size());
            pack.insert(pack.end(), bytes.begin(), bytes.end());
            pack.resize(align(pack.size()), 0);
            return at;
        };
        put32(header, 20, place(textures[i].pixels));
        put32(header, 36, place(textures[i].palette));
        put32(pack, table + i * 4, headers + i * 48);
        std::copy(header.begin(), header.end(), pack.begin() + headers + i * 48);
    }
    return pack;
}
}

std::vector<uint8_t> match_pack(const std::vector<uint8_t> &theirs, const std::vector<uint8_t> &ours,
                                bool ourLighting) {
    const auto source = pack_textures(theirs), wanted = pack_textures(ours);
    const auto picks = match_textures(layout_of(source), layout_of(wanted));
    if (picks.empty())
        throw std::runtime_error("The textures do not match the body's textures");
    std::vector<PackTexture> textures;
    for (size_t i = 0; i < picks.size(); ++i)
        textures.push_back(ourLighting && lighting_texture(wanted[i]) ? wanted[i] : source[picks[i]]);
    return build_pack(theirs, textures);
}

std::vector<uint8_t> with_budokai2_lighting(const std::vector<uint8_t> &theirs, const std::vector<uint8_t> &ours) {
    auto textures = pack_textures(theirs);
    const auto own = pack_textures(ours);
    std::map<std::array<uint32_t, 2>, size_t> seen;
    for (auto &texture : textures) {
        if (!lighting_texture(texture))
            continue;
        const auto shape = layout_of({texture}).front();
        std::vector<const PackTexture *> candidates;
        for (const auto &mine : own)
            if (lighting_texture(mine) && layout_of({mine}).front() == shape)
                candidates.push_back(&mine);
        if (candidates.empty())
            continue;
        const auto *pick = candidates[std::min(seen[shape]++, candidates.size() - 1)];
        texture.pixels = pick->pixels;
        texture.palette = pick->palette;
    }
    return build_pack(theirs, textures);
}

std::vector<uint8_t> model_version(SourceDisc &budokai3, size_t entry, SourceDisc &game, size_t own,
                                   ModelVersion version) {
    if (version == ModelVersion::converted)
        return model_for_budokai2(budokai3, entry);
    if (version == ModelVersion::unchanged) {
        if (!model_parts(budokai3, budokai3.range(roster_archive, entry)))
            throw std::runtime_error("Entry " + std::to_string(entry) + " is not a costume model");
        return budokai3.read_entry(roster_archive, entry);
    }
    const auto theirs = model_for_budokai2(budokai3, entry);
    if (!model_parts(game, game.range(roster_archive, own)))
        throw std::runtime_error("Entry " + std::to_string(own) + " of the game disc is not a costume model");
    const auto ours = game.read_entry(roster_archive, own);
    const auto [theirMesh, theirTextures] = split_model(theirs);
    const auto [ourMesh, ourTextures] = split_model(ours);
    switch (version) {
    case ModelVersion::budokai2_textures:
        return join_model(theirs, theirMesh, ourTextures);
    case ModelVersion::budokai2_lighting:
        return join_model(ours, ourMesh, match_pack(theirTextures, ourTextures, true));
    case ModelVersion::converted_budokai2_lighting:
        return join_model(theirs, theirMesh, with_budokai2_lighting(theirTextures, ourTextures));
    default:
        return join_model(ours, ourMesh, match_pack(theirTextures, ourTextures));
    }
}


namespace {
std::vector<uint8_t> amb_of(const std::vector<std::vector<uint8_t>> &files, const std::vector<uint32_t> &kinds) {
    std::vector<uint8_t> out(0x20 + 16 * files.size(), 0);
    std::memcpy(out.data(), "#AMB", 4);
    write32(out, 4, 0x20);
    write32(out, 12, 3);
    write32(out, 16, uint32_t(files.size()));
    write32(out, 20, 0x20);
    for (size_t i = 0; i < files.size(); ++i) {
        out.resize((out.size() + 63) / 64 * 64, 0);
        if (i == 0)
            write32(out, 24, uint32_t(out.size()));
        write32(out, 0x20 + i * 16, uint32_t(out.size()));
        write32(out, 0x24 + i * 16, uint32_t(files[i].size()));
        write32(out, 0x28 + i * 16, kinds[i]);
        out.insert(out.end(), files[i].begin(), files[i].end());
    }
    out.resize((out.size() + 15) / 16 * 16, 0);
    return out;
}
std::vector<uint8_t> amb_part(const std::vector<uint8_t> &pack, uint32_t index) {
    const uint32_t records = read32(pack, 20);
    if (pack.size() < 32 || std::memcmp(pack.data(), "#AMB", 4) != 0 || index >= read32(pack, 16))
        return {};
    const uint32_t at = read32(pack, records + index * 16), size = read32(pack, records + index * 16 + 4);
    if (!size || at > pack.size() || size > pack.size() - at)
        return {};
    return std::vector<uint8_t>(pack.begin() + at, pack.begin() + at + size);
}
std::vector<std::string> bone_keys(const std::vector<uint8_t> &motions) {
    std::vector<std::string> keys;
    const uint32_t table = read32(motions, 28), count = read32(motions, 24);
    for (uint32_t i = 0; i < count && size_t(table) + (i + 1) * 32 <= motions.size(); ++i) {
        std::string name(reinterpret_cast<const char *>(motions.data() + table + i * 32), 32);
        name = name.substr(0, name.find('\0'));
        if (!name.empty() && name[0] == 'X')
            name.erase(0, 1);
        if (const auto cut = name.find('_'); cut != std::string::npos)
            name.erase(0, cut + 1);
        keys.push_back(name);
    }
    return keys;
}
}

std::vector<uint8_t> merge_motions(const std::vector<std::vector<uint8_t>> &packs) {
    if (packs.empty() || packs.front().size() < 32)
        throw std::runtime_error("No motions to merge");
    const auto &skeleton = packs.front();
    const auto target = bone_keys(skeleton);
    const uint32_t bones = uint32_t(target.size());
    struct Motion {
        std::array<uint8_t, 16> entry{};
        std::vector<uint8_t> data;
        uint32_t source = 0, own = 0;
        std::vector<int> mapping;
    };
    std::vector<Motion> motions;
    for (const auto &pack : packs) {
        const auto keys = bone_keys(pack);
        std::vector<int> mapping;
        for (const auto &key : target) {
            const auto found = std::find(keys.begin(), keys.end(), key);
            mapping.push_back(found == keys.end() ? -1 : int(found - keys.begin()));
        }
        const uint32_t count = read32(pack, 16), table = read32(pack, 20), end = read32(pack, 28);
        if (end > pack.size() || size_t(table) + size_t(count) * 16 > pack.size())
            throw std::runtime_error("A motion pack runs past its end");
        std::vector<uint32_t> starts;
        for (uint32_t i = 0; i < count; ++i)
            starts.push_back(read32(pack, table + i * 16 + 12));
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t stop = end;
            for (const auto other : starts)
                if (other > starts[i] && other < stop)
                    stop = other;
            if (starts[i] > stop || stop > pack.size())
                throw std::runtime_error("A motion runs past its pack");
            Motion motion;
            std::copy(pack.begin() + table + i * 16, pack.begin() + table + i * 16 + 16, motion.entry.begin());
            motion.data.assign(pack.begin() + starts[i], pack.begin() + stop);
            motion.source = starts[i];
            motion.own = read32(pack, 24);
            motion.mapping = mapping;
            motions.push_back(std::move(motion));
        }
    }
    std::vector<uint8_t> out(0x20 + motions.size() * 16, 0);
    std::memcpy(out.data(), "#AMM", 4);
    write32(out, 4, 0x20);
    write32(out, 12, 2);
    write32(out, 16, uint32_t(motions.size()));
    write32(out, 20, 0x20);
    write32(out, 24, bones);
    for (size_t i = 0; i < motions.size(); ++i) {
        auto &motion = motions[i];
        out.resize((out.size() + 15) / 16 * 16, 0);
        const uint32_t placed = uint32_t(out.size()), ownBytes = motion.own * 8, header = bones * 8;
        if (motion.data.size() < ownBytes)
            throw std::runtime_error("A motion is shorter than its bone table");
        std::vector<uint8_t> head(header, 0);
        for (uint32_t bone = 0; bone < bones; ++bone) {
            if (motion.mapping[bone] < 0)
                continue;
            for (uint32_t half = 0; half < 8; half += 4) {
                const uint32_t pointer = read32(motion.data, size_t(motion.mapping[bone]) * 8 + half);
                if (pointer)
                    write32(head, bone * 8 + half, placed + header + (pointer - motion.source - ownBytes));
            }
        }
        std::copy(motion.entry.begin(), motion.entry.begin() + 12, out.begin() + 0x20 + i * 16);
        write32(out, 0x2C + i * 16, placed);
        out.insert(out.end(), head.begin(), head.end());
        out.insert(out.end(), motion.data.begin() + ownBytes, motion.data.end());
    }
    out.resize((out.size() + 15) / 16 * 16, 0);
    write32(out, 28, uint32_t(out.size()));
    out.insert(out.end(), skeleton.begin() + read32(skeleton, 28), skeleton.end());
    return out;
}

namespace {
struct ScriptEntry {
    std::vector<uint8_t> record;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> commands;
};

std::map<uint32_t, ScriptEntry> script_entries(const std::vector<uint8_t> &b) {
    if (b.size() < 32 || std::memcmp(b.data(), "#BSK", 4) != 0)
        throw std::runtime_error("A move script is not a #BSK");
    std::map<uint32_t, ScriptEntry> entries;
    const uint32_t count = read32(b, 16);
    for (uint32_t i = 0; i < count && 0x20 + (i + 1) * 4 <= b.size(); ++i) {
        const uint32_t at = read32(b, 0x20 + i * 4);
        if (!at || size_t(at) + 48 > b.size())
            continue;
        ScriptEntry entry;
        entry.record.assign(b.begin() + at, b.begin() + at + 48);
        const uint32_t commands = read32(entry.record, 40), list = read32(entry.record, 44);
        for (uint32_t k = 0; k < commands && size_t(list) + (k + 1) * 8 <= b.size(); ++k) {
            const uint32_t head = read32(b, list + k * 8), data = read32(b, list + k * 8 + 4);
            const size_t bytes = size_t(head >> 16) * 16;
            if (size_t(data) + bytes > b.size())
                throw std::runtime_error("A move command runs past its script");
            entry.commands.push_back({head, std::vector<uint8_t>(b.begin() + data, b.begin() + data + bytes)});
        }
        entries[i] = std::move(entry);
    }
    return entries;
}

std::map<uint32_t, ScriptEntry> merged_entries(const std::vector<uint8_t> &shared, const std::vector<uint8_t> &own,
                                               uint32_t sharedMotions) {
    std::map<uint32_t, ScriptEntry> entries;
    for (const auto *script : {&shared, &own})
        for (auto &[index, entry] : script_entries(*script)) {
            const uint32_t motion = read32(entry.record, 0), bank = motion >> 16, at = motion & 0xFFFFu;
            if (script == &shared && bank == 0)
                write32(entry.record, 0, 1u << 16 | at);
            else if (script == &own && bank == 3)
                write32(entry.record, 0, 1u << 16 | (sharedMotions + at));
            else if (script == &own && bank == 0)
                write32(entry.record, 0, 1u << 16 | at);
            entries[index] = std::move(entry);
        }
    return entries;
}

std::vector<uint64_t> item_keys(const ScriptEntry &entry) {
    std::vector<uint64_t> keys;
    for (const auto &[head, data] : entry.commands)
        for (size_t at = 0; at + 16 <= data.size(); at += 16) {
            uint64_t h = 1469598103934665603ull ^ (head & 0xFFFFu);
            for (size_t i = 0; i < 16; ++i)
                h = (h ^ data[at + i]) * 1099511628211ull;
            keys.push_back(h);
        }
    std::sort(keys.begin(), keys.end());
    return keys;
}

double item_overlap(const std::vector<uint64_t> &a, const std::vector<uint64_t> &b) {
    size_t common = 0;
    for (size_t i = 0, j = 0; i < a.size() && j < b.size();)
        if (a[i] == b[j])
            ++common, ++i, ++j;
        else if (a[i] < b[j])
            ++i;
        else
            ++j;
    const size_t total = std::max({a.size(), b.size(), size_t(1)});
    return double(common) / double(total);
}

size_t shared_items(const std::vector<uint64_t> &a, const std::vector<uint64_t> &b) {
    size_t common = 0;
    for (size_t i = 0, j = 0; i < a.size() && j < b.size();)
        if (a[i] == b[j])
            ++common, ++i, ++j;
        else if (a[i] < b[j])
            ++i;
        else
            ++j;
    return common;
}

std::map<uint32_t, uint32_t> fill_entries(const std::map<uint32_t, ScriptEntry> &ours,
                                          const std::map<uint32_t, ScriptEntry> &theirs,
                                          const std::map<uint32_t, uint32_t> &anchors,
                                          std::map<uint32_t, uint32_t> chosen) {
    std::map<uint32_t, std::vector<uint64_t>> theirKeys;
    std::unordered_map<uint64_t, std::vector<uint32_t>> holders;
    for (const auto &[index, entry] : theirs) {
        auto keys = item_keys(entry);
        for (size_t k = 0; k < keys.size(); ++k)
            if (!k || keys[k] != keys[k - 1])
                holders[keys[k]].push_back(index);
        theirKeys.emplace(index, std::move(keys));
    }
    const auto predict = [&](uint32_t number) {
        const auto above = anchors.lower_bound(number);
        if (above != anchors.begin()) {
            const auto below = std::prev(above);
            if (above != anchors.end())
                return double(below->second) + (double(above->second) - double(below->second)) *
                                                   double(number - below->first) / double(above->first - below->first);
            return double(below->second) + double(number - below->first);
        }
        if (above != anchors.end())
            return double(above->second) - double(above->first - number);
        return double(number);
    };
    struct Pick {
        double overlap, distance;
        uint32_t index;
    };
    for (const auto &[index, entry] : ours) {
        if (chosen.count(index))
            continue;
        const auto keys = item_keys(entry);
        std::set<uint32_t> candidates;
        for (size_t k = 0; k < keys.size(); ++k)
            if (!k || keys[k] != keys[k - 1])
                if (const auto found = holders.find(keys[k]); found != holders.end())
                    candidates.insert(found->second.begin(), found->second.end());
        if (candidates.empty())
            continue;
        const double expected = predict(index);
        std::vector<Pick> picks;
        for (const uint32_t candidate : candidates)
            picks.push_back({item_overlap(keys, theirKeys.at(candidate)), std::fabs(double(candidate) - expected), candidate});
        std::sort(picks.begin(), picks.end(), [](const Pick &a, const Pick &b) {
            return a.overlap != b.overlap ? a.overlap > b.overlap : a.distance != b.distance ? a.distance < b.distance : a.index < b.index;
        });
        const auto &best = picks.front();
        const auto &bestKeys = theirKeys.at(best.index);
        if (best.overlap < 0.5 || shared_items(keys, bestKeys) < 4)
            continue;
        const auto rival = std::find_if(picks.begin() + 1, picks.end(),
                                        [&](const Pick &pick) { return theirKeys.at(pick.index) != bestKeys; });
        if (rival != picks.end() && rival->overlap > best.overlap - 0.1)
            continue;
        chosen.emplace(index, best.index);
    }
    return chosen;
}

std::map<uint32_t, uint32_t> align_entries(const std::map<uint32_t, ScriptEntry> &ours,
                                           const std::map<uint32_t, ScriptEntry> &theirs) {
    std::vector<uint32_t> a, b;
    std::vector<std::vector<uint64_t>> ka, kb;
    for (const auto &[index, entry] : ours)
        a.push_back(index), ka.push_back(item_keys(entry));
    for (const auto &[index, entry] : theirs)
        b.push_back(index), kb.push_back(item_keys(entry));
    const size_t n = a.size(), m = b.size();
    constexpr float gap = -0.15f;
    std::vector<float> score((n + 1) * (m + 1), 0.0f);
    std::vector<uint8_t> step((n + 1) * (m + 1), 0);
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    for (size_t i = 1; i <= n; ++i)
        score[at(i, 0)] = score[at(i - 1, 0)] + gap, step[at(i, 0)] = 1;
    for (size_t j = 1; j <= m; ++j)
        score[at(0, j)] = score[at(0, j - 1)] + gap, step[at(0, j)] = 2;
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j) {
            const double overlap = item_overlap(ka[i - 1], kb[j - 1]);
            const float pair = overlap > 0.0 ? float(overlap * 2.0 - 0.5) : -1.0f;
            const float diagonal = score[at(i - 1, j - 1)] + pair, up = score[at(i - 1, j)] + gap,
                        left = score[at(i, j - 1)] + gap;
            if (diagonal >= up && diagonal >= left)
                score[at(i, j)] = diagonal, step[at(i, j)] = 0;
            else if (up >= left)
                score[at(i, j)] = up, step[at(i, j)] = 1;
            else
                score[at(i, j)] = left, step[at(i, j)] = 2;
        }
    std::map<uint32_t, uint32_t> renumber;
    for (size_t i = n, j = m; i > 0 || j > 0;) {
        const uint8_t s = step[at(i, j)];
        if (i > 0 && j > 0 && s == 0) {
            if (item_overlap(ka[i - 1], kb[j - 1]) >= 0.5)
                renumber[a[i - 1]] = b[j - 1];
            --i, --j;
        } else if (i > 0 && (j == 0 || s == 1))
            --i;
        else
            --j;
    }
    return renumber;
}
}

std::map<uint32_t, uint32_t> align_move_scripts(const std::vector<uint8_t> &budokai2, const std::vector<uint8_t> &shared,
                                                const std::vector<uint8_t> &own, uint32_t sharedMotions) {
    return align_entries(script_entries(budokai2), merged_entries(shared, own, sharedMotions));
}

namespace {
struct CommandRecord {
    uint32_t move;
    std::vector<uint16_t> settings;
};
using CommandBlocks = std::map<std::pair<uint16_t, uint16_t>, std::vector<std::vector<CommandRecord>>>;

uint16_t read16(const std::vector<uint8_t> &bytes, size_t at) { return uint16_t(bytes[at] | bytes[at + 1] << 8); }

CommandBlocks command_blocks(const std::vector<uint8_t> &bcm) {
    CommandBlocks blocks;
    if (bcm.size() < 0x50 || std::memcmp(bcm.data(), "#BCM", 4) != 0)
        return blocks;
    const uint32_t count = read16(bcm, 0x1E);
    if (0x50 + size_t(count) * 4 > bcm.size())
        return blocks;
    for (uint32_t b = 0; b < count; ++b) {
        const size_t begin = read32(bcm, 0x50 + b * 4), end = b + 1 < count ? read32(bcm, 0x50 + (b + 1) * 4) : bcm.size();
        if (begin + 4 > end || end > bcm.size())
            continue;
        std::vector<uint16_t> words;
        for (size_t at = begin; at + 2 <= end; at += 2)
            words.push_back(read16(bcm, at));
        std::vector<CommandRecord> records;
        for (size_t i = 0; i + 2 < words.size(); ++i)
            if (words[i] >= budokai3_command_first && words[i] < budokai3_command_first + 256 &&
                words[i + 1] == words[i] + 256 && words[i + 2] == words[i] + 256) {
                CommandRecord record;
                record.move = words[i];
                for (size_t k = i + 3; k < words.size() && k < i + 3 + budokai3_command_settings; ++k)
                    record.settings.push_back(words[k]);
                records.push_back(std::move(record));
            }
        blocks[{words[0], words[1]}].push_back(std::move(records));
    }
    return blocks;
}

double settings_match(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
    size_t same = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        same += a[i] == b[i];
    return double(same) / double(std::max({a.size(), b.size(), size_t(1)}));
}
}

std::map<uint32_t, uint32_t> command_renumber(const std::vector<uint8_t> &budokai2, const std::vector<uint8_t> &budokai3) {
    const auto ours = command_blocks(budokai2), theirs = command_blocks(budokai3);
    std::map<uint32_t, uint32_t> renumber;
    for (const auto &[head, blocks] : ours) {
        const auto other = theirs.find(head);
        if (other == theirs.end())
            continue;
        for (size_t k = 0; k < blocks.size() && k < other->second.size(); ++k) {
            const auto &a = blocks[k], &b = other->second[k];
            const size_t n = a.size(), m = b.size();
            constexpr float gap = -0.15f;
            std::vector<float> score((n + 1) * (m + 1), 0.0f);
            std::vector<uint8_t> step((n + 1) * (m + 1), 0);
            const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
            for (size_t i = 1; i <= n; ++i)
                score[at(i, 0)] = score[at(i - 1, 0)] + gap, step[at(i, 0)] = 1;
            for (size_t j = 1; j <= m; ++j)
                score[at(0, j)] = score[at(0, j - 1)] + gap, step[at(0, j)] = 2;
            for (size_t i = 1; i <= n; ++i)
                for (size_t j = 1; j <= m; ++j) {
                    const double match = settings_match(a[i - 1].settings, b[j - 1].settings);
                    const float pair = match > 0.0 ? float(match * 2.0 - 0.5) : -1.0f;
                    const float diagonal = score[at(i - 1, j - 1)] + pair, up = score[at(i - 1, j)] + gap,
                                left = score[at(i, j - 1)] + gap;
                    if (diagonal >= up && diagonal >= left)
                        score[at(i, j)] = diagonal, step[at(i, j)] = 0;
                    else if (up >= left)
                        score[at(i, j)] = up, step[at(i, j)] = 1;
                    else
                        score[at(i, j)] = left, step[at(i, j)] = 2;
                }
            for (size_t i = n, j = m; i > 0 || j > 0;) {
                const uint8_t s = step[at(i, j)];
                if (i > 0 && j > 0 && s == 0) {
                    if (settings_match(a[i - 1].settings, b[j - 1].settings) >= 0.5) {
                        renumber.emplace(a[i - 1].move, b[j - 1].move);
                        renumber.emplace(a[i - 1].move + 256, b[j - 1].move + 256);
                    }
                    --i, --j;
                } else if (i > 0 && (j == 0 || s == 1))
                    --i;
                else
                    --j;
            }
        }
    }
    return renumber;
}

std::map<uint32_t, uint32_t> fill_move_matches(const std::vector<uint8_t> &budokai2, const std::vector<uint8_t> &shared,
                                               const std::vector<uint8_t> &own, uint32_t sharedMotions,
                                               const std::map<uint32_t, uint32_t> &anchors,
                                               const std::map<uint32_t, uint32_t> &chosen) {
    return fill_entries(script_entries(budokai2), merged_entries(shared, own, sharedMotions), anchors, chosen);
}

std::vector<uint8_t> merge_move_scripts(const std::vector<uint8_t> &shared, const std::vector<uint8_t> &own,
                                        uint32_t sharedMotions, const std::map<uint32_t, uint32_t> &renumber,
                                        const std::vector<uint8_t> &wearer) {
    const auto merged = merged_entries(shared, own, sharedMotions);
    auto entries = merged;
    uint32_t count = std::max(read32(shared, 16), read32(own, 16));
    for (const auto &[budokai2, budokai3] : renumber)
        if (const auto found = merged.find(budokai3); found != merged.end()) {
            entries[budokai2] = found->second;
            count = std::max(count, budokai2 + 1);
        }
    if (!wearer.empty()) {
        const auto kind = [](uint32_t head) { return head & 0xFFFFu; };
        const auto has = [&](const ScriptEntry &entry, uint32_t type) {
            return std::any_of(entry.commands.begin(), entry.commands.end(),
                               [&](const auto &command) { return kind(command.first) == type; });
        };
        const auto theirs = script_entries(wearer);
        for (auto &[index, entry] : entries) {
            const auto found = theirs.find(index);
            if (found == theirs.end() || has(entry, 5) || !has(found->second, 5))
                continue;
            for (const auto &command : found->second.commands)
                if ((kind(command.first) == 0 || kind(command.first) == 5) && !has(entry, kind(command.first)))
                    entry.commands.push_back(command);
            std::stable_sort(entry.commands.begin(), entry.commands.end(),
                             [&](const auto &a, const auto &b) { return kind(a.first) < kind(b.first); });
        }
    }
    size_t commandCount = 0;
    for (const auto &[index, entry] : entries)
        commandCount += entry.commands.size();
    const uint32_t records = uint32_t((0x20 + count * 4 + 15) / 16 * 16);
    const uint32_t lists = records + uint32_t(entries.size()) * 48;
    const uint32_t payloads = uint32_t((lists + commandCount * 8 + 15) / 16 * 16);
    std::vector<uint8_t> out(payloads, 0);
    std::memcpy(out.data(), "#BSK", 4);
    write32(out, 4, 0x20);
    write32(out, 12, 3);
    write32(out, 16, count);
    write32(out, 20, 0x20);
    write32(out, 24, read32(own, 24));
    uint32_t record = records, list = lists;
    for (auto &[index, entry] : entries) {
        write32(out, 0x20 + index * 4, record);
        write32(entry.record, 40, uint32_t(entry.commands.size()));
        write32(entry.record, 44, list);
        std::copy(entry.record.begin(), entry.record.end(), out.begin() + record);
        record += 48;
        for (const auto &[head, data] : entry.commands) {
            write32(out, list, head);
            write32(out, list + 4, uint32_t(out.size()));
            out.insert(out.end(), data.begin(), data.end());
            list += 8;
        }
    }
    out.resize((out.size() + 15) / 16 * 16, 0);
    write32(out, 28, uint32_t(out.size()));
    const uint32_t tail = read32(own, 28);
    if (tail <= own.size())
        out.insert(out.end(), own.begin() + tail, own.end());
    return out;
}

std::map<uint32_t, uint32_t> combine_renumbers(const std::vector<std::map<uint32_t, uint32_t>> &tables,
                                               const std::map<uint32_t, uint32_t> &own) {
    std::map<uint32_t, std::map<uint32_t, int>> votes;
    for (const auto &table : tables)
        for (const auto &[budokai2, budokai3] : table)
            ++votes[budokai2][budokai3];
    auto combined = own;
    for (const auto &[budokai2, choices] : votes) {
        int total = 0, best = 0;
        uint32_t chosen = 0;
        for (const auto &[budokai3, count] : choices) {
            total += count;
            if (count > best)
                best = count, chosen = budokai3;
        }
        if (best >= 3 && best * 2 > total)
            combined.emplace(budokai2, chosen);
    }
    return combined;
}

namespace {
std::vector<uint8_t> script_part(const std::vector<uint8_t> &file, uint32_t version) {
    for (uint32_t part = 0; part < 8; ++part) {
        auto piece = amb_part(file, part);
        if (piece.size() >= 16 && std::memcmp(piece.data(), "#BSK", 4) == 0 && read32(piece, 12) == version)
            return piece;
    }
    return {};
}

std::optional<size_t> own_script_entry(SourceDisc &budokai3, size_t model) {
    const auto &entries = budokai3.entries(roster_archive);
    for (size_t entry = std::min(model, entries.size()); entry > 0 && model - entry < budokai3_group_span;) {
        --entry;
        const auto first = amb_part(budokai3.read_entry(roster_archive, entry), 0);
        if (first.size() >= 4 && std::memcmp(first.data(), "#AMC", 4) == 0)
            return std::nullopt;
        if (first.size() >= 16 && std::memcmp(first.data(), "#BSK", 4) == 0 && read32(first, 12) == 4)
            return entry;
    }
    return std::nullopt;
}

std::optional<size_t> effects_entry(SourceDisc &budokai3, size_t model) {
    const auto &entries = budokai3.entries(roster_archive);
    for (size_t entry = model + 1; entry < entries.size() && entry - model <= budokai3_group_span; ++entry) {
        const auto first = amb_part(budokai3.read_entry(roster_archive, entry), 0);
        if (first.size() >= 4 && std::memcmp(first.data(), "#AMC", 4) == 0)
            return entry;
        if (first.size() >= 4 && std::memcmp(first.data(), "#BSK", 4) == 0)
            return std::nullopt;
    }
    return std::nullopt;
}

std::map<uint32_t, uint32_t> roster_renumber(SourceDisc &budokai3, size_t model, SourceDisc &game,
                                             const std::vector<uint8_t> &sharedScript, uint32_t sharedMotions,
                                             bool &ownMatched) {
    ownMatched = false;
    const auto models = read_budokai3_models(budokai3);
    const auto roster = read_disc_roster(game);
    if (!models || !roster)
        return {};
    std::string imported;
    for (const auto &character : models->characters)
        if (!character.files.empty() && character.files.front().second == model)
            imported = character.internal;
    std::map<std::string, size_t> budokai2Scripts;
    for (const auto &character : roster->characters)
        for (const auto &[field, entry] : character.files)
            if (field == moves_field)
                budokai2Scripts.emplace(character.internal, entry);
    std::vector<std::map<uint32_t, uint32_t>> tables;
    std::map<uint32_t, uint32_t> own;
    std::vector<uint8_t> importedOurs, importedTheirs;
    std::set<std::string> done;
    for (const auto &character : models->characters) {
        const auto counterpart = budokai2Scripts.find(character.internal);
        if (character.files.empty() || counterpart == budokai2Scripts.end() || !done.insert(character.internal).second)
            continue;
        const auto scriptEntry = own_script_entry(budokai3, character.files.front().second);
        if (!scriptEntry)
            continue;
        const auto theirs = amb_part(budokai3.read_entry(roster_archive, *scriptEntry), 0);
        const auto ours = script_part(game.read_entry(roster_archive, counterpart->second), 3);
        if (ours.empty())
            continue;
        tables.push_back(align_move_scripts(ours, sharedScript, theirs, sharedMotions));
        if (character.internal == imported) {
            own = tables.back();
            ownMatched = true;
            importedOurs = ours;
            importedTheirs = theirs;
        }
    }
    if (tables.empty())
        return {};
    const auto combined = combine_renumbers(tables, own);
    if (!ownMatched)
        return combined;
    return fill_move_matches(importedOurs, sharedScript, importedTheirs, sharedMotions, own, combined);
}
}

std::optional<Budokai3Moveset> budokai3_moveset(SourceDisc &budokai3, size_t model, SourceDisc &game,
                                                size_t ownMotions, size_t ownMoves) {
    const auto scriptEntry = own_script_entry(budokai3, model), effectsEntry = effects_entry(budokai3, model);
    if (!scriptEntry || !effectsEntry)
        return std::nullopt;
    const auto shared = budokai3.read_entry(roster_archive, budokai3_shared_moves);
    const auto own = budokai3.read_entry(roster_archive, *scriptEntry);
    const auto effects = budokai3.read_entry(roster_archive, *effectsEntry);
    const auto sharedMotions = amb_part(shared, 0), ownMotionsPack = amb_part(own, 1);
    const auto sharedScript = amb_part(shared, 4), ownScript = amb_part(own, 0);
    if (sharedMotions.empty() || ownMotionsPack.empty() || sharedScript.empty() || ownScript.empty())
        return std::nullopt;
    const auto extra = amb_part(game.read_entry(roster_archive, ownMotions), 1);
    const auto wearerMoves = game.read_entry(roster_archive, ownMoves);
    const auto commands = amb_part(wearerMoves, 1);
    if (commands.size() < 4 || std::memcmp(commands.data(), "#BCM", 4) != 0)
        return std::nullopt;
    std::map<uint32_t, uint32_t> renumber;
    bool ownMatched = false;
    try {
        renumber = roster_renumber(budokai3, model, game, sharedScript, read32(sharedMotions, 16), ownMatched);
    } catch (const std::exception &) {
        renumber.clear();
    }
    if (renumber.empty()) {
        try {
            const auto gokuScript = script_part(game.read_entry(roster_archive, budokai2_goku_moves), 3);
            const auto gokuOwn = amb_part(budokai3.read_entry(roster_archive, budokai3_goku_own_moves), 0);
            if (!gokuScript.empty() && gokuOwn.size() >= 16 && std::memcmp(gokuOwn.data(), "#BSK", 4) == 0 &&
                read32(gokuOwn, 12) == 4)
                renumber = align_move_scripts(gokuScript, sharedScript, gokuOwn, read32(sharedMotions, 16));
        } catch (const std::exception &) {
            renumber.clear();
        }
    }
    const auto merged = merged_entries(sharedScript, ownScript, read32(sharedMotions, 16));
    for (const auto &[budokai2, budokai3] : command_renumber(commands, amb_part(effects, 2)))
        if (merged.count(budokai3))
            renumber[budokai2] = budokai3;
    Budokai3Moveset moveset;
    moveset.renumbered = renumber.size();
    moveset.ownMatched = ownMatched;
    moveset.motions = amb_of({merge_motions({sharedMotions, ownMotionsPack}), extra}, {3, 3});
    moveset.moves = amb_of({amb_part(effects, 0), commands,
                            merge_move_scripts(sharedScript, ownScript, read32(sharedMotions, 16), renumber,
                                               script_part(wearerMoves, 3)),
                            amb_part(effects, 3)},
                           {5, 0xFFFFFFFFu, 0xFFFFFFFFu, 8});
    return moveset;
}

}
