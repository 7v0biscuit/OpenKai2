// SPDX-License-Identifier: GPL-3.0-only
#include "mods/stages.h"
#include "mods/disc_identity.h"
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

namespace {
std::optional<std::vector<DiscStage>> read_budokai3_stages(SourceDisc &disc) {
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file("SLUS_209.98");
    } catch (const std::runtime_error &) {
        return std::nullopt;
    }
    const auto image = load_executable(elf);
    if (!image || !image->contains(budokai3_stages, budokai3_stage_count * budokai3_stage_bytes))
        return std::nullopt;
    const auto &memory = image->memory;
    const auto name_at = [&](uint32_t at) {
        std::string name;
        for (uint32_t i = 0; i < 16 && memory[at + i]; ++i)
            name += char(memory[at + i]);
        return name;
    };
    const auto word = [&](uint32_t at) {
        return memory[at] | memory[at + 1] << 8 | memory[at + 2] << 16 | uint32_t(memory[at + 3]) << 24;
    };
    if (name_at(budokai3_stages) != "BUDO" || name_at(budokai3_stage_names) != "GOKU")
        return std::nullopt;
    const auto entries = disc.entries(roster_archive).size();
    std::vector<DiscStage> stages;
    for (uint32_t i = 0; i < budokai3_stage_count; ++i) {
        const uint32_t record = budokai3_stages + i * budokai3_stage_bytes;
        DiscStage stage;
        stage.index = i;
        stage.internal = name_at(record);
        stage.variant = stage.internal.size() > 2 && stage.internal.compare(stage.internal.size() - 2, 2, " B") == 0;
        for (const uint32_t at : {0x10u, 0x14u})
            if (const auto entry = word(record + at); entry && entry < entries)
                stage.files.push_back({at, entry});
        stages.push_back(std::move(stage));
    }
    return stages;
}
} // namespace

namespace {
namespace fs = std::filesystem;
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
uint32_t get32(const std::vector<uint8_t> &bytes, size_t at) {
    return at + 4 <= bytes.size() ? bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | uint32_t(bytes[at + 3]) << 24
                                  : 0u;
}
// stages.ini split into the lines before the first section and each section.
struct StageFile {
    std::vector<std::string> head;
    std::vector<std::vector<std::string>> sections;
};
StageFile split_stages(const fs::path &mod) {
    StageFile file;
    std::ifstream in(mod / "stages.ini");
    for (std::string line; std::getline(in, line);) {
        if (lower(trim(line)) == "[stage]")
            file.sections.emplace_back();
        (file.sections.empty() ? file.head : file.sections.back()).push_back(line);
    }
    return file;
}
StageSection parse_stage(const std::vector<std::string> &lines) {
    StageSection section;
    for (const auto &line : lines) {
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const auto key = lower(trim(line.substr(0, equals)));
        const auto value = trim(line.substr(equals + 1));
        if (key == "slot")
            try {
                section.slot = uint32_t(std::stoul(value, nullptr, 0));
            } catch (const std::exception &) {
            }
        else if (key == "name")
            section.name = value;
        else if (key.rfind("file.", 0) == 0 && lower(value).rfind("add:", 0) == 0)
            section.added.push_back(trim(value.substr(4)));
    }
    return section;
}
// The files of an #AMB pack, as bytes, in record order.
std::vector<std::vector<uint8_t>> pack_files(const std::vector<uint8_t> &pack) {
    if (pack.size() < 32 || std::memcmp(pack.data(), "#AMB", 4) != 0)
        throw std::runtime_error("A Budokai 3 stage pack is not an #AMB");
    const uint32_t count = get32(pack, 16), records = get32(pack, 20);
    if (count > 64 || records + uint64_t(count) * 16 > pack.size())
        throw std::runtime_error("A Budokai 3 stage pack's records run past its end");
    std::vector<std::vector<uint8_t>> files;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = get32(pack, records + i * 16), size = get32(pack, records + i * 16 + 4);
        if (at > pack.size() || size > pack.size() - at)
            throw std::runtime_error("A Budokai 3 stage file runs past its pack");
        files.emplace_back(pack.begin() + at, pack.begin() + at + size);
    }
    return files;
}
// Marks #AMT texture packs version 1, at the top level of `file` or inside it
// when it is an #AMB.
void texture_version_1(std::vector<uint8_t> &file) {
    const auto mark = [](std::vector<uint8_t> &bytes, size_t at) {
        if (at + 16 <= bytes.size() && std::memcmp(bytes.data() + at, "#AMT", 4) == 0 && get32(bytes, at + 12) == 2)
            bytes[at + 12] = 1;
    };
    mark(file, 0);
    if (file.size() >= 32 && std::memcmp(file.data(), "#AMB", 4) == 0) {
        const uint32_t count = std::min<uint32_t>(get32(file, 16), 64), records = get32(file, 20);
        for (uint32_t i = 0; i < count; ++i)
            mark(file, get32(file, records + i * 16));
    }
}
} // namespace

