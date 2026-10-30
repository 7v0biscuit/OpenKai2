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
    // The texture pack counts its textures at 16 and lists their headers
    // from the offset at 20; a header gives the format at 8 and the size at 12.
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
    // In order: each of the wearer's slots takes the next of the model's
    // textures with its format and size, skipping the model's extras.
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
    // Textures: a 32-byte header counting them at 16, offsets from 20, then a
    // 48-byte header each whose pixels are at 20 and palette at 36, both from
    // the start of the pack.
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
    // Mesh blocks: the mesh header counts them at 32 and lists their offsets
    // from 48.
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
    // The #AMB records give each file's size.
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
    // Vertex data is floats; a word that is not a float of a plausible size
    // on either side is a count, a flag or a GIF or VIF code.
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
    // The mesh header counts its blocks at 32 and lists them from 48; a block
    // runs to the next one or to the end of the mesh. Material records sit at
    // 16-byte boundaries inside the blocks: flags, 0x29BD, then two texture
    // numbers.
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
    // The records sit in the mesh's first block at 16-byte boundaries: a word
    // of 0x2xx with a kind in its top four bits, then the offset in the mesh
    // of what it draws.
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

std::vector<uint8_t> model_for_budokai2(SourceDisc &disc, size_t entry) {
    const auto range = disc.range(roster_archive, entry);
    const auto parts = model_parts(disc, range);
    if (!parts)
        throw std::runtime_error("Entry " + std::to_string(entry) + " is not a costume model");
    auto bytes = disc.read_entry(roster_archive, entry);
    const size_t version = size_t(parts->second) + 12;
    if (version + 4 <= bytes.size() && read32(bytes, version) == 2)
        bytes[version] = 1;
    // Budokai 2 sets a flag in every material that Budokai 3 leaves clear.
    for (const auto record : material_records(bytes, false))
        bytes[record + 1] |= uint8_t(budokai2_material_flag >> 8);
    // And one in every draw record of the mesh's first block.
    for (const auto record : draw_records(bytes))
        bytes[record] |= uint8_t(budokai2_draw_flag);
    return bytes;
}

namespace {
// The mesh and the textures of a costume model, each as its own bytes.
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
// An #AMB of a mesh and textures, laid out as the games lay theirs: the
// header, two records, then each file at a 64-byte boundary.
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
// A texture pack's textures: each 48-byte header, then its pixels and its
// palette, at the offsets the header gives from the start of the pack.
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
        texture.pixels = slice(read32(texture.header, 20), read32(texture.header, 24));
        texture.palette = slice(read32(texture.header, 36), read32(texture.header, 40));
        textures.push_back(std::move(texture));
    }
    return textures;
}
std::vector<std::array<uint32_t, 2>> layout_of(const std::vector<PackTexture> &textures) {
    std::vector<std::array<uint32_t, 2>> layout;
    for (const auto &texture : textures)
        layout.push_back({read32(texture.header, 8), read32(texture.header, 12)});
    return layout;
}
} // namespace

std::vector<uint8_t> match_pack(const std::vector<uint8_t> &theirs, const std::vector<uint8_t> &ours) {
    const auto source = pack_textures(theirs), wanted = pack_textures(ours);
    const auto picks = match_textures(layout_of(source), layout_of(wanted));
    if (picks.empty())
        throw std::runtime_error("The textures do not match the body's textures");
    // A pack laid out as the games lay theirs: the 32-byte header, the offset
    // table, the texture headers, then each texture's pixels and palette at
    // 16-byte boundaries.
    const auto align = [](size_t at) { return (at + 15) / 16 * 16; };
    const uint32_t count = uint32_t(picks.size()), table = 0x20, headers = uint32_t(align(table + count * 4));
    std::vector<uint8_t> pack(align(headers + count * 48), 0);
    std::copy(theirs.begin(), theirs.begin() + 0x20, pack.begin());
    put32(pack, 12, 1); // Budokai 2's version
    put32(pack, 16, count);
    put32(pack, 20, table);
    for (uint32_t i = 0; i < count; ++i) {
        auto header = source[picks[i]].header;
        put32(header, 0, i);
        const auto place = [&](const std::vector<uint8_t> &bytes) {
            const auto at = uint32_t(pack.size());
            pack.insert(pack.end(), bytes.begin(), bytes.end());
            pack.resize(align(pack.size()), 0);
            return at;
        };
        put32(header, 20, place(source[picks[i]].pixels));
        put32(header, 36, place(source[picks[i]].palette));
        put32(pack, table + i * 4, headers + i * 48);
        std::copy(header.begin(), header.end(), pack.begin() + headers + i * 48);
    }
    return pack;
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
    return version == ModelVersion::budokai2_textures ? join_model(theirs, theirMesh, ourTextures)
                                                      : join_model(ours, ourMesh, match_pack(theirTextures, ourTextures));
}

} // namespace openkai2::mods
