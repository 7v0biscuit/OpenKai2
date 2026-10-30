// SPDX-License-Identifier: GPL-3.0-only
#include "mods/stages.h"
#include "mods/disc_identity.h"
#include "mods/roster.h"
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
    return found == names.end() ? internal : found->second + suffix;
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