std::vector<StageMod> load_stage_mods(const std::vector<fs::path> &mods, const DiscOverlay::Summary &overlay) {
    const auto key = IsoIndex::key(roster_archive).value_or(std::string());
    const auto added = overlay.added.find(key);
    std::vector<StageMod> stages;
    for (const auto &mod : mods) {
        const auto path = mod / "stages.ini";
        std::ifstream in(path);
        if (!in)
            continue;
        unsigned number = 0;
        std::optional<StageMod> current;
        const auto finish = [&](const std::string &where) {
            if (!current)
                return;
            if (current->slot < stage_first_variant || current->slot >= stage_count)
                throw std::runtime_error(where + ": slot must be a B stage, " + std::to_string(stage_first_variant) +
                                         " to " + std::to_string(stage_count - 1));
            if (current->files.size() != (stage_files_end - stage_files_begin) / 4)
                throw std::runtime_error(where + ": a stage needs all eight files, file.40 to file.5c");
            stages.erase(std::remove_if(stages.begin(), stages.end(),
                                        [&](const StageMod &other) { return other.slot == current->slot; }),
                         stages.end());
            stages.push_back(std::move(*current));
            current.reset();
        };
        for (std::string line; std::getline(in, line);) {
            ++number;
            const auto where = path.string() + ":" + std::to_string(number);
            const auto text = trim(line);
            if (text.empty() || text[0] == '#' || text[0] == ';')
                continue;
            if (lower(text) == "[stage]") {
                finish(where);
                current = StageMod{};
                current->source = path;
                continue;
            }
            const auto equals = text.find('=');
            if (!current || equals == std::string::npos)
                throw std::runtime_error(where + ": expected [stage] or key = value");
            const auto name = lower(trim(text.substr(0, equals)));
            const auto value = trim(text.substr(equals + 1));
            const auto number_of = [&](const std::string &digits) {
                try {
                    size_t used = 0;
                    const auto parsed = std::stoul(digits, &used, 0);
                    if (used == digits.size())
                        return uint32_t(parsed);
                } catch (const std::exception &) {
                }
                throw std::runtime_error(where + ": expected a number, not " + digits);
            };
            if (name == "slot")
                current->slot = number_of(value);
            else if (name == "name") {
                if (value.empty() || value.size() >= 32)
                    throw std::runtime_error(where + ": a stage name needs 1 to 31 characters");
                current->name = value;
            } else if (name.rfind("file.", 0) == 0) {
                const auto offset = number_of("0x" + name.substr(5));
                if (offset < stage_files_begin || offset >= stage_files_end || offset % 4)
                    throw std::runtime_error(where + ": stage files are file.40 to file.5c");
                uint32_t entry = 0;
                if (lower(value).rfind("add:", 0) == 0) {
                    const auto wanted = lower(trim(value.substr(4)));
                    if (added == overlay.added.end() || !added->second.count(wanted))
                        throw std::runtime_error(where + ": no enabled mod adds " + value.substr(4) + " to " + roster_archive);
                    entry = added->second.at(wanted);
                } else
                    entry = number_of(value);
                current->files.erase(std::remove_if(current->files.begin(), current->files.end(),
                                                    [&](const auto &file) { return file.first == offset; }),
                                     current->files.end());
                current->files.push_back({offset, entry});
            } else
                throw std::runtime_error(where + ": unknown key " + name);
        }
        finish(path.string() + ":" + std::to_string(number));
    }
    return stages;
}

