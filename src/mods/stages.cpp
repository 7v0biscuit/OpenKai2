// SPDX-License-Identifier: GPL-3.0-only
#include "mods/stages.h"
#include "mods/disc_identity.h"
#include "mods/roster.h"
#include <stdexcept>

namespace openkai2::mods {

std::optional<std::vector<DiscStage>> read_disc_stages(SourceDisc &disc) {
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