std::vector<std::string> apply_stage_mods(uint8_t *memory, size_t bytes, const std::vector<StageMod> &stages) {
    std::vector<std::string> log;
    if (stages.empty())
        return log;
    const auto name_at = [&](uint32_t at) {
        std::string name;
        for (uint32_t i = 0; i < 32 && memory[at + i]; ++i)
            name += char(memory[at + i]);
        return name;
    };
    if (bytes < stage_records + stage_count * stage_record_bytes || name_at(stage_records) != "ISLAND" ||
        name_at(stage_records + (stage_count - 1) * stage_record_bytes) != "SNOW B")
        throw std::runtime_error("Stage mods need the Budokai 2 USA stage table, which this executable does not hold");
    for (const auto &stage : stages) {
        uint8_t *record = memory + stage_records + stage.slot * stage_record_bytes;
        const auto previous = name_at(stage_records + stage.slot * stage_record_bytes);
        std::memset(record, 0, 32);
        const auto name = stage.name.empty() ? previous : stage.name;
        std::memcpy(record, name.data(), std::min<size_t>(name.size(), 31));
        for (const auto &[offset, entry] : stage.files)
            for (unsigned i = 0; i < 4; ++i)
                record[offset + i] = uint8_t(entry >> (8 * i));
        for (unsigned i = 0; i < 4; ++i)
            record[stage_variant + i] = 0;
        log.push_back("stage: record " + std::to_string(stage.slot) + " is " + name + " (was " + previous +
                      "), with its own files");
    }
    return log;
}

void append_stage(const fs::path &mod, uint32_t slot, const std::string &name,
                  const std::vector<std::pair<uint32_t, std::string>> &files) {
    std::ofstream out(mod / "stages.ini", std::ios::app);
    out << "\n[stage]\nslot = " << slot << "\nname = " << name << '\n';
    for (const auto &[offset, value] : files) {
        char key[16];
        std::snprintf(key, sizeof key, "file.%x", offset);
        out << key << " = " << value << '\n';
    }
    if (!out)
        throw std::runtime_error("Cannot write the mod's stages.ini");
}

std::vector<StageSection> read_stage_sections(const fs::path &mod) {
    std::vector<StageSection> sections;
    for (const auto &lines : split_stages(mod).sections)
        sections.push_back(parse_stage(lines));
    return sections;
}

size_t remove_stage(const fs::path &mod, uint32_t slot) {
    auto file = split_stages(mod);
    std::vector<std::string> removed;
    std::vector<std::vector<std::string>> kept;
    bool found = false;
    for (auto &lines : file.sections) {
        const auto section = parse_stage(lines);
        if (section.slot == slot && !found) {
            found = true;
            removed = section.added;
        } else
            kept.push_back(std::move(lines));
    }
    if (!found)
        throw std::runtime_error("The mod has no stage in record " + std::to_string(slot));
    for (const auto &lines : kept)
        for (const auto &name : parse_stage(lines).added)
            removed.erase(std::remove(removed.begin(), removed.end(), name), removed.end());
    {
        std::ofstream out(mod / "stages.ini", std::ios::trunc);
        for (const auto &line : file.head)
            out << line << '\n';
        for (const auto &lines : kept)
            for (const auto &line : lines)
                out << line << '\n';
    }
    size_t count = 0;
    std::error_code error;
    std::vector<fs::path> gone;
    for (fs::recursive_directory_iterator it(mod / "afs", error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error) && lower(it->path().parent_path().filename().string()) == "add" &&
            std::find(removed.begin(), removed.end(), it->path().filename().string()) != removed.end())
            gone.push_back(it->path());
    for (const auto &path : gone)
        count += fs::remove(path, error);
    return count;
}

std::vector<std::pair<uint32_t, std::vector<uint8_t>>> budokai3_stage_files(SourceDisc &disc, const DiscStage &stage) {
    if (stage.files.size() != 2)
        throw std::runtime_error("The stage " + stage.internal + " does not name two packs");
    const auto first = pack_files(disc.read_entry(roster_archive, stage.files[0].second));
    const auto second = pack_files(disc.read_entry(roster_archive, stage.files[1].second));
    if (second.size() != 3)
        throw std::runtime_error("The stage " + stage.internal + "'s second pack does not hold three files");
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> files;
    for (const auto &[mark, offset] : std::initializer_list<std::pair<const char *, uint32_t>>{
             {"#MDD", 0x40u}, {"#MAD", 0x44u}, {"#MAS", 0x48u}, {"#AMB", 0x4Cu}, {"#SPX", 0x50u}}) {
        const auto found = std::find_if(first.begin(), first.end(), [&](const std::vector<uint8_t> &file) {
            return file.size() >= 4 && std::memcmp(file.data(), mark, 4) == 0;
        });
        if (found == first.end())
            throw std::runtime_error("The stage " + stage.internal + " has no " + mark + " file");
        auto bytes = *found;
        texture_version_1(bytes);
        files.push_back({offset, std::move(bytes)});
    }
    for (uint32_t i = 0; i < 3; ++i)
        files.push_back({0x54u + 4 * i, second[i]});
    return files;
}

std::string stage_display_name(const std::string &internal) {
    // Only names that are certain; the rest show as the game names them.
    static const std::map<std::string, std::string> names{
        {"ROOM TIME MIND", "Hyperbolic Time Chamber"}, {"MIND & TIME", "Hyperbolic Time Chamber"},
        {"BUDO", "World Tournament"}, {"NAMEC", "Namek"}, {"BUU BODY", "Inside Buu"}};
    std::string base = internal, suffix;
    if (base.size() > 2 && base.compare(base.size() - 2, 2, " B") == 0) {
        base.resize(base.size() - 2);
        suffix = " B";
    }
    const auto found = names.find(base);
    if (found != names.end())
        return found->second + suffix;
    // Other names in title case: ISLAND B is "Island B", R&R BASE "R&R Base".
    std::string shown;
    bool start = true;
    for (const unsigned char c : internal) {
        shown += char(start ? c : std::tolower(c));
        start = !std::isalpha(c);
    }
    return shown;
}

std::optional<std::vector<DiscStage>> read_disc_stages(SourceDisc &disc) {
    if (auto stages = read_budokai3_stages(disc))
        return stages;
    std::vector<uint8_t> elf;
    try {
        elf = disc.read_file("SLUS_207.79");
    } catch (const std::runtime_error &) {
        return std::nullopt;
    }
    const auto image = load_executable(elf);
    if (!image || !image->contains(stage_records, stage_count * stage_record_bytes))
        return std::nullopt;
    const auto &memory = image->memory;
    const auto name_at = [&](uint32_t at) {
        std::string name;
        for (uint32_t i = 0; i < 32 && memory[at + i]; ++i)
            name += char(memory[at + i]);
        return name;
    };
    const auto word = [&](uint32_t at) {
        return memory[at] | memory[at + 1] << 8 | memory[at + 2] << 16 | uint32_t(memory[at + 3]) << 24;
    };
    if (name_at(stage_records) != "ISLAND" ||
        name_at(stage_records + (stage_count - 1) * stage_record_bytes) != "SNOW B")
        return std::nullopt;
    const auto entries = disc.entries(roster_archive).size();
    std::vector<DiscStage> stages;
    for (uint32_t i = 0; i < stage_count; ++i) {
        const uint32_t record = stage_records + i * stage_record_bytes;
        DiscStage stage;
        stage.index = i;
        stage.internal = name_at(record);
        stage.variant = word(record + stage_variant) == 1;
        for (uint32_t at = stage_files_begin; at < stage_files_end; at += 4)
            if (const auto entry = word(record + at); entry && entry < entries)
                stage.files.push_back({at, entry});
        stages.push_back(std::move(stage));
    }
    return stages;
}

} // namespace openkai2::mods
